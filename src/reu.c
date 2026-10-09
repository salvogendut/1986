#include "reu.h"
#include <string.h>

enum { STATUS = 0, COMMAND, HOST_LO, HOST_HI, REU_LO, REU_HI, BANK,
       LENGTH_LO, LENGTH_HI, IRQ_MASK, ADDR_CONTROL };
enum { VERIFY_ERROR = 0x20, END_OF_BLOCK = 0x40, IRQ_PENDING = 0x80 };

bool reu_valid_size(unsigned kb) {
    return kb == 0 || kb == 128 || kb == 256 || kb == 512;
}

void reu_reset(Reu *r) {
    /* Retain fitted DRAM and its contents on the RESET line. */
    memset(r->regs, 0, sizeof(r->regs));
    memset(r->shadow, 0, sizeof(r->shadow));
    r->regs[STATUS] = r->size_kb >= 256 ? 0x10 : 0;
    r->regs[COMMAND] = 0x10;
    r->regs[LENGTH_LO] = r->regs[LENGTH_HI] = 0xff;
    r->shadow[5] = r->shadow[6] = 0xff;
    r->regs[IRQ_MASK] = 0x1f;
    r->regs[ADDR_CONTROL] = 0x3f;
    r->armed = r->active = false;
    r->host_address = 0;
    r->reu_address = r->remaining = 0;
    r->phase = r->host_latch = r->reu_latch = 0;
    r->floating_bus = 0xff;
}

void reu_power_cycle(Reu *r) {
    /* Deterministic power-on RAM, not a battery-backed image. */
    memset(r->ram, 0, sizeof(r->ram));
    reu_reset(r);
}

void reu_configure(Reu *r, unsigned kb) {
    if (!reu_valid_size(kb) || r->size_kb == kb) return;
    r->size_kb = kb;
    reu_power_cycle(r);
}

bool reu_irq(const Reu *r) {
    return r->size_kb && (r->regs[STATUS] & IRQ_PENDING);
}

u8 reu_peek(const Reu *r, u16 address) {
    unsigned reg = address & 0x1f;
    if (!r->size_kb || reg >= REU_REG_COUNT) return 0xff;
    return r->regs[reg] | (reg == BANK ? 0xf8 : 0);
}

u8 reu_read(Reu *r, u16 address) {
    /* During DMA the REC disconnects its own IO2 register interface. */
    if (r->active) return 0xff;
    u8 value = reu_peek(r, address);
    if (r->size_kb && (address & 0x1f) == STATUS) r->regs[STATUS] &= 0x1f;
    return value;
}

static void latch_irq(Reu *r, u8 events) {
    if ((r->regs[IRQ_MASK] & 0x80) &&
        (r->regs[IRQ_MASK] & events & (VERIFY_ERROR | END_OF_BLOCK)))
        r->regs[STATUS] |= IRQ_PENDING;
}

static void start(Reu *r) {
    r->armed = false;
    r->active = true;
    r->host_address = r->regs[HOST_LO] | (r->regs[HOST_HI] << 8);
    r->reu_address = r->regs[REU_LO] | (r->regs[REU_HI] << 8) |
                     ((u32)(r->regs[BANK] & 7) << 16);
    r->remaining = r->regs[LENGTH_LO] | (r->regs[LENGTH_HI] << 8);
    if (!r->remaining) r->remaining = 65536;
    r->phase = 0;
}

void reu_write(Reu *r, u16 address, u8 value) {
    unsigned reg = address & 0x1f;
    if (!r->size_kb || r->active || reg == STATUS || reg >= REU_REG_COUNT) return;
    if (reg >= HOST_LO && reg <= LENGTH_HI) {
        if (reg == BANK) value &= 7;
        r->regs[reg] = r->shadow[reg - HOST_LO] = value;
        /* Writing either byte reloads its partner from the shadow too.
         * This is the REC half-autoload behavior modeled by VICE. */
        if (reg != BANK) {
            unsigned partner = reg == LENGTH_LO ? LENGTH_HI :
                               reg == LENGTH_HI ? LENGTH_LO : reg ^ 1u;
            r->regs[partner] = r->shadow[partner - HOST_LO];
        }
    } else {
        if (reg == IRQ_MASK) value |= 0x1f;
        if (reg == ADDR_CONTROL) value |= 0x3f;
        r->regs[reg] = value;
        if (reg == IRQ_MASK) latch_irq(r, r->regs[STATUS]);
        if (reg == COMMAND && (value & 0x80)) {
            if (value & 0x10) start(r);
            else r->armed = true;
        }
    }
}

