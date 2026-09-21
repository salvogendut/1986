# 1986 - Development

This file describes the scaffolding, its module layout, and the roadmap toward
a bootable Commodore C128DCR emulator.

## Architecture

The project follows the sibling emulators (1983, 1984, 1985). A single C
source tree builds one SDL3 application, with tests under `tests/`.

```
src/
  main.c      - SDL3 event loop, frame pacing, function-key dispatch
  display.*   - SDL3 window/renderer/texture + CRT controls
  overlay.*   - F9 options overlay (General/Video/Capture)
  config.*    - 1986.conf load/save
  kbd.*       - C128 keyboard matrix + SDL scancode mapping
  c128.*      - machine object: owns mem/cpu/z80/vic/vdc/cia/sid
  cpu.*       - MOS 8502 (6502-like) interpreter
  mem.*       - C128 memory map (RAM banks + ROM + I/O window)
  mmu.*       - C128 MMU registers ($D500 block, $00/$01 port)
  vic.*       - MOS 8564 VIC-IIe (40-column text, bitmap, and sprites)
  vdc.*       - MOS 8563 VDC (80-column) register file
  cia.*       - MOS 6526 CIA1/CIA2 register file
  sid.*       - MOS 8580 SID oscillator/envelope/filter and PCM output
  disk_image.* - D64/D71/D81 geometry, BAM/directory, atomic PRG saving
  virtual_drive.* - fast logical IEC device used by KERNAL ROM traps
  drive.*     - machine-facing media/virtual-drive holder
  z80.*       - cycle-stepped Z80 (reused from 1983/1984/1985) for CP/M
  z80dis.*    - Z80 disassembler (reused)
  paste.*     - clipboard text -> keyboard injection
  monitor.*   - F8 register monitor window
  gifcap.*    - F6 built-in GIF89a encoder (reused from 1984)
  leds.*      - drive-activity LED bar (reused from 1984)
  notify.*    - desktop/screen notifications (reused from 1984)
tests/
  test_cpu.c  - 8502 sanity (adds, branches, stores)
  test_mmu.c  - MMU register behaviour
  test_sid.c  - SID oscillator, envelope, routing, and audio samples
  test_config.c - config roundtrip
  test_gifcap.c - GIF encoder output
  test_d64.c  - D64 directory bytes + virtual IEC channel lifecycle
  test_d64_save.c - D64 write-back, replacement, and error cases
  test_disk_formats.c - D71/D81 two-sided BAM, directory, save/load cases
```

## Machine-mode scope

The emulation target is the native C128 platform. C64 compatibility mode is
not an incremental MMU configuration: it brings a separate ROM personality,
PLA map, cartridge behavior, startup path, and compatibility surface. It is
therefore intentionally unsupported. `$D505` bit 6 requests are latched for a
one-shot user notification and rejected; the KERNAL reset-vector path then
returns the machine safely to native C128 mode. The boundary remains explicit
so support could be added later without maintaining a misleading partial mode.

CP/M remains in scope as a native advertised use of the C128 hardware.

## Reuse from siblings

- **Z80** (`z80.c`, `z80.h`, `z80dis.c`) is copied verbatim from 1984. It is
  self-contained and exposes a `Z80Bus` callback interface wired in
  `c128.c`. The C128's CP/M mode uses a different Z80 <-> memory banking than
  the CPC, so the bus callbacks are the seam to adjust.
- **display, overlay, gifcap, leds, notify** follow the same conventions as
  1984; the C128-specific constants (`C128_SCREEN_W/H`) are defined in
  `display.h`.
- **Function keys** follow the siblings where possible: F4 screenshot, F5
  reset, F6 capture, F7 pause, F8 monitor, F9 overlay, F10 C128 40/80-column
  display switch, F11 fullscreen, F12 quit.

## CPU: 8502 (6502-like) — VICE core

The 8502 is VICE's `6510core.c` (the MOS 6510/8502 instruction interpreter),
ported behind the project's `CpuBus` seam:

- `src/vice/` holds the ported VICE core: `6510core.c`, `6510core.h`,
  `mos6510.h`, `interrupt.h`/`interrupt.c`, and a thin VICE-compat base
  (`types.h`, `log.h`, `debug.h`, `alarm.h`, `clkguard.h`, `machine.h`,
  `mem.h`, `monitor.h`, `traps.h`, ...). These keep VICE's original copyright
  and GPLv2+ headers (see Development.md and src/vice/).
- `src/cpu.c` is the host: it defines the VICE base globals, the memory
  dispatch (`LOAD`/`STORE` routed through `c128_mem_read`/`c128_mem_write`),
  the machine/monitor/alarm stubs, and the `Cpu8502` wrapper. The core's
  `maincpu_mainloop` is bounded by a cycle budget so a frame can be stepped.
- Memory mapping follows VICE's C128 config-register semantics (raw `$D500`
  -> config index -> RAM/ROM per region) in `mem.c`.

## Status: boots to BASIC READY

With a real C128DCR ROM set (`roms/kernal.bin`, `roms/basic.bin`,
`roms/chargen.bin`), the 8502 executes the reset vector (`$FF3D`), runs the
KERNAL cold start, and BASIC 7.0 boots to the `READY.` prompt:

