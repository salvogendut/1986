#include "drive1571cr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
    failures++; \
} } while (0)

typedef struct {
    Drive1571CrIo chip;
    u16 reg;
    u8 value;
    int reads, writes;
} IoProbe;

static u8 io_read(void *ctx, Drive1571CrIo chip, u16 reg) {
    IoProbe *probe = ctx;
    probe->chip = chip;
    probe->reg = reg;
    probe->reads++;
    return probe->value;
}

static void io_write(void *ctx, Drive1571CrIo chip, u16 reg, u8 value) {
    IoProbe *probe = ctx;
    probe->chip = chip;
    probe->reg = reg;
    probe->value = value;
    probe->writes++;
}

static void install_program(Drive1571Cr *drive, const u8 *program, size_t size) {
    memset(drive->rom, 0xea, sizeof(drive->rom));
    memcpy(drive->rom, program, size);
    drive->rom[0x7ffc] = 0x00; drive->rom[0x7ffd] = 0x80;
    drive->rom[0x7ffe] = 0x00; drive->rom[0x7fff] = 0x90;
    drive->rom[0x7ffa] = 0x00; drive->rom[0x7ffb] = 0xa0;
    drive->rom_loaded = true;
    drive1571cr_reset(drive);
}

int main(void) {
    Drive1571Cr drive;
    drive1571cr_init(&drive);
    CHECK(drive1571cr_read(&drive, 0x8000) == 0xff,
          "unloaded ROM reads as open bus");
    CHECK(drive1571cr_step(&drive) == 0,
          "CPU cannot run without a DOS ROM");

    drive1571cr_write(&drive, 0x0002, 0x5a);
    CHECK(drive1571cr_read(&drive, 0x0802) == 0x5a,
          "2K RAM mirrors through $0fff");
    drive1571cr_write(&drive, 0x0fff, 0xa5);
    CHECK(drive1571cr_read(&drive, 0x07ff) == 0xa5,
          "top of mirrored RAM aliases physical $07ff");
    IoProbe probe = {0};
    drive1571cr_set_io(&drive, io_read, io_write, &probe);
    drive1571cr_write(&drive, 0x180f, 0x12);
    CHECK(probe.chip == DRIVE1571CR_VIA1 && probe.reg == 0x180f && probe.value == 0x12,
          "VIA1 decoded at $1800 mirror");
    drive1571cr_write(&drive, 0x1c02, 0x34);
    CHECK(probe.chip == DRIVE1571CR_VIA2 && probe.reg == 0x1c02 && probe.value == 0x34,
          "VIA2 decoded at $1c00 mirror");
    drive1571cr_write(&drive, 0x2f07, 0x56);
    CHECK(probe.chip == DRIVE1571CR_FDC && probe.reg == 0x2f07 && probe.value == 0x56,
          "WD1770 region decoded at $2000-$2fff");
    drive1571cr_write(&drive, 0x4010, 0x78);
    CHECK(probe.chip == DRIVE1571CR_MOS5710 && probe.reg == 0x4010 &&
          drive1571cr_read(&drive, 0x4010) == 0x78,
          "MOS5710/FDC2 register window decoded");
    drive1571cr_write(&drive, 0x8000, 0x99);
    CHECK(drive1571cr_read(&drive, 0x3000) == 0xff,
          "unmapped address remains open bus");

    static const u8 program[] = {
        0xa2, 0x02,       /* LDX #2 */
        0xa9, 0x15,       /* LDA #$15 */
        0x85, 0x10,       /* STA $10 */
        0x20, 0x10, 0x80, /* JSR $8010 */
        0xca,             /* DEX */
        0xd0, 0xfa,       /* BNE $8006 */
        0x00,             /* BRK */
        0xea, 0xea, 0xea,
        0xe6, 0x10,       /* $8010: INC $10 */
        0x60              /* RTS */
    };
    install_program(&drive, program, sizeof(program));
    CHECK(drive.cpu.pc == 0x8000 && drive.cpu.sp == 0xfd,
          "reset vector and stack state come from drive ROM");
    drive1571cr_run(&drive, 55);
    CHECK(drive.ram[0x10] == 0x17 && drive.cpu.pc == 0x9000,
          "ROM code executes JSR, stack, INC, branch and BRK/IRQ vector");
    CHECK(drive.cpu.cycles >= 55 && !drive.cpu.jammed,
          "drive CPU tracks elapsed cycles without jamming");

    static const u8 arithmetic[] = {
        0xf8,             /* SED */
        0x18,             /* CLC */
        0xa9, 0x45,       /* LDA #$45 */
        0x69, 0x55,       /* ADC #$55 -> $00, carry */
        0x85, 0x20,       /* STA $20 */
        0xd8,             /* CLD */
        0x38,             /* SEC */
        0xe9, 0x01,       /* SBC #1 -> $ff */
        0x85, 0x21        /* STA $21 */
    };
    install_program(&drive, arithmetic, sizeof(arithmetic));
    for (int i = 0; i < 10; i++) CHECK(drive1571cr_step(&drive) > 0,
                                       "arithmetic instruction is legal");
    CHECK(drive.ram[0x20] == 0x00 && drive.ram[0x21] == 0xff,
          "decimal ADC and binary SBC execute independently of host CPU");

    /* Every documented NMOS 6502 opcode must be accepted by the drive core.
     * The actual DOS ROM is user-supplied, so CI cannot depend on it. */
    static const u8 legal[] = {
        0x00,0x01,0x05,0x06,0x08,0x09,0x0a,0x0d,0x0e,
        0x10,0x11,0x15,0x16,0x18,0x19,0x1d,0x1e,
        0x20,0x21,0x24,0x25,0x26,0x28,0x29,0x2a,0x2c,0x2d,0x2e,
        0x30,0x31,0x35,0x36,0x38,0x39,0x3d,0x3e,
        0x40,0x41,0x45,0x46,0x48,0x49,0x4a,0x4c,0x4d,0x4e,
        0x50,0x51,0x55,0x56,0x58,0x59,0x5d,0x5e,
        0x60,0x61,0x65,0x66,0x68,0x69,0x6a,0x6c,0x6d,0x6e,
        0x70,0x71,0x75,0x76,0x78,0x79,0x7d,0x7e,
        0x81,0x84,0x85,0x86,0x88,0x8a,0x8c,0x8d,0x8e,
        0x90,0x91,0x94,0x95,0x96,0x98,0x99,0x9a,0x9d,
        0xa0,0xa1,0xa2,0xa4,0xa5,0xa6,0xa8,0xa9,0xaa,0xac,0xad,0xae,
        0xb0,0xb1,0xb4,0xb5,0xb6,0xb8,0xb9,0xba,0xbc,0xbd,0xbe,
        0xc0,0xc1,0xc4,0xc5,0xc6,0xc8,0xc9,0xca,0xcc,0xcd,0xce,
        0xd0,0xd1,0xd5,0xd6,0xd8,0xd9,0xdd,0xde,
        0xe0,0xe1,0xe4,0xe5,0xe6,0xe8,0xe9,0xea,0xec,0xed,0xee,
        0xf0,0xf1,0xf5,0xf6,0xf8,0xf9,0xfd,0xfe
    };
    for (size_t i = 0; i < sizeof(legal); ++i) {
        install_program(&drive, &legal[i], 1);
        int cycles = drive1571cr_step(&drive);
        if (cycles <= 0 || drive.cpu.jammed) {
            fprintf(stderr, "FAIL documented opcode $%02x jammed\n", legal[i]);
            failures++;
        }
    }
    install_program(&drive, (const u8[]){0x02}, 1);
    CHECK(drive1571cr_step(&drive) == 0 && drive.cpu.jammed,
          "unsupported opcode stops rather than masquerading as NOP");

    const char *user_rom = getenv("C128_TEST_1571_ROM");
    if (user_rom && *user_rom) {
        CHECK(drive1571cr_load_rom(&drive, user_rom),
              "user-supplied 1571CR DOS ROM loads at exact 32K size");
        drive1571cr_reset(&drive);
        CHECK(drive.cpu.pc == (u16)(drive.rom[0x7ffc] |
                                   ((u16)drive.rom[0x7ffd] << 8)),
              "real DOS ROM reset vector is used");
        CHECK(drive1571cr_step(&drive) > 0,
              "real DOS ROM begins executing in the independent drive CPU");
    }

    if (!failures) puts("test-drive1571cr: OK");
    return failures ? 1 : 0;
}
