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

## Status: boots to the BASIC banner

With a real C128DCR ROM set (`roms/kernal.bin`, `roms/basic.bin`,
`roms/chargen.bin`), the 8502 executes the reset vector (`$FF3D`), runs the
KERNAL cold start (MMU init at `$D500`), and BASIC 7.0 boots and renders its
banner:

```
COMMODORE BASIC V7.0  122365 BYTES FREE
(C)1986 COMMODORE ELECTRONICS, LTD.
(C)1977 MICROSOFT CORP.
ALL RIGHTS RESERVED
```

The KERNAL's IEC serial-bus (disk) routines are intercepted with ROM traps
(`cpu_install_serial_traps`, mirroring VICE's `serial_trap_ready`) so the boot
does not block on the serial port. After the banner the boot reaches the
KERNAL's screen-editor main loop, which runs the 50 Hz IRQ correctly, but over
many frames the stack corrupts: an `RTI` eventually returns to a bad address
(`$081B`, an empty bank) and the CPU fetches a `0x00` (BRK), dropping into the
C128 machine monitor (`BREAK`). The corruption comes from the KERNAL's main
loop needing the full CIA keyboard scan / VIC raster hardware (currently
stubbed), not just the CPU + memory. Reaching `READY.` requires implementing
the CIA keyboard scan and VIC raster IRQ so the editor loop runs without
drifting the stack.

Visual check (saves a PPM at frame 60):
```bash
SDL_VIDEODRIVER=dummy C128_SAVE_PPM=/tmp/boot.ppm ./1986 --rom roms
```

## Roadmap

1. **Boot to BASIC READY** — implement the CIA keyboard scan and VIC raster
   IRQ so the KERNAL's screen-editor main loop runs without corrupting the
   stack (currently an `RTI` returns to a bad bank and the monitor appears).
2. **VIC-IIe raster** — per-line raster/IRQ timing and sprite/bitmap modes.
3. **CIA timers + IRQs** — full timer/port emulation and the keyboard matrix.
4. **VDC 8563** — render the 80-column framebuffer.
5. **SID audio** — three-voice render + SDL3 audio stream.
6. **1571 drives** — disk images (D64/D81), the C128's fast serial.
7. **CP/M mode** — switch the bus to the Z80 and map the CP/M RAM bank.
8. **Media / capture / polish** — snapshots, more keyboard matrix, full
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