void reu_ff00_trigger(Reu *r) {
    if (r->size_kb && r->armed && !r->active) start(r);
}

static u32 dram_address(const Reu *r) {
    return r->reu_address & (r->size_kb == 128 ? 0x1ffffu : 0x7ffffu);
}

static u8 ram_read(const Reu *r) {
    u32 address = dram_address(r);
    /* The 1764 has unpopulated banks, not a 256K mirror. */
    return address < r->size_kb * 1024u ? r->ram[address] : r->floating_bus;
}

static void ram_write(Reu *r, u8 value) {
    u32 address = dram_address(r);
    if (address < r->size_kb * 1024u) r->ram[address] = value;
}

static void advance(Reu *r) {
    if (!(r->regs[ADDR_CONTROL] & 0x80)) ++r->host_address;
    if (!(r->regs[ADDR_CONTROL] & 0x40)) {
        r->reu_address = (r->reu_address + 1u) & 0x7ffffu;
        if (r->size_kb == 128 && r->reu_address == 0x20000u)
            r->reu_address = 0;
    }
    --r->remaining;
}

static void finish(Reu *r, u8 events) {
    r->regs[STATUS] |= events;
    if (r->regs[COMMAND] & 0x20) {
        memcpy(r->regs + HOST_LO, r->shadow, sizeof(r->shadow));
    } else {
        r->regs[HOST_LO] = (u8)r->host_address;
        r->regs[HOST_HI] = (u8)(r->host_address >> 8);
        r->regs[REU_LO] = (u8)r->reu_address;
        r->regs[REU_HI] = (u8)(r->reu_address >> 8);
        r->regs[BANK] = (u8)(r->reu_address >> 16);
        /* A completed REC transfer leaves length=1, not zero. */
        unsigned length = r->remaining ? r->remaining : 1;
        r->regs[LENGTH_LO] = (u8)length;
        r->regs[LENGTH_HI] = (u8)(length >> 8);
    }
    latch_irq(r, events);
    r->regs[COMMAND] = (r->regs[COMMAND] & 0x7f) | 0x10;
    r->active = false;
    r->phase = 0;
}

void reu_tick(Reu *r, const ReuBus *bus) {
    if (!r->active) return;
    if (r->phase == 0) { r->phase = 1; return; } /* bus takeover */
    if (r->phase == 3) {
        /* Failed compares have an extra cycle except on the final byte.
         * A penultimate mismatch also sets EOB if the final byte matches. */
        u8 events = VERIFY_ERROR;
        if (r->remaining == 1 && ram_read(r) == bus->read(bus->ctx, r->host_address))
            events |= END_OF_BLOCK;
        finish(r, events);
        return;
    }
    unsigned mode = r->regs[COMMAND] & 3;
    if (mode == 0) {
        r->host_latch = bus->read(bus->ctx, r->host_address);
        ram_write(r, r->host_latch);
    } else if (mode == 1) {
        r->floating_bus = ram_read(r);
        bus->write(bus->ctx, r->host_address, r->floating_bus);
    } else if (mode == 2) {
        if (r->phase == 1) {
            r->reu_latch = ram_read(r);
            r->host_latch = bus->read(bus->ctx, r->host_address);
            r->phase = 2;
            return;
        }
        ram_write(r, r->host_latch);
        bus->write(bus->ctx, r->host_address, r->reu_latch);
        r->phase = 1;
    } else {
        bool match = ram_read(r) == bus->read(bus->ctx, r->host_address);
        advance(r);
        if (!match) {
            if (r->remaining) r->phase = 3;
            else finish(r, VERIFY_ERROR | END_OF_BLOCK);
            return;
        }
        if (!r->remaining) finish(r, END_OF_BLOCK);
        return;
    }
    advance(r);
    if (!r->remaining) {
        if (mode == 0) r->floating_bus = r->host_latch;
        if (mode == 1) r->floating_bus = ram_read(r); /* final prefetch */
        finish(r, END_OF_BLOCK);
    }
}
