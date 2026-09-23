# Z80 initialization and CPU handoff

This note records the C128-specific Z80 boot path implemented by 1986. Keep it
current when changing the scheduler, MMU, Z80 bus, or future native Z80 code:
CP/M depends on the real machine's two-CPU handshake rather than on a synthetic
"start CP/M" trap.

## Reset ownership

`c128_reset()` resets both processors. The 8502 reset vector is prepared, but
MMU register `$D505` resets with bit 0 clear, so the Z80 owns the system bus.
`z80_reset()` starts at PC `$0000`, with interrupts disabled, interrupt mode 0,
and no pending interrupt. `z80_init()` initially sets SP to `$FFFF`; reset does
not depend on that value because the authentic BIOS establishes the stack
before using it.

The scheduler must run exactly one owner at a time:

| `$D505` bit 0 | Bus owner |
|---------------|-----------|
| 0 | Z80 |
| 1 | 8502 |

Ownership changes take effect at the end of the instruction that writes
`$D505`. The processor that relinquishes the bus retains its complete state and
continues at the following instruction when ownership is returned.

## Private reset BIOS

At reset, while MMU CR bit 6 is clear, Z80 memory `$0000-$0FFF` reads the
private 4 KiB Z80 BIOS extracted from the C128 KERNAL ROM image. Writes in this
window go to bank-zero RAM beneath `$D000-$DFFF`; they do not modify the ROM.

The authentic BIOS begins by selecting the native memory configuration through
`$FF00`, initializes C128 hardware, and installs code required by both CPUs. In
particular it copies a 31-byte dual-CPU handoff block from BIOS offset `$0EE5`
to common RAM at `$FFD0`.

The two entry points in that copied block are:

- **`$FFD0`, 8502 entry:** masks interrupts, selects native RAM at `$FF00`,
  writes `$B0` to `$D505` to give the bus to the Z80, then—when the 8502 later
  resumes—continues with its next instruction and jumps to `$3000`.
- **`$FFE0`, Z80 entry:** disables interrupts, selects native RAM at `$FF00`,
  writes `$B1` to Z80 I/O port `$D505` to give the bus to the 8502, then—when
  the Z80 later resumes—continues after the `OUT` instruction.

The reset BIOS initially jumps to the Z80 `$FFE0` entry. That transfers control
to the waiting 8502, which performs the normal KERNAL cold start and boot-sector
scan. A bootable CP/M disk eventually enters the 8502 `$FFD0` side to return
the bus to the Z80. CP/M subsequently uses the same mechanism repeatedly for
services that cross the CPU boundary.

Do not replace these addresses with host-side traps. `$FFD0-$FFFE` must remain
real shared RAM visible under the active MMU configuration, and a `$D505`
write must stop the old owner before it can execute another instruction.

## Z80 bus rules

The C128 Z80 bus in `c128.c` deliberately differs from the 8502 bus:

- Memory `$0000-$0FFF` exposes the private reset BIOS until CR bit 6 selects
  normal RAM.
- Z80 ports `$0000-$0FFF` alias bank-zero RAM at `$D000-$DFFF` during the
  reset configuration.
- `$FF00-$FF04` are the MMU mirrors used to select/commit memory configurations.
- Peripheral I/O decodes through Z80 port pages `$D0-$DF`. The VDC at `$D600`
  remains reachable in native mode; only the `$D5xx` MMU page is disconnected
  when CR bit 0 requests that behavior.
- A disconnected `$D5xx` Z80 read returns zero, matching the C128/VICE path.
- The Z80 cannot select the 8502 character-ROM view at `$D000-$DFFF`.

Future Z80 code should use `IN`/`OUT` for the C128 I/O pages and must preserve
the MMU/common-RAM assumptions around the handoff block.

## Timing and interrupts

The stock Z80 is scheduled at two T-states per 1 MHz PAL video/bus cycle.
**Advanced > Double Z80 Frequency** models the
[C128 8 MHz Z80 daughterboard](https://github.com/ytmytm/c128-z80-8mhz): it
supplies four T-states during the same available CPU phase, producing an
effective 4 MHz execution rate. CIA, SID, tape, VDC bus time, and other
one-MHz peripherals remain driven from the shared bus clock in either mode.
Instruction overrun is carried between raster slices rather than truncating
an instruction.

CIA1/VIC maskable interrupt state is presented to the active processor. The
blue Z80 footer lamp is updated only by actual Z80 execution and reports the
configured effective `2MHZ` or `4MHZ` rate; the white 8502 lamp and its live
1/2 MHz label are accounted independently.

## Validation baseline

The current integration baseline is:

1. A reset without media performs the Z80-to-8502 bootstrap and reaches BASIC
   7.0 `READY.`.
2. Attaching `cpm.system.6228151676.d64` before reset performs the authentic
   disk boot and reaches the CP/M Plus `A>` prompt.
3. Boot tracing (`C128_BOOT_TRACE=1`) reports the active owner plus both PCs,
   making handoff regressions visible.

Relevant implementation files are `src/c128.c`, `src/mmu.c`, `src/mem.c`, and
`src/z80.c`; the MMU ownership assertions live in `tests/test_mmu.c`.
