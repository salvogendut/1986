# 1986 — Roadmap

A work-in-progress Commodore C128DCR emulator in C + SDL3. It reuses the
function-key/overlay conventions of the sibling emulators (1983, 1984, 1985),
the Z80 core for CP/M mode, and VICE's 8502 (6510-core) instruction set.

This document is the forward plan. The current status and technical notes are
in [Development.md](Development.md); controls are in [CONTROLS.md](CONTROLS.md).

Checkboxes track progress: `[x]` = done, `[ ]` = pending.

---

## Current: boot to BASIC READY  ✅ `[x]`

The C128DCR boots to the BASIC 7.0 `READY.` prompt (stable, verified):

```
COMMODORE BASIC V7.0  122365 BYTES FREE
(C)1986 COMMODORE ELECTRONICS, LTD.
(C)1977 MICROSOFT CORP.
ALL RIGHTS RESERVED

READY.
```

- [x] VICE 8502/6510 CPU core, cycle-stepped and bounded per frame.
- [x] C128 MMU config-register banking.
- [x] IEC serial ROM traps.
- [x] PLA colour-RAM banking (`$01` port low bits).
- [x] CIA1 keyboard scan (matrix wired).
- [x] 40x25 text renderer (screen RAM + colour RAM + chargen).
- [x] CIA ICR/IMR + timer-A; VIC raster IRQ registers.

The single thing blocking interactive use (blinking cursor, host keyboard) is
the **IRQ-handler stack imbalance** (see Milestone 1). Everything below is
ordered roughly by "most visible / most foundational" first.

---

## Milestone 1 — Fix the 50 Hz IRQ handler (unblocks cursor + keyboard)  `[ ]`

**Goal.** The KERNAL's 50 Hz main loop runs from the VIC raster IRQ
(`$D01A=0x01`, `$D012=0xFF`). Firing it makes the KERNAL's IRQ handler at
`$FF17`/`$C190` overflow the stack (`SP=0x00` → `$0000`) after a few frames.

**Why it matters.** The cursor blink and SCNKEY (keyboard scan) both run inside
that IRQ handler. Until it runs cleanly, neither works. This is the same
imbalance that blocked `READY.` earlier — my READY fix worked by not firing the
IRQ.

**Approach.**
- [ ] Trace the IRQ handler's dispatch: which `$DC0D` (CIA1 ICR) / `$D019`
  (VIC status) values it reads and where a JSR/RTS pair fails to balance.
- [ ] Make the CIA1 ICR and VIC `$D019` return the exact values VICE's
  `read_ciaicr` / `vicii_read` produce for the current state.
- [ ] Port the relevant parts of VICE `core/ciacore.c`/`ciatimer.c` and
  `vicii/vicii-irq.c` (timer underflow → ICR bit → IRQ line; raster IRQ
  status/mask/line compare).
- [ ] Verify with `C128_BOOT_TRACE=1` that the CPU stays in the `READY.` loop
  (`PC=$C260`) for hundreds of frames with the IRQ firing.

**Done when.** The cursor blinks and a host keypress is echoed at `READY.`,
with no stack overflow.

---

## Milestone 2 — Host keyboard input  `[ ]`

**Goal.** Type at `READY.` and run BASIC commands.

**Status.** The C64/C128 8x8 matrix map in `kbd.c` (letters, digits, symbols,
Shift/Ctrl/RETURN/SPACE/DEL, F-keys, cursor, keypad) is in place. The CIA1 scan
is wired (port A rows / port B columns).

- [x] C64/C128 8x8 matrix map (letters, digits, symbols, Shift/Ctrl,
  RETURN/SPACE/DEL, F-keys, cursor, keypad).
- [ ] Verify the matrix positions against the KERNAL's SCNKEY conversion table
  so each key yields the correct screen code (a wrong `(row,col)` produced a
  `?SYNTAX ERROR` earlier).
- [ ] Add the C128-specific keys (40/80 column toggle, `HELP`, `CAPS`, `ALT`,
  `ESC`, `TAB`, `-`, `=`, `@`, `£`, etc.).
- [ ] Paste path (Ctrl+V) reusing the existing `paste.c`.

**Done when.** `PRINT 1+1` then `RETURN` prints `2`; cursor keys and function
keys behave.

---

## Milestone 3 — PLA / GO 64  `[ ]`

**Goal.** Finish the 8502 `$00`/`$01` port (the PLA) so the C128's memory
config and C64 mode are correct.