```
COMMODORE BASIC V7.0  122365 BYTES FREE
(C)1986 COMMODORE ELECTRONICS, LTD.
(C)1977 MICROSOFT CORP.
ALL RIGHTS RESERVED

READY.
```

This is driven by:
- The IEC serial-bus ROM traps (`cpu_install_serial_traps`, mirroring VICE's
  `serial_trap_ready`) so the boot doesn't block on the disk/serial bus.
- Correct MOS 6526 CIA semantics: `$DC0D` reads the interrupt flags and writes
  the mask, `$DC0E` is timer-A control; timer-A underflow sets ICR bit 0 and
  asserts the IRQ line when masked.
- The VIC-IIe raster IRQ: `$D012` compare, `$D019` status, `$D01A` mask.
- Both IRQ sources wired to the CPU, driving the KERNAL's 50 Hz main loop.

The CIA1 keyboard scan is wired (port A rows / port B columns). The 40x25
text renderer draws screen RAM, colour RAM and chargen. The 8502's `$00/$01`
port (the PLA) is decoded: `data_read = (data & dir) | ~dir`, and its low bits
select the CPU/VIC colour-RAM banks (`$D800`). Native C128 MMU CR bit 0 also
switches `$D000-$DFFF` between I/O and the C128 character-ROM bank, which is
required by BASIC 7's bitmap `CHAR` routine.

The VIC-IIe renderer also implements all eight hardware sprites. Sprite
pointers follow the active screen matrix (`$07F8` in the normal text layout,
`$1FF8` in BASIC graphics mode); data fetches use the CIA2-selected 16K window
inside the 64K RAM bank selected by `$D506`. Standard/multicolor pixels,
X/Y expansion, graphics priority, sprite ordering, and both collision/IRQ
latches are modeled. This is sufficient for BASIC 7 `SPRITE`, `SPRCOLOR`,
`SPRSAV`, and `MOVSPR` output.

## Disk-drive architecture

`virtual_drive.c` is a fast logical IEC device used by the patched KERNAL
routines. It implements device addressing, OPEN/CLOSE, LISTEN/TALK,
UNLISTEN/UNTALK, secondary channels, status responses, and D64/D71/D81
directory streams without running a drive CPU. It follows file-sector chains
and serves raw PRG streams (including their load address) for `LOAD` and `DLOAD`.
Missing files report DOS error 62 both on the IEC status byte and command
channel. `SAVE` and `DSAVE` buffer PRG data on IEC channel 1, then update the
D64, D71, or D81 BAM and directory on CLOSE. The image is written through a
temporary file and replaced atomically; errors leave the live image untouched.
`@:` requests replacement of an existing unlocked file. D71 uses the 1571
second-side BAM at 53/0;
D81 uses the 1581 header at 40/0 and BAM sectors at 40/1-2. The common IEC
path does not require a 1571 or 1581 DOS ROM. D81 partitions, REL files,
formatting, and other DOS write commands are not yet implemented.

The C128 KERNAL's burst-mode flag is cleared while this command-level backend
is active, keeping transfers on the trapped byte routines. A true 1571 will
instead provide the CIA shift-register endpoint needed by fast serial.

A future true `Drive1571` is a separate machine: it will run its own 6502 and
DOS ROM and connect through line-level IEC signals. It must not be placed
behind the command-level `VirtualDrive` interface. The two modes share only
neutral disk-image/media code.

Visual check (saves a PPM at frame 60):
```bash
SDL_VIDEODRIVER=dummy C128_SAVE_PPM=/tmp/boot.ppm ./1986 --rom roms
```

For SID debugging, `C128_SID_TRACE=1` prints register, envelope, sample-count,
and output-energy snapshots every ten frames. SDL3's disk audio driver can
capture the playback stream with `SDL_AUDIO_DRIVER=disk` and
`SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/sid.raw`.

## Roadmap

1. **Virtual drive DOS commands** — add remaining write-side DOS commands to
   the tested D64/D71/D81 logical IEC/media layer.
2. **True 1571** — implement the independent drive CPU, chips, mechanism and
   line-level IEC connection.
3. **Native PLA accuracy** — finish chargen selection and native C128 memory
   visibility without adding the separate C64 personality.
4. **VIC-IIe raster** — per-line register effects, border opening and the
   remaining character/bitmap modes.
5. **CIA timers + IRQs** — full timer/port emulation (timer B cascade, TOD,
   serial).
6. **VDC 8563** — render the 80-column framebuffer.
7. **SID audio** — three-voice 8580 render + SDL3 audio stream are working;
   analog filter and combined-waveform fidelity remain to be improved.
8. **1571 drives** — hardware-level emulation of the integrated drive and the
   C128's fast serial, separate from image-format support.
9. **CP/M mode** — switch the bus to the Z80 and map the CP/M RAM bank.
10. **Media / capture / polish** — snapshots, more keyboard matrix, full
   keyboard layout, real 2 MHz timing.

## Build and test

```bash
autoreconf -iv
./configure
make -j"$(nproc)"
make -C tests check
```

The headless smoke test runs with `SDL_VIDEODRIVER=dummy`:

```bash
SDL_VIDEODRIVER=dummy ./1986
```
