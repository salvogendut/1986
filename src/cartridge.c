#include "cartridge.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Four 8K CHIP packets plus headers, with space for an extended CRT header.
 * Bank-switched images are intentionally rejected rather than truncated. */
#define IMAGE_LIMIT (2 * CARTRIDGE_ROM_SIZE + 0x1000)

static unsigned be16(const u8 *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned be32(const u8 *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | p[3];
}

static bool extension_is(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    while (*dot && *ext) {
        if (tolower((unsigned char)*dot) != tolower((unsigned char)*ext))
            return false;
        dot++;
        ext++;
    }
    return *dot == '\0' && *ext == '\0';
}

void cartridge_detach(Cartridge *cart) {
    memset(cart, 0, sizeof(*cart));
}

static CartridgeResult parse_crt(Cartridge *out, const u8 *image, size_t length) {
    if (length < 0x40) return CART_ERR_FORMAT;
    if (memcmp(image, "C128 CARTRIDGE  ", 16) != 0)
        return CART_ERR_UNSUPPORTED; /* notably C64 CARTRIDGE */
    unsigned header_size = be32(image + 0x10);
    if (header_size < 0x40 || header_size > length || be16(image + 0x14) != 0x0200)
        return CART_ERR_FORMAT;
    if (be16(image + 0x16) != 0)
        return CART_ERR_UNSUPPORTED; /* bank-switched/special C128 cartridge */

    memset(out->rom, 0xFF, sizeof(out->rom));
    bool occupied[4] = { false, false, false, false };
    size_t offset = header_size;
    while (offset < length) {
        if (length - offset < 16 || memcmp(image + offset, "CHIP", 4) != 0)
            return CART_ERR_FORMAT;
        unsigned packet_size = be32(image + offset + 4);
        unsigned chip_type = be16(image + offset + 8);
        unsigned bank = be16(image + offset + 10);
        unsigned address = be16(image + offset + 12);
        unsigned chip_size = be16(image + offset + 14);
        if (packet_size < 16 || packet_size > length - offset ||
            packet_size != chip_size + 16)
            return CART_ERR_FORMAT;
        if (chip_type != 0 || bank != 0)
            return CART_ERR_UNSUPPORTED;
        if ((chip_size != 0x2000 && chip_size != 0x4000) ||
            address < 0x8000 || address > 0xE000 ||
            (address & 0x1FFF) != 0 ||
            (chip_size == 0x4000 && address != 0x8000 && address != 0xC000) ||
            address + chip_size > 0x10000)
            return CART_ERR_UNSUPPORTED;
        unsigned first = (address - 0x8000) / 0x2000;
        unsigned blocks = chip_size / 0x2000;
        for (unsigned i = 0; i < blocks; ++i)
            if (occupied[first + i]) return CART_ERR_FORMAT;
        memcpy(out->rom + address - 0x8000, image + offset + 16, chip_size);
        for (unsigned i = 0; i < blocks; ++i) occupied[first + i] = true;
        offset += packet_size;
    }
    if (!occupied[0] && !occupied[1] && !occupied[2] && !occupied[3])
        return CART_ERR_FORMAT;
    /* A single 8K ROM drives both halves of its 16K function-ROM window. */
    for (int pair = 0; pair < 4; pair += 2) {
        if (occupied[pair] && !occupied[pair + 1])
            memcpy(out->rom + (pair + 1) * 0x2000,
                   out->rom + pair * 0x2000, 0x2000);
        else if (!occupied[pair] && occupied[pair + 1])
            memcpy(out->rom + pair * 0x2000,
                   out->rom + (pair + 1) * 0x2000, 0x2000);
    }
    memcpy(out->name, image + 0x20, 32);
    out->name[32] = '\0';
    out->attached = true;
    return CART_OK;
}

CartridgeResult cartridge_attach(Cartridge *cart, const char *path) {
    if (!path || !path[0]) return CART_ERR_OPEN;
    FILE *file = fopen(path, "rb");
    if (!file) return CART_ERR_OPEN;
    u8 image[IMAGE_LIMIT];
    size_t length = fread(image, 1, sizeof(image), file);
    int extra = fgetc(file);
    bool read_failed = ferror(file) != 0;
    fclose(file);
    if (read_failed) return CART_ERR_OPEN;
    if (extra != EOF) return CART_ERR_SIZE;

    Cartridge next;
    memset(&next, 0, sizeof(next));
    CartridgeResult result;
    if (extension_is(path, ".crt") ||
        (length >= 16 &&
         (memcmp(image, "C128 CARTRIDGE", 14) == 0 ||
          memcmp(image, "C64 CARTRIDGE", 13) == 0))) {
        result = parse_crt(&next, image, length);
    } else if (length == 0x2000 || length == 0x4000 ||
               length == 0x8000 || length == 0x10000) {
        if (length == 0x10000 &&
            memcmp(image, image + CARTRIDGE_ROM_SIZE, CARTRIDGE_ROM_SIZE) != 0)
            return CART_ERR_UNSUPPORTED;
        size_t period = length > CARTRIDGE_ROM_SIZE ? CARTRIDGE_ROM_SIZE : length;
        for (size_t i = 0; i < CARTRIDGE_ROM_SIZE; ++i)
            next.rom[i] = image[i % period];
        snprintf(next.name, sizeof(next.name), "External function ROM");
        next.attached = true;
        result = CART_OK;
    } else {
        result = CART_ERR_SIZE;
    }
    if (result == CART_OK) *cart = next;
    return result;
}

const char *cartridge_result_name(CartridgeResult result) {
    switch (result) {
        case CART_OK: return "OK";
        case CART_ERR_OPEN: return "COULD NOT READ CARTRIDGE IMAGE";
        case CART_ERR_FORMAT: return "MALFORMED CARTRIDGE IMAGE";
        case CART_ERR_UNSUPPORTED: return "UNSUPPORTED CARTRIDGE TYPE";
        case CART_ERR_SIZE: return "UNSUPPORTED CARTRIDGE IMAGE SIZE";
        default: return "CARTRIDGE ERROR";
    }
}
