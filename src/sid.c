#include "sid.h"
#include <string.h>

void sid_init(Sid *s) {
    memset(s, 0, sizeof(*s));
    sid_reset(s);
}

void sid_reset(Sid *s) {
    memset(s->regs, 0, sizeof(s->regs));
}

void sid_write(Sid *s, u16 addr, u8 val) {
    u16 i = addr - 0xD400;
    if (i < sizeof(s->regs)) s->regs[i] = val;
}

u8 sid_read(Sid *s, u16 addr) {
    u16 i = addr - 0xD400;
    if (i < sizeof(s->regs)) return s->regs[i];
    return 0xFF;
}
