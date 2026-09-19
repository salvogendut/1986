/* machine.h - machine stub shim (self-contained). GPLv2+; see Development.md. */
#ifndef VICE_MACHINE_H
#define VICE_MACHINE_H
#include "types.h"
enum { MACHINE_RESET_MODE_SOFT, MACHINE_RESET_MODE_HARD };
enum { JAM_RESET, JAM_HARD_RESET, JAM_MONITOR };
#define MACHINE_SYNC_PAL 1
extern int machine_jam(const char *fmt, ...);
extern void machine_trigger_reset(int mode);
extern void machine_reset(void);
extern void machine_autostart(void);
#endif
