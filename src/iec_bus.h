#pragma once
#include "types.h"
#include "via6522.h"
#include <stdbool.h>

/* Slow IEC lines between the C128 CIA2 and the integrated 1571CR VIA1.
 * The output pins feed open-collector drivers: any asserted output pulls a
 * line low. CIA2 PA6/PA7 and VIA1 PB0/PB2/PB7 read the resulting lines.
 * ATNA (VIA1 PB4) also participates in the 1571's DATA acknowledge gate.
 * Fast-serial SP/CNT and the other IEC devices are not connected yet. */
typedef struct {
    Via6522 *drive_via;
    u8 host_pa;             /* effective CIA2 PA: PRA | ~DDRA */
    u8 drive_pb;            /* effective VIA1 PB: ORB | ~DDRB */
    bool atn_high, clock_high, data_high;
} IecBus;

void iec_bus_init(IecBus *bus, Via6522 *drive_via);
void iec_bus_reset(IecBus *bus);
void iec_bus_set_host(IecBus *bus, u8 pra, u8 ddra);
void iec_bus_set_drive(IecBus *bus, u8 pins);
/* Mask to OR into CIA2 PA after clearing bits 6-7. */
u8 iec_bus_host_inputs(const IecBus *bus);
