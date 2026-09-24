# 1986 - Controls

This is the reference for 1986's keyboard and input controls. Machine setup,
media, and configuration are covered in [`USAGE.md`](USAGE.md).

## Host keys

| Key | Action |
|-----|--------|
| F1  | Swap the host joystick/mouse between C128 control ports |
| F2  | Tape Play/Stop |
| F3  | Rewind tape |
| F4  | Save a PPM screenshot |
| F5  | Reset |
| F6  | Toggle animated GIF recording |
| F7  | Pause or resume |
| F8  | Open/close the multiprocessor ML monitor |
| F9  | Open / save-and-close the options overlay |
| F10 | Switch between 40-column VIC-II and 80-column VDC |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Ctrl++ / Ctrl+- | Adjust window scale |
| Ctrl+V | Paste host clipboard into the C128 |

Shift+F1-F8 send the corresponding C128 function key instead of invoking the
emulator shortcut.

## C128 keyboard

The ordinary alphanumeric block uses a positional C128 mapping. The native
C128-only keys are available as follows:

| Host key | C128 key |
|----------|----------|
| Caps Lock | Locking CAPS (ASCII/DIN) switch |
| Left Alt | Commodore |
| Right Alt | ALT |
| Escape | ESC |
| End | RUN/STOP |
| Page Up | RESTORE |
| Page Down | HELP |
| Tab | TAB |
| Arrow keys | Dedicated C128 cursor keys |
| Pause | LINE FEED |
| Numeric keypad | C128 numeric keypad |
| Keypad `*` | NO SCROLL |
| Shift+Print Screen | Hold the 40/80 DISPLAY key |

These mirror the sibling projects (1983, 1984, 1985): F9 owns the overlay,
F6 owns capture, F12 quits.

## ML monitor

F8 opens an 80x25 machine-language monitor with separate MOS 8502 and Z80
contexts. Tab switches the selected CPU; F7 pauses or resumes. The monitor
provides CPU-aware disassembly, registers, MMU-visible memory dump/edit,
instruction stepping, and CPU-tagged breakpoints. See
[`docs/ML-MONITOR.md`](docs/ML-MONITOR.md) for the command reference and the
C128 bus-ownership rules.

## Options overlay

Left/Right change section, Up/Down select, Enter toggles, F9 saves and
closes, Escape closes (offering to discard if there are unsaved changes).
On either drive-image row, Ctrl+N creates and inserts a blank floppy image;
the Save dialog's `.d64`, `.d71`, or `.d81` extension selects its format, and
an omitted extension defaults to `.d64`. Del ejects the selected medium.
Sections:

- **General** — display, scaling, CRT, input, 40/80 key, Tinker, and About.
- **Media** — drive images/units/types, tape, cartridge, and optional U36 ROM.
- **Advanced** — second/real drives, VDC RAM, monitors, diagnostics, C64 test
  gate, keyboard map, and capture settings.

## GIF capture

**F6** (or `--gif-out PATH`) records an animated GIF of the current
framebuffer. The Capture section sets resolution and frame rate.

## Clipboard paste

Ctrl+V replays printable ASCII through the keyboard matrix one key at a time.
Host-layout-aware symbolic translation and typematic refinement remain future
work; live punctuation currently follows the physical/positional map.
