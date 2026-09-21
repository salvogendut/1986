#include "via6522.h"
#include <stdio.h>

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
    failures++; \
} } while (0)

typedef struct { unsigned port; u8 pins; int calls; } PortProbe;
static void port_changed(void *ctx, unsigned port, u8 pins) {
    PortProbe *probe = ctx;
    probe->port = port;
    probe->pins = pins;
    probe->calls++;
}

int main(void) {
    Via6522 via;
    via6522_init(&via);
    CHECK(via6522_read(&via, 13) == 0 && via6522_read(&via, 14) == 0x80,
          "reset IFR and IER readback");
    CHECK(via6522_read(&via, 1) == 0xff,
          "unconnected port inputs float high");

    PortProbe probe = {0};
    via6522_set_port_hook(&via, port_changed, &probe);
    CHECK(probe.calls == 2 && probe.port == 1 && probe.pins == 0xff,
          "output hook observes initial port pins");
    via6522_set_input_a(&via, 0xa5);
    via6522_write(&via, 3, 0xf0);
    via6522_write(&via, 1, 0x3c);
    CHECK(via6522_read(&via, 1) == 0x35,
          "port A mixes driven high nibble with external low nibble");
    CHECK(probe.port == 0 && probe.pins == 0x3f,
          "port hook receives effective driven pins");

    via6522_write(&via, 14, 0x82); /* enable CA1 */
    via6522_set_ca1(&via, true);
    via6522_set_ca1(&via, false); /* default falling-edge mode */
    CHECK(via6522_irq(&via) && via6522_read(&via, 13) == 0x82,
          "CA1 edge raises enabled IRQ and IFR bit 7");
    CHECK(via6522_read(&via, 15) == 0x35 && via6522_irq(&via),
          "no-handshake port A does not acknowledge CA1");
    CHECK(via6522_read(&via, 1) == 0x35 && !via6522_irq(&via),
          "normal port A read acknowledges CA1");
    via6522_write(&via, 12, 0x10); /* CB1 rising edge */
    via6522_write(&via, 14, 0x90); /* enable CB1 */
    via6522_set_cb1(&via, true);
    CHECK(via6522_read(&via, 13) == 0x90,
          "PCR selects CB1 rising edge");
    via6522_write(&via, 13, 0x10);
    CHECK(!via6522_irq(&via), "IFR write-one-to-clear deasserts IRQ");
    CHECK(via6522_read(&via, 14) == 0x92,
          "IER readback keeps bit 7 set and both enables");
    via6522_write(&via, 14, 0x02); /* disable CA1 */
    CHECK(via6522_read(&via, 14) == 0x90,
          "IER clear operation removes selected enable bit");

    int calls_before_reset = probe.calls;
    via6522_reset(&via);
    CHECK(probe.calls == calls_before_reset + 2 && probe.pins == 0xff,
          "reset retains wiring hook and returns pins high");
    via6522_write(&via, 14, 0xc0); /* T1 IRQ enabled */
    via6522_write(&via, 4, 2);
    via6522_write(&via, 5, 0); /* T1=2, expires after 3 clocks */
    via6522_tick(&via, 2);
    CHECK(!via6522_irq(&via) && via6522_read(&via, 4) == 0,
          "T1 counter counts down without premature IRQ");
    via6522_tick(&via, 1);
    CHECK(via6522_read(&via, 13) == 0xc0,
          "T1 one-shot underflow raises IRQ");
    (void)via6522_read(&via, 4);
    via6522_tick(&via, 10);
    CHECK(!via6522_irq(&via), "T1 one-shot does not retrigger after acknowledge");

    via6522_write(&via, 11, 0x40); /* T1 free-run */
    via6522_write(&via, 4, 1);
    via6522_write(&via, 5, 0); /* two-clock period */
    via6522_tick(&via, 5);
    CHECK(via6522_irq(&via), "T1 free-run triggers across a multi-cycle tick");
    (void)via6522_read(&via, 4);
    via6522_tick(&via, 1);
    CHECK(via6522_irq(&via), "T1 free-run retriggers after acknowledgement");

    via6522_reset(&via);
    via6522_write(&via, 11, 0x20); /* T2 counts PB6 falling edges */
    via6522_write(&via, 14, 0xa0);
    via6522_write(&via, 8, 1);
    via6522_write(&via, 9, 0);
    via6522_tick(&via, 100);
    CHECK(!via6522_irq(&via), "T2 pulse mode ignores Phi2 clocks");
    via6522_set_input_b(&via, 0xbf);
    CHECK(!via6522_irq(&via), "first PB6 falling edge decrements T2");
    via6522_set_input_b(&via, 0xff);
    via6522_set_input_b(&via, 0xbf);
    CHECK(via6522_read(&via, 13) == 0xa0,
          "second PB6 falling edge triggers T2 IRQ");
    (void)via6522_read(&via, 8);
    CHECK(!via6522_irq(&via), "T2 low read acknowledges its IRQ");

    via6522_reset(&via);
    via6522_write(&via, 11, 0x80); /* T1 controls PB7 */
    via6522_write(&via, 4, 0);
    via6522_write(&via, 5, 0);
    CHECK(!(via6522_read(&via, 0) & 0x80), "T1 start drives PB7 low");
    via6522_tick(&via, 1);
    CHECK(via6522_read(&via, 0) & 0x80,
          "T1 one-shot underflow drives PB7 high");

    if (!failures) puts("test-via6522: OK");
    return failures ? 1 : 0;
}
