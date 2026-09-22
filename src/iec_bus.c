#include "iec_bus.h"
#include <stdio.h>
#include <stdlib.h>

static void trace(const IecBus *bus, char source) {
    static unsigned printed;
    if (printed >= 160 || !getenv("C128_IEC_TRACE")) return;
    fprintf(stderr, "[iec] %03u %c host=$%02x drive=$%02x lines=%d%d%d\n",
            printed++, source, bus->host_pa, bus->drive_pb,
            bus->atn_high, bus->clock_high, bus->data_high);
}

static void update(IecBus *bus) {
    bool old_atn = bus->atn_high, old_clock = bus->clock_high;
    bool old_data = bus->data_high;
    /* VICE 3.10: c64iec.c/iec_update_cpu_bus() and
     * drive/iec/via1d1541.c/store_prb(). In this representation a low CIA2
     * or VIA output releases a line; a high output pulls it low. The 1571
     * data gate additionally compares ATNA to the host ATN level. */
    bus->atn_high = !(bus->host_pa & 0x08);
    bus->clock_high = !(bus->host_pa & 0x10) && !(bus->drive_pb & 0x08);
    bool atna_high = (bus->drive_pb & 0x10) != 0;
    /* VICE passes the inverted CIA2 output to iec_update_cpu_bus(), so
     * its cpu_bus ATN bit represents the physical ATN-high level. */
    bool drive_data_release = !(bus->drive_pb & 0x02) &&
                              (atna_high != bus->atn_high);
    bus->data_high = !(bus->host_pa & 0x20) && drive_data_release;
    bus->line_changes += (old_atn != bus->atn_high) +
                         (old_clock != bus->clock_high) +
                         (old_data != bus->data_high);

    if (bus->drive_via) {
        /* VIA1 has inverters on the three IEC input pins. PB5/PB6 are the
         * hard-wired address sense, not floating inputs. */
        u8 pins = (u8)(bus->drive_via->input_b & (u8)~0xe5);
        pins |= (u8)((bus->drive_unit - 8) << 5);
        if (!bus->data_high) pins |= 0x01;
        if (!bus->clock_high) pins |= 0x04;
        if (!bus->atn_high) pins |= 0x80;
        via6522_set_input_b(bus->drive_via, pins);
        /* 1571 VIA1 CA1 sees inverted ATN: assertion is a rising edge.
         * Its DOS ROM selects rising-edge interrupts with PCR bit 0. */
        via6522_set_ca1(bus->drive_via, !bus->atn_high);
    }
}

void iec_bus_init(IecBus *bus, Via6522 *drive_via) {
    bus->drive_via = drive_via;
    bus->drive_unit = 8;
    iec_bus_reset(bus);
}

void iec_bus_set_unit(IecBus *bus, unsigned unit) {
    if (unit < 8 || unit > 11) return;
    bus->drive_unit = unit;
    update(bus);
}

void iec_bus_reset(IecBus *bus) {
    bus->host_pa = 0xff;
    bus->drive_pb = 0xff;
    bus->host_changes = bus->drive_changes = bus->line_changes = 0;
    update(bus);
}

void iec_bus_set_host(IecBus *bus, u8 pra, u8 ddra) {
    u8 pins = (u8)(pra | (u8)~ddra);
    bool changed = pins != bus->host_pa;
    if (changed) bus->host_changes++;
    bus->host_pa = pins;
    update(bus);
    if (changed) trace(bus, 'H');
}

void iec_bus_set_drive(IecBus *bus, u8 pins) {
    bool changed = pins != bus->drive_pb;
    if (changed) bus->drive_changes++;
    bus->drive_pb = pins;
    update(bus);
    if (changed) trace(bus, 'D');
}

u8 iec_bus_host_inputs(const IecBus *bus) {
    return (u8)((bus->clock_high ? 0x40 : 0) |
                (bus->data_high ? 0x80 : 0));
}
