#pragma once
#include "types.h"

/*
 * C128 MMU (based on the 8722 + the 8502's $00/$01 port).
 *
 * The MMU maps a 16-bit address space onto a set of RAM banks and ROMs. For
 * the scaffolding it exposes the handful of configuration registers the rest
 * of the machine reads; the real banking logic (bank 0..15, ROM selection,
 * 2 MHz mode, VDC access) will live here as the port matures.
 *
 * Register map (as seen by the 8502):
 *   $D500   — memory configuration register (MCR)
 *   $D501   — pre-configuration register
 *   $D506   — RAM configuration register (VIC bank and common-RAM layout)
 *   $D507/8 — relocated zero-page address (page number / bank latch)
 *   $D509/A — relocated stack-page address (page number / bank latch)
 *   $D50D   — VDC bank (bits 1..0 select 16K VDC bank)
 *   $D50E   — VDC access control (bit 6: IRQ mask, bit 7: register select)
 */
typedef struct {
    u8  mcr;      /* memory configuration register */
    u8  prefig;   /* pre-configuration register */
    u8  pcr2;     /* preconfiguration register $D502 */
    u8  pcr3;     /* preconfiguration register $D503 */
    u8  pcr4;     /* preconfiguration register $D504 */
    u8  rcr;      /* $D506 RAM configuration register */
    u8  page0;    /* $D507, physical page for CPU $0000-$00FF */
    u8  page0_bank; /* $D508, committed by a write to $D507 */
    u8  page0_bank_latch;
    u8  page1;    /* $D509, physical page for CPU $0100-$01FF */
    u8  page1_bank; /* $D50A, committed by a write to $D509 */
    u8  page1_bank_latch;
    u8  vdc_bank; /* $D50D */
    u8  vdc_ctrl; /* $D50E */
    u8  mcr5;     /* $D505 mode configuration register (low nibble) */
    bool c64_request_pending; /* rejected C64-mode transition to report */
    bool c64_request_active;  /* suppress duplicate reports for one request */
    bool col4080; /* 40/80 column key: true = 40-col (default) */
    bool mmio;    /* true when $D500 block is mapped in */
} Mmu;

void mmu_init(Mmu *mmu);
void mmu_reset(Mmu *mmu);
void mmu_write(Mmu *mmu, u16 addr, u8 val);
u8   mmu_read(const Mmu *mmu, u16 addr);
u8   mmu_ffxx_read(const Mmu *mmu, u16 addr);         /* $FF00-$FF04 mirror */
void mmu_ffxx_write(Mmu *mmu, u16 addr, u8 val);
/* Consume one rejected $D505 C64-mode request. The emulator deliberately
 * remains in native C128 mode. */
bool mmu_take_c64_request(Mmu *mmu);
