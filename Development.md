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

## CPU: 8502 (6502-like)

`cpu.c` is a compact, binary-mode interpreter covering the standard opcode
set (loads/stores, transfers, stack, logic/arithmetic, shifts, branches,
JMP/JSR/RTS/RTI/BRK, flag ops). Decimal mode is stubbed. Undocumented opcodes
are treated as 2-cycle NOPs. The intended path is to replace/extend this with
the reference core from VICE's `src/6510core.c` (`src/mos6510.h`), keeping the
`CpuBus` read/write interface so the MMU/memory wiring is unchanged.

## Status: boot test

With a real C128DCR ROM set (`roms/kernal.bin` + `roms/basic.bin`), the
scaffold loads and splits both 32K dumps and the 8502 executes the reset
vector, runs the KERNAL entry and reaches BASIC's address space (PC=$6101).
It is not a stable boot yet: the compact 8502 subset (which treats
undocumented/omitted opcodes as NOPs) and the simplified MMU config are not
yet complete enough to keep the KERNAL's control flow intact. That is the
first real milestone below.

```bash
SDL_VIDEODRIVER=dummy C128_BOOT_TRACE=1 ./1986 --rom roms
```

## Roadmap

1. **Boot the KERNAL** — port the full VICE 8502 core (`src/6510core.c` /
   `src/mos6510.h`) behind the existing `CpuBus` interface and wire the C128
   MMU banking (see VICE `src/c128/c128mem.c` + `c128mmu.c`) so the KERNAL
   boots to the READY prompt.
2. **VIC-IIe raster** — replace the test pattern with a per-line renderer
   reading screen RAM, colour RAM, and character ROM.
3. **CIA timers + IRQs** — drive raster IRQs and the keyboard scan.
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
