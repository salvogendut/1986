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

## Status

The emulator boots the C128 KERNAL and BASIC 7.0 to a usable `READY.` prompt.
Current working pieces include:

- VIC-IIe 40-column text/bitmap display with eight hardware sprites, and the
  VDC 80-column text display.
- Host keyboard input, paste, cursor, CIA timer and raster IRQ handling.
- Function-key conventions: F4 screenshot, F5 reset, F6 GIF capture, F8
  monitor, F9 options overlay, F10 40/80 switch, F11 fullscreen, F12 quit.
- Options overlay that reads and writes
  `1986.conf`.
- VICE's 8502/6510 core and a reused Z80 core wired to the C128 bus.
- A fast virtual IEC drive that reads D64 images and supports `DIRECTORY`,
  `LOAD`, and BASIC 7.0 `DLOAD`, including DOS status errors.
- Tests for the CPU, MMU, VIC graphics and sprites, configuration, GIF encoder,
  D64 format, KERNAL IEC trap contract, and virtual-drive channel lifecycle.

Saving to the virtual drive, true cycle-level 1571 emulation, SID audio, CP/M
mode, and several accuracy features remain unfinished. The virtual drive and
future true 1571 are intentionally separate implementations; they will share
only the media/image layer.

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

F4 screenshot, F5 reset, F6 GIF capture, F7 pause, F8 monitor, F9 options,
F10 40/80-column switch, F11 fullscreen, F12 quit. See
[CONTROLS.md](CONTROLS.md).

## Usage

See [USAGE.md](USAGE.md) for command-line options, configuration, and ROM
layout.
