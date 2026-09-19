#include "mmu.h"

void mmu_init(Mmu *mmu) {
    mmu_reset(mmu);
}

void mmu_reset(Mmu *mmu) {
    mmu->mcr = 0x01;      /* ROM in $8000-$BFFF and $C000-$FFFF, RAM elsewhere */
    mmu->prefig = 0x00;
    mmu->ram_bank = 0x00;
    mmu->rom_bank = 0x00;
    mmu->mode = 0x00;     /* 1 MHz, 8502 active */
    mmu->vdc_bank = 0x00;
    mmu->vdc_ctrl = 0x00;
    mmu->mmio = true;
}

void mmu_write(Mmu *mmu, u16 addr, u8 val) {
    switch (addr & 0xFF) {
        case 0x00: mmu->mcr = val; break;
        case 0x01: mmu->prefig = val; break;
        case 0x06: mmu->ram_bank = val & 0x0F; mmu->rom_bank = (val >> 4) & 0x0F; break;
        case 0x07: mmu->mode = val; break;
        case 0x0D: mmu->vdc_bank = val & 0x03; break;
        case 0x0E: mmu->vdc_ctrl = val; break;
        default: break;
    }
}

u8 mmu_read(const Mmu *mmu, u16 addr) {
    switch (addr & 0xFF) {
        case 0x00: return mmu->mcr;
        case 0x01: return mmu->prefig;
        case 0x06: return (u8)((mmu->rom_bank << 4) | mmu->ram_bank);
        case 0x07: return mmu->mode;
        case 0x0D: return mmu->vdc_bank;
        case 0x0E: return mmu->vdc_ctrl;
        default:   return 0xFF;
    }
}
