# 1986 - Development

This file describes the source layout and the main emulation seams of the
working Commodore C128DCR core. User-facing status is in
[`docs/STATUS.md`](docs/STATUS.md), while planned work is tracked in
[`ROADMAP.md`](ROADMAP.md).

## Architecture

The project follows the sibling emulators (1983, 1984, 1985). A single C
source tree builds one SDL3 application, with tests under `tests/`.

```
src/
  main.c      - SDL3 event loop, frame pacing, function-key dispatch
  display.*   - SDL3 window/renderer/texture + CRT controls
  overlay.*   - F9 options overlay (General/Media/Advanced)
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
  leds.*      - drive and 8502/Z80 activity/frequency footer
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

The product target remains the C128 platform, not a general-purpose C64
emulator. There is nevertheless an experimental development path for the
C128's own C64 personality. Like VICE x128, it does not instantiate a second
machine: `$D505` bit 6 changes the ROM/PLA personality while the same 8502,
VIC-IIe, SID, CIAs, RAM, and IEC devices continue running. The implementation
includes the C64 processor-port PLA map, C64 BASIC/KERNAL ROM windows, the
lower character-ROM bank, bank-0 color RAM, VIC-IIe `$D030` fast mode, and C64
KERNAL IEC trap addresses.

The path is guarded by `c64_test_mode`, defaults to disabled, and additionally
requires the optional C64 BASIC and KERNAL ROMs. With the gate disabled,
`$D505` C64 requests are still rejected with the existing one-shot
notification. The Advanced toggle is intentionally temporary: after shared
C128 hardware has been validated with C64 software and C128-enhanced programs,
the UI can be removed without deleting the dormant, default-disabled core.
General C64 cartridge compatibility is not part of this work.

CP/M is a working native C128 mode. Reset begins on the Z80, the authentic
reset BIOS installs the common-RAM handshake, and `$D505` transfers the bus
between the Z80 and 8502. See [`docs/Z80-CPM.md`](docs/Z80-CPM.md) before
changing that path or adding native Z80 code.

## Reuse from siblings

- **Z80** (`z80.c`, `z80.h`, `z80dis.c`) originated in the sibling projects.
  It is self-contained and exposes a `Z80Bus` callback interface wired in
  `c128.c`. C128 memory/I/O banking, CPU ownership, shared peripheral timing,
  and the CP/M handoff live on the C128 side of that seam.
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

## Status: BASIC and CP/M boot

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
- CIA TOD clocks receive one 50 Hz mains pulse per PAL frame, independently of
  8502 fast mode. TOD alarms, serial shift completion, CNT pin edges, and FLAG
  falling edges feed the same interrupt mask/status path. External diagnostic
  harness loopbacks are not wired by default.
- The VIC-IIe raster IRQ: `$D012` compare, `$D019` status, `$D01A` mask.
- Both IRQ sources wired to the CPU, driving the KERNAL's 50 Hz main loop.

At hardware reset, `$D505` gives the bus to the Z80 rather than the 8502. The
private 4 KiB reset BIOS at Z80 `$0000` initializes the machine, copies the
dual-CPU trampoline to common RAM at `$FFD0`, and enters its Z80 half at
`$FFE0`. That half writes `$B1` to I/O port `$D505`; the scheduler stops the
Z80 at that instruction boundary and begins the waiting 8502 KERNAL. A CP/M
boot sector later uses the 8502 half at `$FFD0`, which writes `$B0` to `$D505`
and returns ownership to the Z80. Full invariants and address maps are recorded
in [`docs/Z80-CPM.md`](docs/Z80-CPM.md).

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
formatting, and other DOS write commands are not yet implemented. The command
channel accepts `S:pattern` (SCRATCH) and `R:new=old` (RENAME) for unlocked
SEQ/PRG/USR root-directory entries on all three formats. SCRATCH updates the
BAM and reports the removed-file count; both commands use the same atomic
write-back and external-change guard as SAVE. The Advanced overlay's Real Disk
Drive switch is a persisted selection for the ROM-backed 1571 backend when
the required DOS ROM is present.
An optional second fast virtual drive owns a separate image and unit. The
logical IEC callbacks fan out attention/data to both devices, and only the
addressed unit answers. The Advanced > Second Drive switch disconnects it
without forgetting its image or unit; configuration loading and Media unit
cycling prevent #8-#11 address collisions. The F9 overlay is a compact top
panel, following the sibling emulators' layout.

The C128 KERNAL's burst-mode flag is cleared while this command-level backend
is active, keeping transfers on the trapped byte routines. The true 1571's
fast-serial CIA shift-register endpoint is not connected yet.

The independent `Drive1571Cr` core now owns a 2K mirrored RAM, 32K DOS ROM,
reset/interrupt vectors, an NMOS 6502 instruction executor, and two 6522 VIAs.
The VIAs have port direction/readback, T1/T2 counters, control-line edges, and
IRQ propagation to the drive CPU. Their timing is at instruction boundaries;
shift-register and sub-instruction bus timing are still absent. The MOS5710's
limited CIA-like SDR/ICR/CRA registers use the existing CIA model, following
VICE's partial 1571CR handling. Its extra FDC2 registers and the WD1770 have
decoded hooks only; MFM is not yet connected. With the Advanced
gate on, a present 1571CR ROM executes in raster-line slices at the drive's
1 MHz clock (or 2 MHz when VIA1 PA5 selects it), with instruction overshoot
carried between slices. CIA2 PA3-PA7 and VIA1 PB0-PB4/PB7 now exchange slow
IEC ATN/CLOCK/DATA and ATNA signals through an open-collector bus, including
the VIA1 CA1 ATN edge. `C128_1571_TRACE=1` reports ROM PC, cycles, VIA ports,
and IEC levels every 50 frames. The opt-in true-drive path runs its DOS ROM and
slow IEC directly rather than KERNAL traps. VIA2 presents decoded D64/D71 GCR
sectors and accepts standard sector writes through its CB2 write gate and PA
byte latch. On gate close, head/side change, or eject, checksum-valid sectors
are decoded and atomically persisted. An unsuccessful flush leaves old media
attached; arbitrary raw/protection tracks cannot be stored in D64/D71.
When enabled with 1571CR selected for both drives, a second independent
`Drive1571Cr` shares the same open-collector IEC ATN/CLOCK/DATA lines. Each
VIA1 senses its own #8-#11 address straps and the common ATN edge, while the
drives keep separate CPU clocks, media, GCR writes, LEDs, and activity/audio
history. The visual monitor stacks Drive 2 above Drive 1. Disabling Drive 2
releases its IEC outputs after pending writes have been flushed. Because the
KERNAL traps and ROM-level IEC cannot be mixed, selecting 1581 for either
enabled drive falls back to the fast virtual pair at the next launch.
The core is a sibling of the command-level `VirtualDrive`, never behind its
interface; the modes will share only neutral disk-image/media code. The
Advanced real-drive gate selects this backend after restart when its DOS ROM
is present. Media stores 1571/1581
hardware type independently for each drive; 1581 is only a future selection.

The cassette implementation in `tape.c` follows VICE 3.10's distinction
between TAP and T64. TAP v0/v1/v2 stores timed flux gaps: the 8502 `$01`
port controls motor and reads the Play switch, while each completed pulse
drives CIA1 FLAG. The optional audio monitor mixes the TAP signal's
positive/negative halfwaves into SID PCM; the visual monitor plots recent
gap lengths and transport status. T64 instead contains file records, not a
recorded signal: two C128 KERNAL tape routines are trapped only while a T64
is mounted, then restored for TAP playback/ejection. T64 has no waveform or
audio; recording to either format is not implemented.

Visual check (saves a PPM at frame 60):
```bash
SDL_VIDEODRIVER=dummy C128_SAVE_PPM=/tmp/boot.ppm ./1986 --rom roms
```

For SID debugging, `C128_SID_TRACE=1` prints register, envelope, sample-count,
and output-energy snapshots every ten frames. SDL3's disk audio driver can
capture the playback stream with `SDL_AUDIO_DRIVER=disk` and
`SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/sid.raw`.

## Roadmap

The maintained milestone list is [`ROADMAP.md`](ROADMAP.md). Keeping the plan
there avoids the implementation guide becoming stale as completed features
move into [`docs/STATUS.md`](docs/STATUS.md).

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
