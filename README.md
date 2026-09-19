# 1986 - Commodore C128DCR emulator

1986 is a work-in-progress emulator of the Commodore C128DCR. The emulation
core is written in C and the desktop application uses SDL3.

It is the latest in a series of sibling projects that share an architecture:

| Repo  | Machine                                  |
|-------|------------------------------------------|
| 1983  | MSX (Z80)                                |
| 1984  | Amstrad CPC (Z80)                        |
| 1985  | Amstrad PCW (Z80)                        |
| 1986  | **Commodore C128DCR (8502 + Z80)**       |

From those it reuses the SDL3 display layer, the function-key conventions,
the options overlay, and the cycle-stepped Z80 core (for CP/M mode). The
6502-like 8502 core is being ported from the reference VICE SDL port.

The name "1986" was chosen not only because of the sibling projects and the
year of the C128DCR's introduction, but also because it was the year I got
my first computer, at the tender age of 13: a Commodore C128.

## Status: scaffolding

This tree currently boots a runnable skeleton:

- SDL3 window with a VIC-IIe test pattern (border + colour grid).
- Function-key conventions: F4 screenshot, F5 reset, F6 GIF capture, F8
  monitor, F9 options overlay, F10 pause, F11 fullscreen, F12 quit.
- Options overlay (General / Video / Capture) that reads and writes
  `1986.conf`.
- A compact 8502/6502 interpreter (standard opcode set, binary mode) and the
  reused Z80 core, both wired to a C128 memory map.
- MMU, VIC-IIe, VDC 8563, CIAs, and SID register stubs.
- Tests for the CPU, MMU, config, and GIF encoder.

It does **not** yet boot the KERNAL/BASIC: the ROM pipeline, real VIC raster,
VDC rendering, CIA timers, SID audio, and the 1571 drives are TODO.

See [DEVELOPMENT.md](Development.md) for technical notes and
[ROADMAP.md](ROADMAP.md) for the forward plan.

## Build from source

On Fedora:

```bash
sudo dnf install gcc make autoconf automake pkgconf-pkg-config sdl3-devel
autoreconf -iv
./configure
make -j"$(nproc)"
./1986
```

See [INSTALL.md](INSTALL.md) for other platforms.

## Run the tests

```bash
make -C tests check
```

## Controls

F4 screenshot, F5 reset, F6 GIF capture, F8 monitor, F9 options, F10 pause,
F11 fullscreen, F12 quit. See [CONTROLS.md](CONTROLS.md).

## Usage

See [USAGE.md](USAGE.md) for command-line options, configuration, and ROM
layout.
