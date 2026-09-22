#include "iec_bus.h"

static void update(IecBus *bus) {
    /* VICE 3.10: c64iec.c/iec_update_cpu_bus() and
     * drive/iec/via1d1541.c/store_prb(). In this representation a low CIA2
     * or VIA output releases a line; a high output pulls it low. The 1571
     * data gate additionally compares ATNA to the host ATN level. */
    bus->atn_high = !(bus->host_pa & 0x08);
    bus->clock_high = !(bus->host_pa & 0x10) && !(bus->drive_pb & 0x08);
    bool atna_high = (bus->drive_pb & 0x10) != 0;
    bool drive_data_release = !(bus->drive_pb & 0x02) &&
                              (atna_high != bus->atn_high);
    bus->data_high = !(bus->host_pa & 0x20) && drive_data_release;

    if (bus->drive_via) {
        /* VIA1 has inverters on the three IEC input pins. Preserve all
         * unrelated input pins, including the mechanism's PB6 timer input. */
        u8 pins = (u8)(bus->drive_via->input_b & (u8)~0x85);
        if (!bus->data_high) pins |= 0x01;
        if (!bus->clock_high) pins |= 0x04;
        if (!bus->atn_high) pins |= 0x80;
        via6522_set_input_b(bus->drive_via, pins);
        via6522_set_ca1(bus->drive_via, bus->atn_high);
    }
}

void iec_bus_init(IecBus *bus, Via6522 *drive_via) {
    bus->drive_via = drive_via;
    iec_bus_reset(bus);
}

void iec_bus_reset(IecBus *bus) {
    bus->host_pa = 0xff;
    bus->drive_pb = 0xff;
    update(bus);
}

void iec_bus_set_host(IecBus *bus, u8 pra, u8 ddra) {
    bus->host_pa = (u8)(pra | (u8)~ddra);
    update(bus);
}

void iec_bus_set_drive(IecBus *bus, u8 pins) {
    bus->drive_pb = pins;
    update(bus);
}

u8 iec_bus_host_inputs(const IecBus *bus) {
    return (u8)((bus->clock_high ? 0x40 : 0) |
                (bus->data_high ? 0x80 : 0));
}
