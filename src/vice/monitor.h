/* monitor.h - minimal VICE monitor shim (self-contained). GPLv2+; see Development.md. */
#ifndef VICE_MONITOR_H
#define VICE_MONITOR_H
#include "types.h"

enum { e_comp_space = 0 };

/* Monitor interrupt-mask bits (used by the CPU core's DO_INTERRUPT). */
#define MI_STEP    (1 << 0)
#define MI_BREAK   (1 << 1)
#define MI_WATCH   (1 << 2)

struct monitor_interface_s;
typedef struct monitor_interface_s monitor_interface_t;

extern int monitor_mask[];
extern monitor_interface_t *maincpu_monitor_interface_get(void);
extern void monitor_startup(int space);
extern int monitor_force_import(int space);
extern void monitor_check_icount(unsigned int pc);
extern void monitor_check_icount_interrupt(void);
extern int monitor_check_breakpoints(int space, unsigned int pc);
extern void monitor_check_watchpoints(unsigned int last_addr, unsigned int pc);
#endif
