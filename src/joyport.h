#pragma once
#include "types.h"
#include <stdbool.h>

/* C128 control-port pins 1..4 and 6, active low at CIA1. */
#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_FIRE  0x10

typedef struct {
    u8 joystick[2];       /* pressed-direction masks, port 1/2 */
    u8 mouse_buttons[2];  /* left = fire, right = up (1351) */
    u8 mouse_x[2], mouse_y[2];
} JoyPorts;

void joyports_reset(JoyPorts *ports);
void joyports_set_joystick(JoyPorts *ports, unsigned port, u8 pressed);
void joyports_mouse_motion(JoyPorts *ports, unsigned port, int dx, int dy);
void joyports_mouse_button(JoyPorts *ports, unsigned port, bool right, bool down);
u8 joyports_digital(const JoyPorts *ports, unsigned port, bool mouse_mode);
u8 joyports_pot(const JoyPorts *ports, unsigned port, bool mouse_mode, bool y);
