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
- [x] Fast virtual IEC drive with D64 `DIRECTORY`, `LOAD`, and `DLOAD`.
- [x] Live D64 replacement/ejection from the Media Overlay.
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
- [ ] Add the C128-specific keys (40/80 column toggle, `HELP`, `CAPS`, `ALT`,
  `ESC`, `TAB`, `-`, `=`, `@`, `£`, etc.).
- [ ] Complete host-layout translation and keyboard auto-repeat behavior.

**Done when.** The full native C128 keyboard is available from common host
layouts without relying on emulator-only shortcuts.

---

## Milestone 3 — Native C128 PLA accuracy  `[ ]`

**Goal.** Finish the 8502 `$00`/`$01` port and remaining native C128 memory
visibility rules without introducing a partial C64 personality.

- [x] `data_read = (data & dir) | ~dir` decoded.
- [x] Low bits select the CPU/VIC colour-RAM banks at `$D800`.
- [x] Native MMU character-ROM visibility at `$D000-$DFFF`, allowing BASIC
  7.0 `CHAR` to fetch real glyph data in bitmap mode.
- [ ] Chargen select: `$01` bit 6 (`mem_update_chargen(pport.data_read & 0x40)`)
  selects the chargen address.
- [x] Reject `$D505` C64-mode requests with a one-shot user notification and
  recover through the native C128 reset path.

**Done when.** Native chargen and colour-RAM visibility track the KERNAL's
`$01` writes, and unsupported mode requests cannot leave partial MMU state.

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
- [ ] Timer B (latch, cascade from timer A, `$DC0F`/`$DD0F`).
- [ ] Time-of-day (TOD) alarm.
- [ ] Serial shift register (SDR) and the FLAG line.
- [x] CIA2 port A VIC-bank bits.
- [ ] CIA2 RS-232 behavior.

**Done when.** Timer B, TOD, serial/FLAG, and CIA2 I/O behave while preserving
the already-working timer-A-driven KERNAL loop.

---

## Milestone 6 — VDC 8563 (80-column)  `[ ]`

**Goal.** Complete the working 80-column display with high-resolution modes
and more accurate VDC behavior.

- [x] VDC register file, update-address counter, block fill/copy, and 64K
  internal video RAM for the C128DCR.
- [x] 80x25 text rendering, attribute colours/reverse, and cursor blink.
- [x] Alternative VIC/VDC output in unified and dual-window display modes.
- [x] F10 display switching with the last active 40/80 mode persisted across
  application restarts and focused correctly in dual-window mode.
- [ ] Remaining attribute effects: flash, underline, and alternate charset.
- [ ] 640x200/400 bitmap and interlace modes.
- [ ] VDC timing, ready/busy status, and scan timing accuracy.

**Done when.** Native VDC text, bitmap, and interlace software renders with
accurate register/status timing.

---

## Milestone 7 — SID audio  `[ ]`

**Goal.** Three-voice SID render → SDL3 audio stream.

- [x] SID register address decoding and storage at `$D400-$D41F`.
- [ ] 6581/8580 oscillators, ADSR, filter, volume at `$D400-$D41F`.
- [ ] Drive the SDL3 audio callback from the 8502 frame cadence.

**Done when.** Music/SFX play.

---

## Milestone 8 — Virtual drive and true 1571  `[ ]`

**Goal.** Provide a convenient fast virtual drive and an independent,
cycle-level 1571 implementation without conflating their interfaces.

- [x] D64 directory parsing and fixed-width CBM DOS directory stream.
- [x] Fast virtual IEC device with KERNAL traps and channel lifecycle.
- [x] Virtual-drive `LOAD`/`DLOAD` and DOS error handling (#31).
- [x] Live D64 eject/insert from the Media Overlay, including persisted media
  state and immediate `DIRECTORY` visibility after a swap (#44).
- [ ] Virtual-drive `SAVE` and write-side DOS commands.
- [ ] D71 and D81 media formats.
- [ ] True integrated 1571: drive CPU, 2K RAM, DOS ROM, CIA/VIA/FDC,
  mechanism timing, line-level IEC, and fast serial (#30).

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

1. **Core accuracy:** native PLA chargen selection, VIC-IIe bad-lines/raster
   effects, and CIA timer-B/TOD/serial completion.
2. **Audio:** SID voices, envelopes, filter, and SDL3 output.
3. **Storage formats and writes:** virtual-drive `SAVE`, DOS write commands,
   D71, and D81.
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
