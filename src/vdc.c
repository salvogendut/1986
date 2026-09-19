#include "vdc.h"
#include <string.h>

void vdc_init(Vdc *v) {
    memset(v, 0, sizeof(*v));
    vdc_reset(v);
}

void vdc_reset(Vdc *v) {
    memset(v->regs, 0, sizeof(v->regs));
    v->reg = 0;
    v->addr = 0;
}

void vdc_write_index(Vdc *v, u8 val) {
    v->reg = val & 0x3F;
}

void vdc_write_data(Vdc *v, u8 val) {
    v->regs[v->reg] = val;
    /* Word-address register pair: register 18 = low, 19 = high. */
    if (v->reg == 18) v->addr = (u16)((v->addr & 0xFF00) | val);
    if (v->reg == 19) v->addr = (u16)((v->addr & 0x00FF) | (val << 8));
}

u8 vdc_read_data(Vdc *v) {
    return v->regs[v->reg];
}
