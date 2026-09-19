/*
 * interrupt.c - Implementation of 6510 interrupts and alarms (trimmed port).
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andre Fachat <fachat@physik.tu-chemnitz.de>
 *  Andreas Boose <viceteam@t-online.de>
 *
 * This file is part of 1986. It is derived from VICE, the Versatile Commodore
 * Emulator (https://vice-emu.sourceforge.io/), GPLv2 or later. See README in
 * the VICE tree for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 * The snapshot and DMA-stealing helpers from the original interrupt.c are
 * omitted; the remaining functions are kept equivalent so the 6510 core and
 * the main-CPU host build self-contained.
 */

#include "interrupt.h"
#include "6510core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void interrupt_cpu_status_init(interrupt_cpu_status_t *cs,
                               unsigned int *last_opcode_info_ptr)
{
    cs->num_ints = 0;
    cs->pending_int = NULL;
    cs->int_name = NULL;
    cs->last_opcode_info_ptr = last_opcode_info_ptr;
}

void interrupt_cpu_status_reset(interrupt_cpu_status_t *cs)
{
    unsigned int num_ints, *pending_int, *last_opcode_info_ptr;
    char **int_name;

    num_ints = cs->num_ints;
    pending_int = cs->pending_int;
    int_name = cs->int_name;
    last_opcode_info_ptr = cs->last_opcode_info_ptr;
    if (num_ints > 0) {
        memset(pending_int, 0, num_ints * sizeof(*(cs->pending_int)));
    }
    memset(cs, 0, sizeof(interrupt_cpu_status_t));
    cs->num_ints = num_ints;
    cs->pending_int = pending_int;
    cs->int_name = int_name;
    cs->last_opcode_info_ptr = last_opcode_info_ptr;

    cs->num_last_stolen_cycles = 0;
    cs->last_stolen_cycles_clk = (CLOCK)0;
    cs->num_dma_per_opcode = 0;
    cs->irq_delay_cycles = 0;
    cs->nmi_delay_cycles = 0;
    cs->global_pending_int = IK_NONE;
    cs->nmi_trap_func = NULL;
    cs->reset_trap_func = NULL;
    cs->irq_pending_clk = CLOCK_MAX;
}

unsigned int interrupt_cpu_status_int_new(interrupt_cpu_status_t *cs,
                                          const char *name)
{
    cs->num_ints += 1;
    cs->pending_int = realloc(cs->pending_int, cs->num_ints * sizeof(*(cs->pending_int)));
    cs->pending_int[cs->num_ints - 1] = 0;
    cs->int_name = realloc(cs->int_name, cs->num_ints * sizeof(char *));
    cs->int_name[cs->num_ints - 1] = strdup(name);
    return cs->num_ints - 1;
}

interrupt_cpu_status_t *interrupt_cpu_status_new(void)
{
    return (interrupt_cpu_status_t *)calloc(1, sizeof(interrupt_cpu_status_t));
}

void interrupt_cpu_status_destroy(interrupt_cpu_status_t *cs)
{
    if (cs != NULL) {
        unsigned int num;
        for (num = 0; num < cs->num_ints; num++) {
            free(cs->int_name[num]);
        }
        free(cs->int_name);
        free(cs->pending_int);
    }
    free(cs);
}

void interrupt_set_nmi_trap_func(interrupt_cpu_status_t *cs,
                                 void (*nmi_trap_func)(void))
{
    cs->nmi_trap_func = nmi_trap_func;
}

void interrupt_set_reset_trap_func(interrupt_cpu_status_t *cs, void (*reset_trap_func)(void))
{
    cs->reset_trap_func = reset_trap_func;
}

void interrupt_cpu_status_time_warp(interrupt_cpu_status_t *cs,
                                    CLOCK warp_amount,
                                    int warp_direction)
{
    if (cs->irq_pending_clk < CLOCK_MAX) {
        if (warp_direction > 0) {
            if (cs->irq_pending_clk > warp_amount) {
                cs->irq_pending_clk -= warp_amount;
            } else {
                cs->irq_pending_clk = 0;
            }
        } else {
            cs->irq_pending_clk += warp_amount;
        }
    }
    if (cs->nmi_clk != 0) {
        if (warp_direction > 0) {
            if (cs->nmi_clk > warp_amount) {
                cs->nmi_clk -= warp_amount;
            } else {
                cs->nmi_clk = 0;
            }
        } else {
            cs->nmi_clk += warp_amount;
        }
    }
    if (cs->irq_clk != 0) {
        if (warp_direction > 0) {
            if (cs->irq_clk > warp_amount) {
                cs->irq_clk -= warp_amount;
            } else {
                cs->irq_clk = 0;
            }
        } else {
            cs->irq_clk += warp_amount;
        }
    }
}

