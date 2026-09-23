# Current status and scope

1986 boots the C128 KERNAL and BASIC 7.0 to a usable `READY.` prompt. It also
boots a CP/M Plus system disk through the authentic C128 Z80 reset BIOS and
reaches the `A>` prompt. The emulator is under active development: the items
below describe working paths, not a claim of cycle-exact compatibility.

## CPU and memory

- VICE-derived 8502/6510 instruction core with native C128 MMU and PLA maps.
- 1 MHz and 2 MHz 8502 operation, including VIC-IIe `$D030` changes.
- Z80 execution at its effective 2 MHz rate, with `$D505` CPU ownership,
  native Z80 memory/I/O mapping, and shared-RAM handoff to and from the 8502.
- CP/M Plus boot from a C128 CP/M system disk.
- Two 64 KiB system RAM banks and selectable 16/64 KiB VDC RAM. The C128DCR
  default is 64 KiB.
- White 8502 and blue Z80 footer indicators. The 8502 label follows its live
  1/2 MHz state; each lamp brightens only while that processor is clocked.

The reset and cross-CPU protocol is documented separately in
[Z80-CPM.md](Z80-CPM.md) so future native Z80 work preserves the hardware
handoff rather than bypassing it with host traps.

## Video

- VIC-IIe 40-column text and bitmap modes, eight hardware sprites, standard
  and multicolor pixels, expansion, priority, collision latches, raster IRQs,
  and common per-line register effects.
- VDC 80-column text and standard 640x200 bitmap output, including fitted
  16/64 KiB RAM and register-controlled addressing behavior.
- Unified display or separate VIC/VDC windows. The selected display is focused,
  raised where the window manager permits, and restored at the next launch.
- VDC 400-line interlace and some advanced timing/effects remain incomplete.

## Audio and I/O

- Three-voice 8580 SID output through SDL3, with waveforms, envelopes, routing,
  and a basic filter approximation.
- CIA timers, TOD, serial/FLAG interrupt paths, VIC raster IRQs, keyboard matrix,
  clipboard paste, joysticks, gamepad input, and 1351 mouse input.
- Shift+C= character-set switching, RUN/STOP and RESTORE mappings, plus an
  on-screen host keyboard map.
- TAP pulse playback and T64 file-container loading. Tape recording is not
  implemented.
- SID analog filtering and combined-waveform behavior remain approximations.

## Media

- Fast virtual drives read and write D64, D71, and D81 images and can expose a
  standalone PRG as read-only single-file media.
- Two independently addressed IEC units, normally #8 and #9.
- Native C128 generic CRT cartridges and raw 8/16/32 KiB external function ROMs.
- Optional U36 internal function-ROM slot for raw 8/16/32 KiB images.
- Experimental ROM-backed 1571CR execution with slow IEC and D64/D71 GCR sector
  reads/writes. See [DRIVES.md](DRIVES.md) for its safety rules and limitations.

## Desktop interface

- Compact F9 options overlay with General, Media, and Advanced sections.
- Persistent configuration and per-picker last-used directories.
- PPM screenshots, built-in GIF capture, fullscreen/scaling, function-key
  reminder, notifications, and a monitor/disassembler.
- Per-drive activity LEDs and optional drive/tape audio and visual monitors.

## C128-first product scope

The finished product targets native C128 software and CP/M, not broad C64
compatibility. **Advanced > C64 Test Mode** is an opt-in development facility.
It lets `GO64` enter the C128's own C64 personality while retaining the same
8502, VIC-IIe, SID, CIAs, RAM, and IEC hardware. This helps exercise shared
components and software such as C128-enhanced titles that begin in C64 mode.

The test gate defaults to Off and requires optional C64 BASIC and KERNAL ROMs.
With it Off, `GO64` remains deliberately unavailable. General C64 cartridge
compatibility and becoming a replacement for a dedicated C64 emulator are not
release goals.

## Important limitations

- Emulation is not cycle exact; more VIC-IIe bad-line/raster validation remains.
- VDC interlace and advanced modes remain incomplete.
- The SID filter and combined waveforms are approximate.
- The real 1571 path lacks burst serial, WD1770/FDC2 MFM, exact mechanism
  timing, and nonstandard raw/protection-track persistence.
- The virtual drive does not execute uploaded drive code through `M-E`.
- Snapshots, tape recording, and some keyboard/layout polish remain future work.

See the [roadmap](../ROADMAP.md) for milestone detail and
[usage guide](../USAGE.md) for operating instructions.
