#include "mmu.h"
void mmu_init(Mmu *mmu) {
    mmu_reset(mmu);
}

void mmu_reset(Mmu *mmu) {
    mmu->mcr = 0x01;      /* ROM in $8000-$BFFF and $C000-$FFFF, RAM elsewhere */
    mmu->prefig = 0x00;
    mmu->pcr2 = 0x00;
    mmu->pcr3 = 0x00;
    mmu->pcr4 = 0x00;
    mmu->rcr = 0x00;
    mmu->mode = 0x00;     /* 1 MHz, 8502 active */
    mmu->vdc_bank = 0x00;
    mmu->vdc_ctrl = 0x00;
    mmu->mcr5 = 0x00;
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
            /* Bit 6 changes the real C128 into its C64 personality. 1986 is
             * intentionally native-C128-only, so latch the request for the
             * host UI but never let that mode bit enter the emulated state. */
            if (val & 0x40) {
                if (!mmu->c64_request_active)
                    mmu->c64_request_pending = true;
                mmu->c64_request_active = true;
            } else {
                mmu->c64_request_active = false;
            }
            mmu->mcr5 = (val & 0x3F) | 0x30;
            break;
        case 0x06: mmu->rcr = val; break;
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
        case 0x02: return mmu->pcr2;
        case 0x03: return mmu->pcr3;
        case 0x04: return mmu->pcr4;
        case 0x05: /* MCR: bit 7 = 40/80 key, bits 4-5 = GAME/EXROM, low nibble = mode */
            return (u8)((mmu->mcr5 & 0x0F) | (mmu->col4080 ? 0x80 : 0) | 0x10 | 0x20);
        case 0x06: return mmu->rcr;
        case 0x07: return mmu->mode;
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
