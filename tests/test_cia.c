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
    cia_set_cnt(&c, false); /* no external high level to open the gate */
    cia_tick(&c, 3);
    CHECK(cia_read(&c, 0xDC06) == 2,
          "CNT-gated mode does not count while CNT is low");
    cia_set_cnt(&c, true);
    cia_tick(&c, 2);
    CHECK(cia_read(&c, 0xDC06) == 1,
          "CNT-gated Timer B counts Timer A underflows while CNT is high");

    cia_reset(&c);
    set_timer_b(&c, 0);
    cia_write(&c, 0xDC0F, 0x11);
    cia_tick(&c, 63);
    CHECK(cia_read(&c, 0xDC0D) == 0 && cia_read(&c, 0xDC06) == 0xC1,
          "zero latch is a full 65536-clock interval, not instant underflow");

    cia_reset(&c);
    CHECK(cia_read(&c, 0xDC0B) == 0x01,
          "TOD resets to 1:00:00.0");
    cia_write(&c, 0xDC0D, 0x84); /* enable TOD alarm interrupt */
    cia_write(&c, 0xDC0F, 0x80); /* write alarm */
    cia_write(&c, 0xDC0B, 0x01);
    cia_write(&c, 0xDC0A, 0x00);
    cia_write(&c, 0xDC09, 0x00);
    cia_write(&c, 0xDC08, 0x01);
    cia_write(&c, 0xDC0F, 0x00); /* write clock */
    cia_write(&c, 0xDC0B, 0x01); /* stop while setting time */
    cia_write(&c, 0xDC0A, 0x00);
    cia_write(&c, 0xDC09, 0x00);
    cia_write(&c, 0xDC08, 0x00); /* restart */
    cia_write(&c, 0xDC0E, 0x80); /* 50 Hz mains reference */
    cia_read(&c, 0xDC0D); /* setting the alarm may match the initial clock */
    for (int i = 0; i < 4; ++i) cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC08) == 0x00 && cia_read(&c, 0xDC0D) == 0,
          "TOD does not advance before five 50 Hz pulses");
    cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC08) == 0x01 && cia_irq_line(&c) &&
          cia_read(&c, 0xDC0D) == 0x84,
          "matching TOD alarm raises ICR bit 2 and IRQ");
    CHECK(!cia_irq_line(&c), "reading ICR acknowledges TOD alarm");

    CHECK(cia_read(&c, 0xDC0B) == 0x01, "reading TOD hours latches the clock");
    for (int i = 0; i < 5; ++i) cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC08) == 0x01 && cia_read(&c, 0xDC08) == 0x02,
          "TOD snapshot persists until tenths are read");

    cia_reset(&c);
    cia_write(&c, 0xDC0E, 0x80);
    cia_write(&c, 0xDC0B, 0x11);
    cia_write(&c, 0xDC0A, 0x59);
    cia_write(&c, 0xDC09, 0x59);
    cia_write(&c, 0xDC08, 0x09);
    for (int i = 0; i < 5; ++i) cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC0B) == 0x92 &&
          cia_read(&c, 0xDC0A) == 0x00 &&
          cia_read(&c, 0xDC09) == 0x00 &&
          cia_read(&c, 0xDC08) == 0x00,
          "TOD rolls 11:59:59.9 AM to 12:00:00.0 PM");
    cia_write(&c, 0xDC0A, 0x59);
    cia_write(&c, 0xDC09, 0x59);
    cia_write(&c, 0xDC08, 0x09);
    for (int i = 0; i < 5; ++i) cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC0B) == 0x81 && cia_read(&c, 0xDC08) == 0x00,
          "TOD rolls 12:59:59.9 PM to 1:00:00.0 PM");

    cia_reset(&c);
    cia_write(&c, 0xDC0B, 0x12);
    CHECK(cia_read(&c, 0xDC0B) == 0x92,
          "writing clock hour 12 toggles the AM/PM latch");

    cia_reset(&c);
    cia_write(&c, 0xDC08, 0x00); /* start 60 Hz mode */
    cia_tick(&c, 1000000);
    CHECK(cia_read(&c, 0xDC08) == 0x00,
          "CPU clock cycles alone do not advance the mains-driven TOD");
    for (int i = 0; i < 5; ++i) cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC08) == 0x00,
          "60 Hz mode does not tick after five mains pulses");
    cia_tod_tick(&c);
    CHECK(cia_read(&c, 0xDC08) == 0x01,
          "60 Hz mode advances after six mains pulses");

    cia_reset(&c);
    set_timer_a(&c, 2);
    cia_write(&c, 0xDC0D, 0x88); /* serial interrupt mask */
    cia_write(&c, 0xDC0E, 0x51); /* serial output + force-load + Timer A */
    cia_write(&c, 0xDC0C, 0xA5);
    cia_tick(&c, 2);
    CHECK(!c.cnt_output_high && c.sp_output_high,
          "serial output presents the high bit before the first CNT pulse");
    cia_tick(&c, 2);
    CHECK(c.cnt_output_high && c.sp_output_high,
          "first CNT rising edge transmits the high bit");
    cia_tick(&c, 2);
    CHECK(!c.cnt_output_high && !c.sp_output_high,
          "next bit appears on SP while CNT is low");
    cia_tick(&c, 24); /* 15 Timer A underflows in total */
    CHECK(!cia_irq_line(&c) && (cia_read(&c, 0xDC0D) & 0x08) == 0,
          "serial output has not completed before eighth CNT pulse");
    cia_tick(&c, 2);
    CHECK(cia_irq_line(&c) && (cia_read(&c, 0xDC0D) & 0x88) == 0x88,
          "serial output completion raises SDR interrupt");
    Cia receiver;
    cia_init(&receiver);
    CHECK(cia_read(&receiver, 0xDD0C) == 0 &&
          cia_read(&receiver, 0xDD0D) == 0,
          "CIA serial pins are not looped together without a harness");

    cia_reset(&c);
    cia_write(&c, 0xDC0D, 0x88);
    for (int bit = 7; bit >= 0; bit--) {
        cia_set_cnt(&c, false);
        cia_set_sp(&c, (0xA5 & (1 << bit)) != 0);
        cia_set_cnt(&c, true);
        if (bit == 1)
            CHECK(!cia_irq_line(&c), "serial input waits for all eight bits");
    }
    CHECK(cia_read(&c, 0xDC0C) == 0xA5 && cia_irq_line(&c) &&
          cia_read(&c, 0xDC0D) == 0x88,
          "CNT-clocked serial input fills SDR and raises IRQ");

    cia_reset(&c);
    cia_write(&c, 0xDC0D, 0x90);
    cia_set_flag(&c, false);
    CHECK(cia_irq_line(&c) && cia_read(&c, 0xDC0D) == 0x90,
          "falling FLAG input raises ICR bit 4 and IRQ");
    cia_set_flag(&c, false);
    CHECK(cia_read(&c, 0xDC0D) == 0,
          "holding FLAG low does not raise another interrupt");
    cia_set_flag(&c, true);
    cia_set_flag(&c, false);
    CHECK(cia_read(&c, 0xDC0D) == 0x90,
          "a new falling FLAG edge raises another interrupt");

    cia_reset(&c);
    set_timer_a(&c, 2);
    cia_write(&c, 0xDC0E, 0x31); /* CNT input clocks Timer A */
    cia_set_cnt(&c, false);
    cia_set_cnt(&c, true);
    CHECK(cia_read(&c, 0xDC04) == 1,
          "CNT rising edge advances CNT-driven Timer A");
    cia_set_cnt(&c, false);
    cia_set_cnt(&c, true);
    CHECK((cia_read(&c, 0xDC0D) & 0x01) != 0,
          "CNT-driven Timer A underflow raises its interrupt flag");

    cia_reset(&c);
    set_timer_b(&c, 2);
    cia_write(&c, 0xDC0F, 0x31); /* CNT input clocks Timer B */
    cia_set_cnt(&c, false);
    cia_set_cnt(&c, true);
    CHECK(cia_read(&c, 0xDC06) == 1,
          "CNT rising edge advances CNT-driven Timer B");
    cia_set_cnt(&c, false);
    cia_set_cnt(&c, true);
    CHECK((cia_read(&c, 0xDC0D) & 0x02) != 0,
          "CNT-driven Timer B underflow raises its interrupt flag");

    if (failures == 0) { printf("test-cia: OK\n"); return 0; }
    printf("test-cia: %d failure(s)\n", failures);
    return 1;
}
