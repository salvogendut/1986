#include "mmu.h"
void mmu_init(Mmu *mmu) {
    mmu->c64_enabled = false;
    mmu_reset(mmu);
}

void mmu_reset(Mmu *mmu) {
    mmu->mcr = 0x01;      /* ROM in $8000-$BFFF and $C000-$FFFF, RAM elsewhere */
    mmu->prefig = 0x00;
    mmu->pcr2 = 0x00;
    mmu->pcr3 = 0x00;
    mmu->pcr4 = 0x00;
    mmu->rcr = 0x00;
    mmu->page0 = 0x00;
    mmu->page0_bank = 0x00;
    mmu->page0_bank_latch = 0x00;
    mmu->page1 = 0x01;
    mmu->page1_bank = 0x00;
    mmu->page1_bank_latch = 0x00;
    mmu->vdc_bank = 0x00;
    mmu->vdc_ctrl = 0x00;
    mmu->mcr5 = 0x00;
    mmu->c64_mode = false;
    mmu->c64_ram_bank = 0;
    mmu->c64_request_pending = false;
    mmu->c64_request_active = false;
    mmu->col4080 = true;  /* 40-column key released -> 40-col VIC */
    mmu->mmio = true;
}

void mmu_write(Mmu *mmu, u16 addr, u8 val) {
    switch (addr & 0xFF) {
        case 0x00: mmu->mcr = val; break;
        case 0x01: mmu->prefig = val; break;
        case 0x02: mmu->pcr2 = val; break;
        case 0x03: mmu->pcr3 = val; break;
        case 0x04: mmu->pcr4 = val; break;
        case 0x05:
            /* VICE's x128 changes the memory personality immediately when
             * MCR bit 6 rises. Keep that authentic path behind the explicit
             * Advanced test gate; otherwise retain the historical rejection. */
            if (val & 0x40) {
                if (mmu->c64_enabled) {
                    if (!mmu->c64_mode)
                        mmu->c64_ram_bank = (mmu->mcr >> 6) & 1;
                    mmu->c64_mode = true;
                } else if (!mmu->c64_request_active) {
                    mmu->c64_request_pending = true;
                }
                mmu->c64_request_active = true;
            } else {
                mmu->c64_request_active = false;
                mmu->c64_mode = false;
            }
            mmu->mcr5 = (val & 0x3F) | 0x30 |
                        (mmu->c64_mode ? 0x40 : 0);
            break;
        case 0x06: mmu->rcr = val; break;
        case 0x07:
            mmu->page0 = val;
            mmu->page0_bank = mmu->page0_bank_latch & 1;
            break;
        case 0x08: mmu->page0_bank_latch = val; break;
        case 0x09:
            mmu->page1 = val;
            mmu->page1_bank = mmu->page1_bank_latch & 1;
            break;
        case 0x0A: mmu->page1_bank_latch = val; break;
        case 0x0D: mmu->vdc_bank = val & 0x03; break;
        case 0x0E: mmu->vdc_ctrl = val; break;
        default: break;
    }
}

u8 mmu_read(const Mmu *mmu, u16 addr) {
    switch (addr & 0xFF) {
        case 0x00: return mmu->mcr;
        case 0x01: return mmu->prefig;
        case 0x02: return mmu->pcr2;
        case 0x03: return mmu->pcr3;
        case 0x04: return mmu->pcr4;
        case 0x05: /* MCR: bit 7 = 40/80 key, bits 4-5 = GAME/EXROM, low nibble = mode */
            return (u8)((mmu->mcr5 & 0x0F) | (mmu->col4080 ? 0x80 : 0) | 0x10 | 0x20);
        case 0x06: return mmu->rcr;
        case 0x07: return mmu->page0;
        case 0x08: return mmu->page0_bank | 0xF0;
        case 0x09: return mmu->page1;
        case 0x0A: return mmu->page1_bank | 0xF0;
        case 0x0D: return mmu->vdc_bank;
        case 0x0E: return mmu->vdc_ctrl;
        default:   return 0xFF;
    }
}

/* $FF00-$FF04 mirror of $D500-$D504.  Writing $FF01-$FF04 commits the
 * preconfiguration register to the configuration register (VICE's
 * mmu_ffxx_store). */
u8 mmu_ffxx_read(const Mmu *mmu, u16 addr) {
    return mmu_read(mmu, addr);
}

void mmu_ffxx_write(Mmu *mmu, u16 addr, u8 val) {
    if (addr == 0xFF00) {
        mmu->mcr = val;
    } else {
        /* Commit the preconfiguration register to the CR. */
        switch (addr & 0xFF) {
            case 0x01: mmu->mcr = mmu->prefig; break;
            case 0x02: mmu->mcr = mmu->pcr2;   break;
            case 0x03: mmu->mcr = mmu->pcr3;   break;
            case 0x04: mmu->mcr = mmu->pcr4;   break;
            default:   break;
        }
    }
}

bool mmu_take_c64_request(Mmu *mmu) {
    bool pending = mmu->c64_request_pending;
    mmu->c64_request_pending = false;
    return pending;
}

void mmu_set_c64_enabled(Mmu *mmu, bool enabled) {
    mmu->c64_enabled = enabled;
    if (!enabled) {
        mmu->c64_mode = false;
        mmu->mcr5 &= (u8)~0x40;
    }
}

bool mmu_is_c64_mode(const Mmu *mmu) {
    return mmu->c64_mode;
}

bool mmu_cpu_is_8502(const Mmu *mmu) {
    return (mmu->mcr5 & 0x01) != 0;
}
