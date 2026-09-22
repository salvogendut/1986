/*
 * maincpu.h - main CPU interface (minimal, self-contained).
 *
 * Part of 1986. The VICE-derived core expects a small set of globals and
 * functions from the host "main CPU" module. This header declares them; they
 * are defined by src/cpu.c. GPLv2+; see Development.md.
 */

#ifndef VICE_MAINCPU_H
#define VICE_MAINCPU_H

#include "types.h"
#include "interrupt.h"
#include "mos6510.h"

extern unsigned int last_opcode_info;
extern unsigned int last_opcode_addr;

#define OPINFO_NUMBER_MSK  0xff
#define OPINFO_NUMBER(opinfo)  ((opinfo) & OPINFO_NUMBER_MSK)

extern unsigned int reg_pc;
extern struct mos6510_regs_s maincpu_regs;
extern int maincpu_rmw_flag;
extern CLOCK maincpu_clk;
extern CLOCK maincpu_clk_limit;
extern int maincpu_stretch;

struct alarm_context_s;
struct clk_guard_s;
struct monitor_interface_s;

extern const CLOCK maincpu_opcode_write_cycles[];
extern struct alarm_context_s *maincpu_alarm_context;
extern struct clk_guard_s *maincpu_clk_guard;
extern struct monitor_interface_s *maincpu_monitor_interface;

void maincpu_resync_limits(void);
void maincpu_reset(void);
void maincpu_mainloop(void);

void maincpu_set_pc(int pc);
void maincpu_set_a(int a);
void maincpu_set_x(int x);
void maincpu_set_y(int y);
void maincpu_set_sign(int n);
void maincpu_set_zero(int z);
void maincpu_set_carry(int c);
void maincpu_set_interrupt(int i);
unsigned int maincpu_get_pc(void);
unsigned int maincpu_get_a(void);
unsigned int maincpu_get_x(void);

#endif
