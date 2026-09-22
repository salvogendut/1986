#include "iec_bus.h"
#include <stdio.h>

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
    failures++; \
} } while (0)

static void via_port(void *ctx, unsigned port, u8 pins) {
    if (port == 1) iec_bus_set_drive(ctx, pins);
}

static void via_port2(void *ctx, unsigned port, u8 pins) {
    if (port == 1) iec_bus_set_drive2(ctx, pins);
}

int main(void) {
    Via6522 via;
    IecBus bus;
    via6522_init(&via);
    via6522_write(&via, 12, 0x01); /* 1571 ROM enables rising CA1 edge */
    iec_bus_init(&bus, &via);
    via6522_set_port_hook(&via, via_port, &bus);
    via6522_write(&via, 2, 0x1a); /* PB1 DATA, PB3 CLK, PB4 ATNA output */
    via6522_write(&via, 0, 0x00); /* release all drive outputs */
    iec_bus_set_host(&bus, 0x00, 0x38); /* release ATN, CLK and DATA */
    CHECK(bus.atn_high && bus.clock_high && bus.data_high &&
          iec_bus_host_inputs(&bus) == 0xc0,
          "idle open-collector lines are high at the CIA inputs");
    CHECK((via6522_read(&via, 0) & 0x85) == 0,
          "VIA1 IEC inputs are active-low on idle lines");
    CHECK((via6522_read(&via, 0) & 0x60) == 0,
          "integrated 1571CR address sense selects IEC unit 8");
    iec_bus_set_unit(&bus, 9);
    CHECK((via6522_read(&via, 0) & 0x60) == 0x20,
          "VIA1 address straps can select IEC unit 9");
    iec_bus_set_unit(&bus, 8);

    iec_bus_set_host(&bus, 0x08, 0x38); /* assert ATN */
    CHECK(!bus.atn_high && !bus.data_high && bus.clock_high,
          "ATN transition activates the 1571 ATNA DATA acknowledgement");
    CHECK((via6522_read(&via, 13) & 0x02) != 0,
          "ATN falling edge reaches VIA1 CA1 interrupt flag");
    CHECK((via6522_read(&via, 0) & 0x80) != 0,
          "VIA1 PB7 reads the asserted ATN line");

    via6522_write(&via, 0, 0x10); /* release ATNA while ATN low */
    CHECK(bus.data_high && (iec_bus_host_inputs(&bus) & 0x80),
          "drive can release DATA while ATN is asserted");
    iec_bus_set_host(&bus, 0x18, 0x38); /* host asserts CLOCK too */
    CHECK(!bus.clock_high && (via6522_read(&via, 0) & 0x04),
          "CIA2 CLOCK output reaches VIA1 PB2");

    iec_bus_set_host(&bus, 0x00, 0x38);
    via6522_write(&via, 0, 0x08); /* drive asserts CLOCK */
    CHECK(!bus.clock_high && !(iec_bus_host_inputs(&bus) & 0x40),
          "VIA1 PB3 CLOCK output reaches CIA2 PA6");
    via6522_write(&via, 0, 0x02); /* drive asserts DATA */
    CHECK(!bus.data_high && !(iec_bus_host_inputs(&bus) & 0x80),
          "VIA1 PB1 DATA output reaches CIA2 PA7");

    via6522_write(&via, 0, 0x00);
    iec_bus_set_host(&bus, 0x20, 0x38); /* host asserts DATA */
    CHECK(!bus.data_high && (via6522_read(&via, 0) & 0x01),
          "CIA2 DATA output reaches VIA1 PB0");
    iec_bus_set_host(&bus, 0x00, 0x00); /* input pins float high */
    CHECK(!bus.atn_high && !bus.clock_high,
          "CIA2 input DDR uses the same effective-port pins as VICE");

    Via6522 via2;
    via6522_init(&via2);
    via6522_write(&via2, 12, 0x01);
    iec_bus_attach_second(&bus, &via2, 9);
    via6522_set_port_hook(&via2, via_port2, &bus);
    via6522_write(&via2, 2, 0x1a);
    via6522_write(&via2, 0, 0x00);
    iec_bus_set_host(&bus, 0x00, 0x38);
    iec_bus_enable_second(&bus, true);
    CHECK(bus.atn_high && bus.clock_high && bus.data_high &&
          (via6522_read(&via2, 0) & 0x60) == 0x20,
          "second VIA shares idle bus but senses IEC unit 9");
    iec_bus_set_host(&bus, 0x08, 0x38);
    CHECK((via6522_read(&via, 0) & 0x80) &&
          (via6522_read(&via2, 0) & 0x80) &&
          (via6522_read(&via2, 13) & 0x02),
          "ATN reaches both drive VIA interrupt inputs");
    iec_bus_set_host(&bus, 0x00, 0x38);
    via6522_write(&via2, 0, 0x08);
    CHECK(!bus.clock_high && (via6522_read(&via, 0) & 0x04),
          "second drive pulls shared CLOCK low for first drive");
    via6522_write(&via2, 0, 0x02);
    CHECK(!bus.data_high && (via6522_read(&via, 0) & 0x01),
          "second drive pulls shared DATA low for first drive");
    iec_bus_enable_second(&bus, false);
    CHECK(bus.clock_high && bus.data_high,
          "disconnecting second drive releases its IEC outputs");

    if (failures) {
        fprintf(stderr, "test-iec-bus: %d failure(s)\n", failures);
        return 1;
    }
    puts("test-iec-bus: OK");
    return 0;
}