void interrupt_log_wrong_nirq(void)
{
    fprintf(stderr, "[vice] interrupt_set_irq(): wrong nirq!\n");
}

void interrupt_log_wrong_nnmi(void)
{
    fprintf(stderr, "[vice] interrupt_set_nmi(): wrong nnmi!\n");
}

void interrupt_restore_irq(interrupt_cpu_status_t *cs, int int_num, int value)
{
    interrupt_set_irq(cs, (unsigned int)int_num, value, cs->irq_clk);
}

void interrupt_restore_nmi(interrupt_cpu_status_t *cs, int int_num, int value)
{
    interrupt_set_nmi(cs, (unsigned int)int_num, value, cs->nmi_clk);
}

int interrupt_get_irq(interrupt_cpu_status_t *cs, int int_num)
{
    return (cs->pending_int[int_num] & IK_IRQ) ? 1 : 0;
}

int interrupt_get_nmi(interrupt_cpu_status_t *cs, int int_num)
{
    return (cs->pending_int[int_num] & IK_NMI) ? 1 : 0;
}

void interrupt_fixup_int_clk(interrupt_cpu_status_t *cs, CLOCK cpu_clk,
                             CLOCK *int_clk)
{
    unsigned int num_cycles_left = 0, last_num_cycles_left = 0, num_dma;
    unsigned int cycles_left_to_trigger_irq =
        (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr) ? 2 : 1);
    CLOCK last_start_clk = CLOCK_MAX;

    num_dma = cs->num_dma_per_opcode;
    while (num_dma != 0) {
        num_dma--;
        num_cycles_left = cs->num_cycles_left[num_dma];
        if ((cs->dma_start_clk[num_dma] - 1) <= cpu_clk) {
            break;
        }
        last_num_cycles_left = num_cycles_left;
        last_start_clk = cs->dma_start_clk[num_dma];
    }
    if (num_cycles_left - last_num_cycles_left > last_start_clk - cpu_clk - 1) {
        num_cycles_left = last_num_cycles_left + last_start_clk - cpu_clk - 1;
    }

    *int_clk = cs->last_stolen_cycles_clk;
    if (cs->num_dma_per_opcode > 0 && cs->dma_start_clk[0] > cpu_clk) {
        *int_clk -= (cs->dma_start_clk[0] - cpu_clk);
    }
    if (num_cycles_left >= cycles_left_to_trigger_irq) {
        *int_clk -= (cycles_left_to_trigger_irq + 1);
    }
}

void interrupt_trigger_dma(interrupt_cpu_status_t *cs, CLOCK cpu_clk)
{
    (void)cpu_clk;
    cs->global_pending_int = (enum cpu_int)(cs->global_pending_int | IK_DMA);
}

void interrupt_ack_dma(interrupt_cpu_status_t *cs)
{
    cs->global_pending_int = (enum cpu_int)(cs->global_pending_int & ~IK_DMA);
}

void interrupt_trigger_reset(interrupt_cpu_status_t *cs, CLOCK cpu_clk)
{
    (void)cpu_clk;
    if (cs == NULL) {
        return;
    }
    cs->global_pending_int |= IK_RESET;
}

void interrupt_ack_reset(interrupt_cpu_status_t *cs)
{
    cs->global_pending_int &= ~IK_RESET;
    if (cs->reset_trap_func) {
        cs->reset_trap_func();
    }
}

void interrupt_maincpu_trigger_trap(void (*trap_func)(WORD, void *data),
                                    void *data)
{
    interrupt_cpu_status_t *cs = maincpu_int_status;
    cs->global_pending_int |= IK_TRAP;
    cs->trap_func = trap_func;
    cs->trap_data = data;
}

void interrupt_do_trap(interrupt_cpu_status_t *cs, WORD address)
{
    cs->global_pending_int &= ~IK_TRAP;
    cs->trap_func(address, cs->trap_data);
}

void interrupt_monitor_trap_on(interrupt_cpu_status_t *cs)
{
    cs->global_pending_int |= IK_MONITOR;
}

void interrupt_monitor_trap_off(interrupt_cpu_status_t *cs)
{
    cs->global_pending_int &= ~IK_MONITOR;
}

/* Pending-interrupt check used by the main-CPU loop. */
int check_pending_interrupt(interrupt_cpu_status_t *cs)
{
    return cs->global_pending_int != IK_NONE;
}
