#include "cia.h"
#include <stdio.h>

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
} while (0)

static void set_timer_a(Cia *c, u16 latch) {
    cia_write(c, 0xDC04, (u8)latch);
    cia_write(c, 0xDC05, (u8)(latch >> 8));
}

static void set_timer_b(Cia *c, u16 latch) {
    cia_write(c, 0xDC06, (u8)latch);
    cia_write(c, 0xDC07, (u8)(latch >> 8));
}

int main(void) {
    Cia c;
    cia_init(&c);

    set_timer_b(&c, 3);
    CHECK(cia_read(&c, 0xDC06) == 3 && cia_read(&c, 0xDC07) == 0,
          "stopped Timer B reads its loaded latch");
    cia_write(&c, 0xDC0F, 0x11); /* start + force load */
    CHECK(cia_read(&c, 0xDC0F) == 0x01, "force-load strobe self-clears");
    cia_tick(&c, 2);
    CHECK(cia_read(&c, 0xDC06) == 1, "Timer B counts Phi2 clocks");
    CHECK(cia_read(&c, 0xDC0D) == 0, "no early Timer B interrupt");
    cia_tick(&c, 1);
    CHECK(cia_read(&c, 0xDC06) == 3, "Timer B reloads after underflow");
    CHECK(!cia_irq_line(&c), "unmasked Timer B flag does not assert IRQ");
    cia_write(&c, 0xDC0D, 0x82); /* enable Timer B mask after event */
    CHECK(cia_irq_line(&c), "masking a pending Timer B flag asserts IRQ");
    CHECK(cia_read(&c, 0xDC0D) == 0x82, "ICR reports Timer B and IRQ bits");
    CHECK(!cia_irq_line(&c) && cia_read(&c, 0xDC0D) == 0,
          "ICR read clears Timer B flag and IRQ line");

    cia_tick(&c, 3);
    CHECK(cia_irq_line(&c), "continuous Timer B underflows again");
    cia_write(&c, 0xDC0D, 0x02); /* remove Timer B mask */
    CHECK(!cia_irq_line(&c), "clearing mask drops Timer B IRQ line");
    CHECK(cia_read(&c, 0xDC0D) == 0x02,
          "unmasked pending Timer B flag survives mask change");

    cia_reset(&c);
    set_timer_b(&c, 2);
    cia_write(&c, 0xDC0F, 0x19); /* one-shot, force-load, start */
    cia_tick(&c, 2);
    CHECK(cia_read(&c, 0xDC0D) == 0x02, "one-shot Timer B underflows once");
    CHECK((cia_read(&c, 0xDC0F) & 0x01) == 0,
          "one-shot Timer B clears START after underflow");
    cia_tick(&c, 20);
    CHECK(cia_read(&c, 0xDC0D) == 0,
          "stopped one-shot Timer B does not underflow again");

    cia_reset(&c);
    set_timer_b(&c, 5);
    cia_write(&c, 0xDC0F, 0x01);
    cia_tick(&c, 2);
    cia_write(&c, 0xDC0F, 0x00);
    CHECK(cia_read(&c, 0xDC06) == 3, "stopping Timer B retains count");
    cia_tick(&c, 10);
    CHECK(cia_read(&c, 0xDC06) == 3, "stopped Timer B does not count");
    cia_write(&c, 0xDC0F, 0x01);
    cia_tick(&c, 2);
    CHECK(cia_read(&c, 0xDC06) == 1, "restart resumes Timer B count");
    cia_write(&c, 0xDC0F, 0x11);
    CHECK(cia_read(&c, 0xDC06) == 5, "force load refreshes running Timer B");

    cia_reset(&c);
    set_timer_a(&c, 2);
    set_timer_b(&c, 4);
    cia_write(&c, 0xDC0E, 0x11);
    cia_write(&c, 0xDC0F, 0x51); /* Timer A underflows, force-load, start */
    cia_write(&c, 0xDC0D, 0x82);
    cia_tick(&c, 7); /* three Timer A underflows */
    CHECK(cia_read(&c, 0xDC06) == 1,
          "Timer B counts all Timer A underflows in a batch");
    CHECK(cia_read(&c, 0xDC0D) == 0x01,
          "Timer A flag alone does not assert Timer B-masked IRQ");
    cia_tick(&c, 2); /* fourth Timer A underflow */
    CHECK(cia_read(&c, 0xDC06) == 4,
          "cascade Timer B reloads on fourth Timer A underflow");
    CHECK(cia_irq_line(&c) && cia_read(&c, 0xDC0D) == 0x83,
          "cascade sets both timer flags and masked IRQ output");

    cia_reset(&c);
    set_timer_a(&c, 2);
    set_timer_b(&c, 2);
    cia_write(&c, 0xDC0E, 0x19); /* Timer A one-shot */
    cia_write(&c, 0xDC0F, 0x51); /* Timer B cascades from A */
    cia_tick(&c, 20);
    CHECK(cia_read(&c, 0xDC06) == 1,
          "one-shot Timer A contributes only one cascade pulse");

    cia_reset(&c);
    set_timer_a(&c, 2);
    set_timer_b(&c, 2);
    cia_write(&c, 0xDC0E, 0x11);
    cia_write(&c, 0xDC0F, 0x31); /* CNT-only Timer B */
    cia_tick(&c, 20);
    CHECK(cia_read(&c, 0xDC06) == 2,
          "CNT-only mode does not incorrectly count Phi2");
    cia_write(&c, 0xDC0F, 0x61); /* TA underflows gated by CNT */
    cia_tick(&c, 20);
    CHECK(cia_read(&c, 0xDC06) == 2,
          "CNT-gated mode does not count without an external CNT signal");

    cia_reset(&c);
    set_timer_b(&c, 0);
    cia_write(&c, 0xDC0F, 0x11);
    cia_tick(&c, 63);
    CHECK(cia_read(&c, 0xDC0D) == 0 && cia_read(&c, 0xDC06) == 0xC1,
          "zero latch is a full 65536-clock interval, not instant underflow");

    if (failures == 0) { printf("test-cia: OK\n"); return 0; }
    printf("test-cia: %d failure(s)\n", failures);
    return 1;
}
