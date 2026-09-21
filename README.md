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
6502-like 8502 core is adapted from VICE.

The name "1986" was chosen not only because of the sibling projects and the
year of the C128DCR's introduction, but also because it was the year I got
my first computer, at the tender age of 13: a Commodore C128.

## Acknowledgments

1986 owes a great deal to [VICE, the Versatile Commodore Emulator](https://vice-emu.sourceforge.io/)
and its contributors. Our code broadly borrows from VICE: the 8502/6510 CPU
core is adapted from it, and its C128 hardware emulation has been an important
reference for the rest of the machine. Ported VICE source files retain their
original copyright and license notices in `src/vice/`. Thank you to the VICE
team for making this work available.

## Native C128 scope

1986 deliberately treats the Commodore 128 as a platform in its own right.
It targets native C128 software, including the VIC-IIe 40-column and VDC
80-column environments; CP/M mode is also planned. The separate C64
personality entered with `GO64` or the Commodore-key boot path is intentionally
out of scope. A C64-mode request is rejected with a clear notification and the
machine continues in native C128 mode.

The shared VIC-IIe, SID, CIA and IEC hardware will still be implemented as
accurately as native C128 software requires. This scope decision avoids
carrying a partial C64 PLA, ROM, cartridge and compatibility implementation in
a project whose purpose is the C128 itself.

## Status

The emulator boots the C128 KERNAL and BASIC 7.0 to a usable `READY.` prompt.
Current working pieces include:

- VIC-IIe 40-column text/bitmap display with eight hardware sprites, and the
  VDC 80-column text display.
- Host keyboard input, paste, cursor, CIA timer and raster IRQ handling.
- Audible three-voice 8580 SID synthesis through SDL3, including the standard
  waveforms, envelopes, voice routing, and a basic filter approximation.
- Function-key conventions: F4 screenshot, F5 reset, F6 GIF capture, F8
  monitor, F9 options overlay, F10 40/80 switch, F11 fullscreen, F12 quit.
- Compact options overlay that reads and writes `1986.conf`, with an optional
  second virtual IEC drive and separately selected disk image/unit.
- VICE's 8502/6510 core and a reused Z80 core wired to the C128 bus.
- A fast virtual IEC drive that reads and writes D64, D71, and D81 images,
  supporting `DIRECTORY`, `LOAD`/`DLOAD`, `SAVE`/`DSAVE`, and DOS `SCRATCH`/
  `RENAME` commands with status errors.
- Tests for the CPU, MMU, CIA/SID, VIC graphics and sprites, configuration,
  GIF encoder, disk formats, KERNAL IEC traps, and virtual-drive channels.

Further write-side DOS commands, true cycle-level 1571 emulation,
high-fidelity SID filter/combined-waveform emulation, CP/M mode, and several
accuracy features remain unfinished. The virtual drive does not emulate 1571
or 1581 hardware; it and the future true 1571 will share only the media/image
layer. Advanced > Real Disk Drive is a saved preference for that future
backend; while it is pending, the fast virtual drive stays active.

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
