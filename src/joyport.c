#include "joyport.h"
#include <string.h>

void joyports_reset(JoyPorts *ports) {
    memset(ports, 0, sizeof(*ports));
}

void joyports_set_joystick(JoyPorts *ports, unsigned port, u8 pressed) {
    if (port < 2) ports->joystick[port] = pressed & 0x1f;
}

void joyports_mouse_motion(JoyPorts *ports, unsigned port, int dx, int dy) {
    if (port >= 2) return;
    /* The 1351 exposes its six-bit position in POT bits 1-6; bit 0 is the
     * noise bit ignored by Commodore's driver. Map one host pixel to two raw
     * POT units so the standard divide-by-two delta routine returns one
     * pointer pixel. Host SDL Y grows downward while the 1351 counter grows
     * upward (VICE likewise subtracts relative Y motion). */
    ports->mouse_x[port] = (u8)((ports->mouse_x[port] + dx * 2) & 0x7f);
    ports->mouse_y[port] = (u8)((ports->mouse_y[port] - dy * 2) & 0x7f);
}

void joyports_mouse_button(JoyPorts *ports, unsigned port, bool right, bool down) {
    if (port >= 2) return;
    u8 bit = right ? JOY_UP : JOY_FIRE;
    if (down) ports->mouse_buttons[port] |= bit;
    else ports->mouse_buttons[port] &= (u8)~bit;
}

u8 joyports_digital(const JoyPorts *ports, unsigned port, bool mouse_mode) {
    if (port >= 2) return 0xff;
    return (u8)~(mouse_mode ? ports->mouse_buttons[port] : ports->joystick[port]);
}

u8 joyports_pot(const JoyPorts *ports, unsigned port, bool mouse_mode, bool y) {
    if (port >= 2 || !mouse_mode) return 0xff;
    return (u8)(0x40 + (y ? ports->mouse_y[port] : ports->mouse_x[port]));
}

void joyports_analog_gate_reset(JoyAnalogGate *gate) {
    gate->x_ready = false;
    gate->y_ready = false;
}

u8 joyports_analog_directions(JoyAnalogGate *gate, int x, int y) {
    u8 pressed = 0;
    if (x >= -8000 && x <= 8000) gate->x_ready = true;
    if (y >= -8000 && y <= 8000) gate->y_ready = true;
    if (gate->x_ready) {
        if (x < -16000) pressed |= JOY_LEFT;
        else if (x > 16000) pressed |= JOY_RIGHT;
    }
    if (gate->y_ready) {
        if (y < -16000) pressed |= JOY_UP;
        else if (y > 16000) pressed |= JOY_DOWN;
    }
    return pressed;
}
