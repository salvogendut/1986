#pragma once
#include "drive_cpu6502.h"
#include "cia.h"
#include "wd1770.h"

/* Standalone 2 MHz 1581 board. Not yet selected by the application backend.
 * CIA timer/IRQ/serial functionality is shared with the host; the 8520's
 * binary TOD counter and IEC burst-serial wiring remain future work. */
enum { DRIVE1581_CLOCK_HZ = 2000000 };
typedef void (*Drive1581PortHook)(void *ctx, u8 port_b);
typedef struct {
    u8 ram[0x2000], rom[0x8000];
    bool rom_loaded;
    DriveCpu6502 cpu;
    Cia cia;
    Wd1770 fdc;
    unsigned unit;
    int clock_debt;
    bool led, atn_high, clock_high, data_high;
    Drive1581PortHook port_hook;
    void *port_ctx;
} Drive1581;

void drive1581_init(Drive1581 *drive);
bool drive1581_load_rom(Drive1581 *drive, const char *path);
void drive1581_reset(Drive1581 *drive);
void drive1581_power_cycle(Drive1581 *drive);
u8 drive1581_read(Drive1581 *drive, u16 addr);
void drive1581_write(Drive1581 *drive, u16 addr, u8 value);
int drive1581_step(Drive1581 *drive);
/* Budget is in 2 MHz drive cycles, not host Phi2 or CPU cycles. */
int drive1581_advance(Drive1581 *drive, int cycles);
bool drive1581_set_unit(Drive1581 *drive, unsigned unit);
void drive1581_set_port_hook(Drive1581 *drive, Drive1581PortHook hook, void *ctx);
/* Bus-adapter interface: ATN also feeds CIA FLAG; DATA/CLOCK are inverted at
 * PB0/PB2. Returned output pins include the CIA direction-register pullups. */
void drive1581_set_iec(Drive1581 *drive, bool atn, bool clock, bool data);
u8 drive1581_port_b(const Drive1581 *drive);
bool drive1581_clock_released(const Drive1581 *drive);
bool drive1581_data_released(const Drive1581 *drive);
