#include "cartridge.h"
#include "mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static void be16(u8 *p, unsigned n) { p[0] = n >> 8; p[1] = n; }
static void be32(u8 *p, unsigned n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}

static void write_image(const char *path, const u8 *data, size_t size) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "open temporary cartridge image");
    if (!f) return;
    CHECK(fwrite(data, 1, size, f) == size, "write temporary cartridge image");
    fclose(f);
}

static size_t add_chip(u8 *image, size_t at, unsigned address,
                       unsigned size, u8 value) {
    memcpy(image + at, "CHIP", 4);
    be32(image + at + 4, size + 16);
    be16(image + at + 8, 0);  /* ROM */
    be16(image + at + 10, 0); /* bank 0 */
    be16(image + at + 12, address);
    be16(image + at + 14, size);
    memset(image + at + 16, value, size);
    return at + 16 + size;
}

int main(void) {
    char dir[] = "/tmp/1986-cart-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "create temporary cartridge directory");
    char raw_path[128], crt_path[128];
    snprintf(raw_path, sizeof(raw_path), "%s/raw.bin", dir);
    snprintf(crt_path, sizeof(crt_path), "%s/generic.crt", dir);
    u8 *image = calloc(1, 0x11000);
    Mem *mem = calloc(1, sizeof(*mem));
    CHECK(image && mem, "allocate cartridge test fixtures");
    if (!image || !mem) return 1;
    mem_init(mem);

    for (size_t size = 0x2000; size <= 0x10000; size *= 2) {
        for (size_t i = 0; i < size; ++i) image[i] = (u8)(i % 251);
        if (size == 0x10000)
            memcpy(image + 0x8000, image, 0x8000);
        write_image(raw_path, image, size);
        CHECK(cartridge_attach(&mem->cart, raw_path) == CART_OK,
              "raw 8/16/32/64K function ROM attaches");
        CHECK(mem->cart.attached && mem->cart.rom[0x1FFF] == image[0x1FFF] &&
              mem->cart.rom[0x7FFF] == image[0x7FFF % (size > 0x8000 ? 0x8000 : size)],
              "raw image repeats into the 32K function-ROM window");
    }
    image[0x8000] ^= 1;
    write_image(raw_path, image, 0x10000);
    CHECK(cartridge_attach(&mem->cart, raw_path) == CART_ERR_UNSUPPORTED &&
          mem->cart.attached, "genuinely banked 64K image is rejected atomically");

    memset(image, 0, 0x11000);
    memcpy(image, "C128 CARTRIDGE  ", 16);
    be32(image + 0x10, 0x40);
    be16(image + 0x14, 0x0200);
    memcpy(image + 0x20, "Synthetic C128", 14);
    size_t end = add_chip(image, 0x40, 0x8000, 0x2000, 0x5A);
    end = add_chip(image, end, 0xC000, 0x4000, 0xA5);
    write_image(crt_path, image, end);
    CHECK(cartridge_attach(&mem->cart, crt_path) == CART_OK,
          "generic C128 CRT attaches");
    CHECK(mem->cart.rom[0x0000] == 0x5A && mem->cart.rom[0x2000] == 0x5A &&
          mem->cart.rom[0x4000] == 0xA5 && mem->cart.rom[0x7FFF] == 0xA5,
          "8K low CHIP mirrors and 16K high CHIP maps");

    mem->basic[0x4000] = 0x11;
    mem->editor[0] = 0x22;
    mem->kernal[0] = 0x33;
    CHECK(mem_read(mem, 0x8000) == 0x11 && mem_read(mem, 0xC000) == 0x22 &&
          mem_read(mem, 0xE000) == 0x33,
          "default C128 ROM map remains intact with cartridge attached");
    mem->mmu.mcr = 0x09; /* lower external function ROM */
    CHECK(mem_read(mem, 0x8000) == 0x5A && mem_read(mem, 0xA000) == 0x5A &&
          mem_read(mem, 0xC000) == 0x22, "lower MMU selector maps ROML only");
    mem->mmu.mcr = 0x21; /* upper external function ROM, I/O hidden */
    CHECK(mem_read(mem, 0xC000) == 0xA5 && mem_read(mem, 0xD000) == 0xA5 &&
          mem_read(mem, 0xE000) == 0xA5,
          "upper MMU selector maps ROMH across C000-FFFF");
    mem_write(mem, 0xC000, 0x77);
    CHECK(mem_read(mem, 0xC000) == 0xA5 && mem->ram[0xC000] == 0x77,
          "writes pass through the cartridge to RAM beneath");
    cartridge_detach(&mem->cart);
    CHECK(!mem->cart.attached && mem_read(mem, 0xC000) == 0x77,
          "eject removes ROM mapping without clearing RAM");

    memcpy(image, "C64 CARTRIDGE   ", 16);
    write_image(crt_path, image, end);
    CHECK(cartridge_attach(&mem->cart, crt_path) == CART_ERR_UNSUPPORTED &&
          !mem->cart.attached, "C64 CRT is rejected in native C128 mode");
    memcpy(image, "C128 CARTRIDGE  ", 16);
    be16(image + 0x16, 1);
    write_image(crt_path, image, end);
    CHECK(cartridge_attach(&mem->cart, crt_path) == CART_ERR_UNSUPPORTED,
          "special/banked C128 CRT type is rejected");
    be16(image + 0x16, 0);
    write_image(crt_path, image, end - 1);
    CHECK(cartridge_attach(&mem->cart, crt_path) == CART_ERR_FORMAT,
          "truncated CHIP packet is rejected");

    unlink(crt_path);
    unlink(raw_path);
    rmdir(dir);
    free(mem);
    free(image);
    if (failures) { printf("test-cartridge: %d failure(s)\n", failures); return 1; }
    puts("test-cartridge: OK");
    return 0;
}
