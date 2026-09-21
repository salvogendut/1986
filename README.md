# 1986 - Commodore C128DCR emulator

![1986 logo beside the Commodore 128 BASIC screen](1986.png)

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

## Screenshots

VIC-IIe 40-column BASIC desktop:

![VIC-IIe 40-column BASIC desktop](screenshots/vicii-basic.png)

VDC 80-column BASIC desktop:

![VDC 80-column BASIC desktop](screenshots/vdc-basic.png)

The Rocky Horror Show, staircase scene:

![The Rocky Horror Show staircase scene](screenshots/rocky-horror-staircase.png)

The Rocky Horror Show, room scene:

![The Rocky Horror Show room scene](screenshots/rocky-horror-room.png)

LUDO:

![LUDO game board on the VDC display](screenshots/ludo.png)

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
  VDC 80-column text and standard 640x200 bitmap display. VDC interlace modes
  remain unfinished.
- Host keyboard input, paste, selectable joystick ports and 1351 mouse input,
  CIA timers/TOD/serial/FLAG interrupts,
  and VIC raster IRQ handling.
- Shift+C= character-set switching in both 40- and 80-column modes; an
  Advanced keyboard-map dialog shows host mappings for RUN/STOP, RESTORE,
  and other keys.
- Audible three-voice 8580 SID synthesis through SDL3, including the standard
  waveforms, envelopes, voice routing, and a basic filter approximation.
- Function-key conventions: F1 host input-port swap, F4 screenshot, F5 reset, F6 GIF capture, F7
  pause, F8 monitor, F9 options overlay, F10 40/80 switch, F11 fullscreen,
  F12 quit.
- An in-window function-key reminder in both display windows and an About
  dialog in General showing the emulator version and build commit. General
  also has a persistent 40/80 key setting for the default VIC/VDC output.
  With separate windows, F10 brings the selected display forward where the
  window manager permits; focusing either window selects and remembers it
  for the next launch.
- With Tinker enabled, Advanced > VDC RAM selects 64K (C128DCR default) or
  16K fitted video RAM. The setting is saved; VDC register 28 separately
  selects 16K/64K addressing, including the address-line mapping when that
  mode differs from the fitted RAM. Reset the emulated machine after changing
  the fitted size.
- Compact options overlay that reads and writes `1986.conf`, with an optional
  second virtual IEC drive, separately selected disk image/unit, and
  per-entry remembered file-picker directories.
- VICE's 8502/6510 core and a reused Z80 core wired to the C128 bus.
- A fast virtual IEC drive that reads and writes D64, D71, and D81 images,
  supporting `DIRECTORY`, `LOAD`/`DLOAD`, `SAVE`/`DSAVE`, and DOS `SCRATCH`/
  `RENAME` commands with status errors. It also loads standalone `.prg` files
  as read-only single-file media.
- Native C128 generic `.crt` cartridges and raw 8/16/32 KiB external
  function-ROM `.bin` images (also repeated 64 KiB EPROM dumps), with live
  Media-overlay insertion/ejection and `--cart` startup loading. C64-only and
  bank-switched cartridges are not yet supported.
- Optional U36 internal function-ROM slot for raw 8/16/32 KiB `.bin`/`.rom`
  images, selected from Media when Tinker is enabled and restored at launch.
- Tests for the CPU, MMU, CIA/SID, VIC graphics and sprites, configuration,
  GIF encoder, disk formats, KERNAL IEC traps, and virtual-drive channels.

Tape playback, further write-side DOS commands, true cycle-level 1571 emulation,
high-fidelity SID filter/combined-waveform emulation, CP/M mode, and several
accuracy features remain unfinished. The virtual drive does not emulate 1571
or 1581 hardware; it and the future true 1571 will share only the media/image
layer. The first independent 1571CR slice now has a 2K RAM/32K ROM bus map,
reset/interrupt vectors, a standalone NMOS 6502 instruction core, and two
6522 VIAs with port, timer, and IRQ handling, plus the MOS5710's limited CIA
serial/interrupt registers. It does not yet have FDC/FDC2, mechanism timing,
or physical IEC, so it cannot service disks.
Advanced > Real Disk Drive is still a saved preference: while the
hardware backend is pending, the fast virtual drive stays active. With that
preference On, Media exposes a hardware type per drive (1571CR or future 1581);
selecting 1581 does not imply that its hardware is emulated.
The bottom bar shows a separately labeled activity LED for each enabled drive,
in both the 40-column and 80-column windows. For now these follow the active
virtual drive's disk and IEC transfers; true-drive hardware LED state will be
connected when the physical backend becomes operational.

See [DEVELOPMENT.md](Development.md) for technical notes and
[ROADMAP.md](ROADMAP.md) for the forward plan.

## ROMs

1986 does not include Commodore machine or drive ROM images in the repository
or release packages, to avoid redistributing third-party copyrighted material.
Users must obtain compatible ROMs themselves from a source they are authorized
to use. Place them in `roms/` or select a ROM directory with `--rom`; see
[roms/README](roms/README) for the expected filenames and layout.

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

Tagged releases provide Fedora RPM, Debian DEB, Windows portable ZIP,
macOS app bundles for Apple Silicon and Intel, and a Linux Flatpak bundle.
They use the icon derived from `1986_logo.png`; machine ROMs are not bundled.

## Run the tests

```bash
make -C tests check
```

## Controls

F1 swap host joystick port, F4 screenshot, F5 reset, F6 GIF capture, F7 pause, F8 monitor, F9 options,
F10 40/80-column switch, F11 fullscreen, F12 quit. See
[CONTROLS.md](CONTROLS.md).

## Usage

See [USAGE.md](USAGE.md) for command-line options, configuration, and ROM
layout.
