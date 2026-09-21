#include "joyport.h"
#include <stdio.h>

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", message); ++failures; } } while (0)

int main(void) {
    JoyPorts ports;
    joyports_reset(&ports);
    CHECK(joyports_digital(&ports, 0, false) == 0xff &&
          joyports_digital(&ports, 1, false) == 0xff, "both ports start released");
    joyports_set_joystick(&ports, 0, JOY_UP | JOY_FIRE);
    joyports_set_joystick(&ports, 1, JOY_LEFT);
    CHECK(joyports_digital(&ports, 0, false) == (u8)~(JOY_UP | JOY_FIRE),
          "port 1 uses active-low CIA pins");
    CHECK(joyports_digital(&ports, 1, false) == (u8)~JOY_LEFT,
          "port 2 is independent");
    joyports_mouse_motion(&ports, 0, 5, -3);
    joyports_mouse_button(&ports, 0, false, true);
    joyports_mouse_button(&ports, 0, true, true);
    CHECK(joyports_digital(&ports, 0, true) == (u8)~(JOY_UP | JOY_FIRE),
          "1351 left/right buttons use fire/up pins");
    CHECK(joyports_pot(&ports, 0, true, false) == 0x45 &&
          joyports_pot(&ports, 0, true, true) == 0xbd,
          "1351 motion appears on SID POTX/POTY counters");
    CHECK(joyports_pot(&ports, 1, false, false) == 0xff,
          "joystick mode leaves POT lines disconnected");
    joyports_mouse_button(&ports, 0, false, false);
    joyports_mouse_button(&ports, 0, true, false);
    CHECK(joyports_digital(&ports, 0, true) == 0xff,
          "release clears 1351 buttons");
    if (!failures) puts("joyport: ok");
    return failures ? 1 : 0;
}
