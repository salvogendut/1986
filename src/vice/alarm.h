/* alarm.h - minimal VICE alarm shim (self-contained). GPLv2+; see Development.md. */
#ifndef VICE_ALARM_H
#define VICE_ALARM_H
#include "types.h"
struct alarm_context_s;
typedef struct alarm_context_s alarm_context_t;
alarm_context_t *alarm_context_new(void);
void alarm_context_destroy(alarm_context_t *ctx);
void alarm_context_set_pending_clk(alarm_context_t *ctx, CLOCK clk);
CLOCK alarm_context_next_pending_clk(alarm_context_t *ctx);
void alarm_context_dispatch(alarm_context_t *ctx, CLOCK clk);
#endif