- [x] `data_read = (data & dir) | ~dir` decoded.
- [x] Low bits select the CPU/VIC colour-RAM banks at `$D800`.
- [ ] Chargen select: `$01` bit 6 (`mem_update_chargen(pport.data_read & 0x40)`)
  selects the chargen address.
- [ ] GO 64: `mmu_set_config64((~dir | data) & 0x7)` switches to the C64 memory
  map; implement a C64-mode `mem_read`/`mem_write` (the C64 PLA).

**Done when.** `GO 64` boots the C64 kernel; the chargen/colour-RAM bank select
tracks the KERNAL's `$01` writes.

---

## Milestone 4 — VIC-IIe raster, sprites, bitmap modes  `[ ]`

**Goal.** Cycle-accurate VIC-IIe: per-line raster, sprites, multicolour/bitmap.

- [x] 40x25 text renderer (screen RAM + colour RAM + chargen).
- [x] Raster IRQ registers (`$D012`/`$D019`/`$D01A`) + raster-line crossing
  check.
- [ ] Advance the raster per scanline (63 cycles/line) instead of per frame;
  fire the raster IRQ at the compare line (tie into Milestone 1).
- [ ] Bad-lines and VIC memory-fetch for sprites/bitmap.
- [ ] Sprite collision IRQ (`$D019` bits 1-2).

**Done when.** Sprites and bitmap/badline demos render correctly.

---

## Milestone 5 — CIA timers + full I/O  `[ ]`

**Goal.** Complete MOS 6526 emulation.

- [x] Timer A (latch, underflow → ICR bit 0 → IRQ line).
- [x] ICR/IMR semantics (`$DC0D` read = flags, write = mask).
- [ ] Timer B (latch, cascade from timer A, `$DC0F`/`$DD0F`).
- [ ] Time-of-day (TOD) alarm.
- [ ] Serial shift register (SDR) and the FLAG line.
- [ ] CIA2 port A VIC-bank bits and the RS-232.

**Done when.** Timer-driven code (the 50 Hz jiffy clock, TOD reads) behaves.

---

## Milestone 6 — VDC 8563 (80-column)  `[ ]`

**Goal.** Render the 80-column framebuffer from the 8563 VDC.

- [x] VDC register file + word-address counter (`vdc.c`).
- [ ] 80x25 text mode (and 640x200/400 high-res), attribute RAM, internal 16K
  video RAM.
- [ ] Composite it as an alternative to the VIC-IIe 40-column display.

**Done when.** `PRINT CHR$(14)` switches to an 80-column screen.

---

## Milestone 7 — SID audio  `[ ]`

**Goal.** Three-voice SID render → SDL3 audio stream.

- [ ] 6581/8580 oscillators, ADSR, filter, volume at `$D400-$D41F`.
- [ ] Drive the SDL3 audio callback from the 8502 frame cadence.

**Done when.** Music/SFX play.

---

## Milestone 8 — 1571 drives + disk images  `[ ]`

**Goal.** Load and save D64/D81 images, the C128's fast serial.

- [ ] The integrated 1571: 1541-compatible + 1571 2K RAM, the fast-serial
  (burst) protocol, and the IEC bus (which the KERNAL's disk routines use — this
  also removes the need for the serial ROM traps added as a boot workaround).

**Done when.** `LOAD"*",8,1` from a D64/D81 works.

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

**Goal.** Snapshots, more of the keyboard matrix, real 2 MHz timing, accuracy
passes.

- [x] GIF capture (F6), PPM screenshots (F4), boot trace, one-shot PPM dump.
- [ ] Snapshots (VICE `.vsf` or a simple own format) for save/load of machine
  state.
- [ ] Full keyboard matrix + auto-repeat.
- [ ] 2 MHz fast-mode timing (`$D507`/`$01`) affects the raster and CIA.
- [ ] Cycle-exact raster/CPU interleave; run VICE's test programs.
- [ ] WebM capture, CRT shader options, gamepad input.
- [ ] Flatpak + packaging updates; CI.

**Done when.** The emulator is a daily-driver C128DCR.

---

## Cross-cutting guidance

- **Reuse VICE.** Where a subsystem is hard (CIA, VIC-II, VDC, SID, 1571, PLA),
  port VICE's approach (its `core/`, `vicii/`, `vdc/`, `sid/`, `drive/`,
  `c128/` sources) rather than reverse-engineering from scratch. Keep VICE's
  GPLv2+ headers on anything ported.
- **Verify visually.** Use `C128_SAVE_PPM=<path>` + `C128_SAVE_FRAME=N` to dump
  frames, and `C128_BOOT_TRACE=1` to watch the CPU.
- **Keep it green.** `make -C tests check` must pass; the boot must stay stable.
