#pragma once
#include "types.h"

/* Commodore 1700/1764/1750 RAM Expansion Controller. Register semantics,
 * autoload quirks and missing-DRAM behavior follow VICE 3.10 c64/cart/reu.c.
 * DMA is advanced one shared 1 MHz bus cycle at a time by the machine. */
#define REU_MAX_RAM (512u * 1024u)
#define REU_REG_COUNT 11

typedef struct {
    unsigned size_kb;             /* 0 (detached), 128, 256 or 512 */
    u8 regs[REU_REG_COUNT];
    u8 shadow[7];                 /* programmed registers 2..8 (autoload) */
    bool armed, active;
    u16 host_address;
    u32 reu_address, remaining;   /* length zero means 65536 bytes */
    u8 phase;                    /* 0=start, 1=transfer, 2=swap write, 3=verify tail */
    u8 host_latch, reu_latch, floating_bus;
    u8 ram[REU_MAX_RAM];
} Reu;

typedef struct {
    void *ctx;
    u8 (*read)(void *ctx, u16 address);
    void (*write)(void *ctx, u16 address, u8 value);
} ReuBus;

bool reu_valid_size(unsigned kb);
void reu_configure(Reu *r, unsigned kb); /* size change clears RAM */
void reu_reset(Reu *r);                 /* register reset; RAM survives */
void reu_power_cycle(Reu *r);           /* registers and volatile RAM */
u8 reu_read(Reu *r, u16 address);
u8 reu_peek(const Reu *r, u16 address);
void reu_write(Reu *r, u16 address, u8 value);
void reu_ff00_trigger(Reu *r);
void reu_tick(Reu *r, const ReuBus *bus);
bool reu_irq(const Reu *r);
