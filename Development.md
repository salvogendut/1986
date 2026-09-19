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
  vic.*       - MOS 8564 VIC-IIe (40-column) — test pattern for now
  vdc.*       - MOS 8563 VDC (80-column) register file
  cia.*       - MOS 6526 CIA1/CIA2 register file
  sid.*       - MOS 6581/8580 SID register file
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
  test_config.c - config roundtrip
  test_gifcap.c - GIF encoder output
```

## Reuse from siblings

- **Z80** (`z80.c`, `z80.h`, `z80dis.c`) is copied verbatim from 1984. It is
  self-contained and exposes a `Z80Bus` callback interface wired in
  `c128.c`. The C128's CP/M mode uses a different Z80 <-> memory banking than
  the CPC, so the bus callbacks are the seam to adjust.
- **display, overlay, gifcap, leds, notify** follow the same conventions as
  1984; the C128-specific constants (`C128_SCREEN_W/H`) are defined in
  `display.h`.
- **Function keys** match the siblings: F4 screenshot, F5 reset, F6 capture,
  F8 monitor, F9 overlay, F10 pause, F11 fullscreen, F12 quit.

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
select the CPU/VIC colour-RAM banks (`$D800`).

**Cursor blink / keyboard scan (blocked).** The KERNAL drives its 50 Hz main
loop from the VIC raster IRQ (`$D01A=0x01`, `$D012=0xFF`). The raster IRQ
machinery (`vic_tick`, `cpu_step_budget`) is in place, but firing it makes the
KERNAL's IRQ handler overflow the stack (`SP=0x00` -> `$0000`) after a few
frames — the same IRQ-handler imbalance that blocked `READY.` earlier. Because
SCNKEY (keyboard scan) and the cursor update run in that IRQ handler, input and
the blinking cursor are blocked until the handler runs without corrupting the
stack. This needs the IRQ handler to dispatch correctly against the CIA/VIC
status registers (roadmap item 1).

Visual check (saves a PPM at frame 60):
```bash
SDL_VIDEODRIVER=dummy C128_SAVE_PPM=/tmp/boot.ppm ./1986 --rom roms
```

## Roadmap

1. **Fix the IRQ handler stack imbalance** so the 50 Hz VIC raster IRQ can run
   the KERNAL's cursor update and SCNKEY without overflowing the stack — this
   unblocks the blinking cursor and host keyboard input at `READY.`.
2. **Keyboard input** — wire host keys into the C128 keyboard matrix so
   commands can be typed at the `READY.` prompt.
3. **PLA / GO 64** — chargen select (bit 6 of `$01`) and the full C64-mode
   memory model for GO 64 (`$01` -> `mmu_set_config64` in VICE).
4. **VIC-IIe raster** — per-line raster/IRQ timing and sprite/bitmap modes.
5. **CIA timers + IRQs** — full timer/port emulation (timer B cascade, TOD,
   serial).
6. **VDC 8563** — render the 80-column framebuffer.
7. **SID audio** — three-voice render + SDL3 audio stream.
8. **1571 drives** — disk images (D64/D81), the C128's fast serial.
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
