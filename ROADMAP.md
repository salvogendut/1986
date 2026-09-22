# 1986 — Roadmap

A work-in-progress Commodore C128DCR emulator in C + SDL3. It reuses the
function-key/overlay conventions of the sibling emulators (1983, 1984, 1985),
the Z80 core for CP/M mode, and VICE's 8502 (6510-core) instruction set.

This document is the forward plan. The current status and technical notes are
in [Development.md](Development.md); controls are in [CONTROLS.md](CONTROLS.md).

The roadmap targets the native C128 platform and eventual CP/M support. The
separate C64 compatibility personality is intentionally out of scope.

Checkboxes track progress: `[x]` = done, `[ ]` = pending.

---

## Current baseline — interactive native C128  ✅ `[x]`

The C128DCR boots to the BASIC 7.0 `READY.` prompt and runs interactive BASIC
programs in both its VIC-IIe 40-column and VDC 80-column environments:

```
COMMODORE BASIC V7.0  122365 BYTES FREE
(C)1986 COMMODORE ELECTRONICS, LTD.
(C)1977 MICROSOFT CORP.
ALL RIGHTS RESERVED

READY.
```

- [x] VICE 8502/6510 CPU core, C128 MMU banking, and native-C128 memory map.
- [x] Stable CIA timer-A/VIC raster IRQ path driving the KERNAL main loop.
- [x] Host keyboard, cursor, clipboard/CLI paste, and BASIC command entry.
- [x] VIC-IIe text, hires/multicolor bitmap, BASIC `CHAR`, and eight sprites.
- [x] VDC 80x25 text with attributes/cursor and selectable display output.
- [x] Persistent 40/80-column selection in unified and dual-window modes.
- [x] Fast virtual IEC drive with D64/D71/D81 `DIRECTORY`, `LOAD`/`DLOAD`,
  and `SAVE`/`DSAVE`.
- [x] Live disk-image replacement/ejection from the Media Overlay.
- [x] Explicit rejection of the out-of-scope C64 personality (`GO64`).

The remaining milestones focus on hardware completeness and accuracy rather
than reaching the first usable BASIC prompt.

---

## Milestone 1 — Stable 50 Hz IRQ-driven BASIC  ✅ `[x]`

**Goal.** Run the KERNAL's timer/raster-driven main loop without corrupting the
8502 stack.

- [x] CIA1 timer-A latch, counter, underflow, ICR/IMR, and IRQ line.
- [x] VIC `$D012` raster compare and `$D019`/`$D01A` status/mask behavior.
- [x] Combined CIA/VIC IRQ delivery to the 8502.
- [x] Stable blinking cursor, SCNKEY keyboard scanning, and interactive BASIC.

**Done.** BASIC remains usable with IRQs active and host keypresses are echoed
at `READY.` without the former stack overflow.

---

## Milestone 2 — Host keyboard input  `[ ]`

**Goal.** Complete the C128 keyboard beyond the working everyday subset.

- [x] Core 8x8 matrix map (letters, digits, common symbols, Shift/Ctrl,
  RETURN/SPACE/DEL, function keys, and cursor keys).
- [x] Matrix positions verified sufficiently for interactive BASIC programs.
- [x] Cursor keys and emulator function-key conventions.
- [x] Clipboard paste and deterministic `--paste`/`--paste-at` input.
- [x] Shift+C= upper/graphics versus upper/lowercase switching on VIC and VDC,
  with a Caps Lock host shortcut, RUN/STOP and RESTORE mappings, and an
  Advanced keyboard-map dialog.
- [ ] Add the C128-specific keys (40/80 column toggle, `HELP`, `CAPS`, `ALT`,
  `ESC`, `TAB`, `-`, `=`, `@`, `£`, etc.).
- [ ] Complete host-layout translation and keyboard auto-repeat behavior.

**Done when.** The full native C128 keyboard is available from common host
layouts without relying on emulator-only shortcuts.

---

## Milestone 3 — Native C128 PLA accuracy  `[ ]`

**Goal.** Finish the 8502 `$00`/`$01` port and remaining native C128 memory
visibility rules without introducing a partial C64 personality. On the
International/US model, `$01` bit 6 does not select the character-ROM half;
localized DIN/ASCII variants are a separate future scope decision.

- [x] `data_read = (data & dir) | ~dir` decoded.
- [x] Low bits select the CPU/VIC colour-RAM banks at `$D800`.
- [x] Native MMU character-ROM visibility at `$D000-$DFFF`, allowing BASIC
  7.0 `CHAR` to fetch real glyph data in bitmap mode.
