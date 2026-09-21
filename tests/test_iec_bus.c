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

int main(void) {
    Via6522 via;
    IecBus bus;
    via6522_init(&via);
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

    if (failures) {
        fprintf(stderr, "test-iec-bus: %d failure(s)\n", failures);
        return 1;
    }
    puts("test-iec-bus: OK");
    return 0;
}
