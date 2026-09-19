/* clkguard.h - minimal VICE clkguard shim (self-contained). GPLv2+; see Development.md. */
#ifndef VICE_CLKGUARD_H
#define VICE_CLKGUARD_H
struct clk_guard_s;
typedef struct clk_guard_s clk_guard_t;
clk_guard_t *clk_guard_new(void);
void clk_guard_destroy(clk_guard_t *g);
#endif
