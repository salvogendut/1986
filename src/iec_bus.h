#pragma once
#include "types.h"
#include "via6522.h"
#include <stdbool.h>

typedef void (*IecBusInputHook)(void *ctx, unsigned unit, bool atn,
                                 bool clock, bool data);

/* Shared slow IEC lines between the C128 CIA2 and up to two 1571/1581s.
 * The output pins feed open-collector drivers: any asserted output pulls a
 * line low. CIA2 PA6/PA7 and VIA1 PB0/PB2/PB7 read the resulting lines.
 * PB4 participates in model-specific DATA acknowledge gates (1571 XOR,
 * 1581 OR). Input hooks receive physical levels and must not drive outputs.
 * Fast-serial SP/CNT and the other IEC devices are not connected yet. */
typedef struct {
    Via6522 *drive_via;
    Via6522 *drive2_via;
    IecBusInputHook input_hook[2]; /* 1581 CIA adapters; NULL for a VIA */
    void *input_ctx[2];
    u8 host_pa;             /* effective CIA2 PA: PRA | ~DDRA */
    u8 drive_pb;            /* effective VIA1 PB: ORB | ~DDRB */
    u8 drive2_pb;
    unsigned drive_unit;    /* hard-wired VIA1 PB5/PB6 address, 8-11 */
    unsigned drive2_unit;
    bool drive2_enabled;
    bool atn_high, clock_high, data_high;
    unsigned host_changes, drive_changes, line_changes;
} IecBus;

void iec_bus_init(IecBus *bus, Via6522 *drive_via);
void iec_bus_reset(IecBus *bus);
void iec_bus_set_unit(IecBus *bus, unsigned unit);
void iec_bus_attach_second(IecBus *bus, Via6522 *drive_via, unsigned unit);
void iec_bus_attach_first(IecBus *bus, Via6522 *via, unsigned unit);
void iec_bus_attach_1581(IecBus *bus, unsigned slot, IecBusInputHook hook,
                         void *ctx, unsigned unit);
void iec_bus_enable_second(IecBus *bus, bool enabled);
void iec_bus_set_host(IecBus *bus, u8 pra, u8 ddra);
void iec_bus_set_drive(IecBus *bus, u8 pins);
void iec_bus_set_drive2(IecBus *bus, u8 pins);
/* Mask to OR into CIA2 PA after clearing bits 6-7. */
u8 iec_bus_host_inputs(const IecBus *bus);