- [x] VIC character-ROM visibility follows effective `$01` bit 2 and the
  `$D018` character address; RAM-defined character sets use the selected VIC
  bank when ROM is not mapped (#47).
- [ ] Verify remaining native PLA memory visibility and processor-port
  electrical/readback details against VICE's tests.
- [x] Reject `$D505` C64-mode requests with a one-shot user notification and
  recover through the native C128 reset path.

**Done when.** Native processor-port visibility and readback track the
KERNAL's `$00/$01` writes and remaining VICE hardware tests, without leaving
partial MMU state on unsupported mode requests.

---

## Milestone 4 — VIC-IIe raster, sprites, bitmap modes  `[ ]`

**Goal.** Cycle-accurate VIC-IIe: per-line raster, sprites, multicolour/bitmap.

- [x] 40x25 text renderer (screen RAM + colour RAM + chargen).
- [x] Raster IRQ registers (`$D012`/`$D019`/`$D01A`) + raster-line crossing
  check.
- [x] Hires and multicolor bitmap rendering with the correct per-cell colour
  sources; BASIC 7.0 graphics and `CHAR` output verified visually.
- [x] All eight standard/multicolor sprites, expansion, priority and banking.
- [x] Sprite collision latches and IRQs (`$D019` bits 1-2).
- [ ] Bad-lines, border opening, and per-raster register effects.

**Done when.** Sprites and bitmap/badline demos render correctly.

---

## Milestone 5 — CIA timers + full I/O  `[ ]`

**Goal.** Complete MOS 6526 emulation.

- [x] Timer A (latch, underflow → ICR bit 0 → IRQ line).
- [x] ICR/IMR semantics (`$DC0D` read = flags, write = mask).
- [x] Timer B Phi2/cascade counting, reload/one-shot, and ICR bit 1 on both
  CIAs; CIA2 delivers its masked interrupt as NMI (#49).
- [x] External CNT pin edges and CNT-driven/gated timer modes (no harness
  connected by default).
- [x] 50/60 Hz time-of-day (TOD) clock, latch, and alarm interrupt.
- [x] Serial shift register (SDR) input/output interrupts and FLAG falling-edge
  interrupt; external CIA-to-CIA wiring remains optional future work.
- [x] CIA2 port A VIC-bank bits.
- [ ] CIA2 RS-232 behavior.

**Done when.** CNT-driven timer modes, TOD, serial/FLAG, and CIA2 I/O behave
while preserving the already-working timer-A-driven KERNAL loop.

---

## Milestone 6 — VDC 8563 (80-column)  `[ ]`

**Goal.** Complete the working 80-column display with high-resolution modes
and more accurate VDC behavior.

- [x] VDC register file, update-address counter, block fill/copy, and 64K
  internal video RAM for the C128DCR.
- [x] Persistent Advanced 16K/64K VDC RAM selection (64K default), including
  physical address mapping. Register 28 selects an independent addressing mode,
  rather than reporting the fitted RAM size.
- [x] 80x25 text rendering, attribute colours/reverse, and cursor blink.
- [x] Programmable text row stride and raster/row geometry, plus flash,
  underline, and semigraphics character effects.
- [x] Alternative VIC/VDC output in unified and dual-window display modes.
- [x] F10 display switching with the last active 40/80 mode persisted across
  application restarts and focused correctly in dual-window mode.
- [x] Alternate-charset text attribute for upper/lowercase switching.
- [x] Standard 640x200 VDC bitmap mode with register and attribute colours,
  reverse video, and C128DCR 16-byte character slots.
- [x] 8568 revision status, approximate ready/busy and VBLANK, and read-only
  light-pen position registers.
- [ ] 640x400 VDC interlace and extended bitmap modes.
- [ ] Accurate VDC borders, smooth scrolling, address latching, and scan timing.
- [ ] Investigate BASIC 8 selecting 16K VRAM despite the 64K C128DCR model.

**Done when.** Native VDC text, bitmap, and interlace software renders with
accurate register/status timing.

---

## Milestone 7 — SID audio  `[ ]`

**Goal.** Three-voice SID render → SDL3 audio stream.

- [x] SID register address decoding and storage at `$D400-$D41F`.
- [x] Three 8580 voices, standard waveforms, ADSR, volume, voice routing,
  and approximate low/band/high-pass filter (#51).
- [x] Stream signed 16-bit mono PCM to SDL3 at the PAL SID clock cadence.
- [ ] Match reSID's analog filter, combined waveforms, and edge-case timing.

**Done when.** Native C128 music/SFX play with satisfactory 8580 fidelity.

---

## Milestone 8 — Virtual drive and true 1571  `[ ]`

**Goal.** Provide a convenient fast virtual drive and an independent,
cycle-level 1571 implementation without conflating their interfaces.

- [x] D64 directory parsing and fixed-width CBM DOS directory stream.
- [x] Fast virtual IEC device with KERNAL traps and channel lifecycle.
- [x] Virtual-drive `LOAD`/`DLOAD` and DOS error handling (#31).
- [x] Live D64 eject/insert from the Media Overlay, including persisted media
  state and immediate `DIRECTORY` visibility after a swap (#44).
- [x] Read-only standalone PRG loading from either Drive picker or `--disk`,
  with single-file directory listing and normal IEC `LOAD`/`DLOAD` (#80).
- [x] Virtual-drive `SAVE`/`DSAVE` to D64 with BAM/directory updates and
  atomic write-back (#53).
- [x] SCRATCH and RENAME command-channel operations on D64/D71/D81 root
  SEQ/PRG/USR files, with atomic write-back and DOS status (#57).
- [ ] Further write-side DOS commands (format, copy, etc.).
- [x] D71 and D81 image formats with two-sided BAM handling and PRG
  read/write support (#55).
- [ ] D81 partition navigation and REL-file operations.
- [ ] True integrated 1571: drive CPU, 2K RAM, DOS ROM, CIA/VIA/FDC,
  mechanism timing, line-level IEC, and fast serial (#30).
- [x] First 1571CR slice: independent ROM-backed 6502 core, mirrored 2K RAM,
  hardware address decoder, reset/interrupt vectors, and CPU/bus tests (#92).
- [x] Two 6522 VIAs with port direction, timers, control-line IRQs and drive-CPU
  IRQ propagation; shift-register and cycle-exact timing remain open (#92).
- [x] Partial MOS5710 CIA serial/interrupt registers and shared drive IRQ line
  (following VICE's limited 1571CR handling; extra FDC2 registers remain open).
- [x] Clock the 1571CR ROM alongside the C128 at 1/2 MHz and connect slow IEC
  ATN/CLOCK/DATA/ATNA between CIA2 and VIA1, with bus and ROM-probe tests (#94).
- [ ] Emulate WD1770/FDC2, mechanism and fast serial; replace virtual KERNAL
  traps with the hardware backend only once disk commands work reliably.
- [x] Media persists a per-drive hardware type when real-drive mode is selected;
  1581 is shown as future hardware rather than confused with D81 image support.
- [x] Separate, live Drive 1/Drive 2 activity LEDs in both display windows;
  connect the physical drive's LED latch when its backend takes over (#92).
- [x] Persisted Advanced > Real Disk Drive preference, default Off; On is
  marked pending and retains the virtual backend until true-drive support (#57).
- [x] Optional second fast virtual drive with independent image and a distinct
  IEC unit; compact, legible F9 overlay (#59).

**Done when.** Virtual mode is broadly useful and true-drive mode runs the DOS
ROM through emulated hardware.

---

## Milestone 9 — CP/M mode  `[ ]`

**Goal.** Switch the bus to the Z80 and map the CP/M RAM bank.

- [x] Z80 core (`z80.c`, reused from the sibling projects) wired to a `Z80Bus`.
- [ ] Step the Z80 when the MMU `$D507` Z80-enable bit is set.
- [ ] Map the 64K CP/M bank and route the Z80 memory/IO through the C128 bus.
- [ ] Provide the Z80 ROM traps the KERNAL uses to enter CP/M.

**Done when.** A CP/M disk boots.

---

## Milestone 10 — Media, capture, polish  `[ ]`

**Goal.** Snapshots, complete input, accurate 2 MHz timing, and desktop polish.

- [x] GIF capture (F6), PPM screenshots (F4), boot trace, one-shot PPM dump.
- [x] Persistent configuration overlay, screen/console notifications, and
  live media selection.
- [x] Persistent active display selection in unified and dual-window modes.
- [x] Generic native-C128 CRT and raw external function-ROM cartridge loading,
  including live Media attach/eject; bank-switched cartridges remain future work.
- [x] U36 internal function-ROM socket with Tinker-gated Media selection and
  persistent live attach/eject.
- [ ] Snapshots (VICE `.vsf` or a simple own format) for save/load of machine
  state.
- [ ] Full native C128 keyboard coverage, host layouts, and auto-repeat.
- [ ] 2 MHz fast-mode timing (`$D507`/`$01`) affects the raster and CIA.
- [ ] Cycle-exact raster/CPU interleave; run VICE's test programs.
- [ ] WebM capture, CRT shader options, gamepad input.
- [ ] Flatpak + packaging updates; CI.

**Done when.** The emulator is a daily-driver C128DCR.

---

## Remaining work at a glance

The main unfinished areas, grouped by likely development scale, are:

1. **Core accuracy:** remaining native PLA tests, VIC-IIe bad-lines/raster
   effects, CIA2 RS-232, and optional external harness wiring.
2. **Audio accuracy:** SID analog filter, combined waveforms, and edge-case
   timing beyond the working three-voice SDL3 output.
3. **Storage features:** further DOS write commands, D81 partitions, and
   REL-file operations.
4. **Large machine subsystems:** true cycle-level 1571 hardware and CP/M/Z80
   bus switching.
5. **Usability and validation:** snapshots, complete keyboard handling,
   accurate 2 MHz operation, cycle-exact tests, capture/gamepad options,
   packaging, and CI.

---

## Cross-cutting guidance

- **Reuse VICE.** Where a subsystem is hard (CIA, VIC-II, VDC, SID, 1571, PLA),
  port VICE's approach (its `core/`, `vicii/`, `vdc/`, `sid/`, `drive/`,
  `c128/` sources) rather than reverse-engineering from scratch. Keep VICE's
  GPLv2+ headers on anything ported.
- **Verify visually.** Use `C128_SAVE_PPM=<path>` + `C128_SAVE_FRAME=N` to dump
  frames, and `C128_BOOT_TRACE=1` to watch the CPU.
- **Keep it green.** `make -C tests check` must pass; the boot must stay stable.
