# Disk and drive architecture

1986 has two intentionally different IEC storage backends. Both use the same
neutral disk-image/media layer, but they do not share an emulated drive CPU.

| Backend | Purpose | Images | Drive CPU/DOS ROM |
|---------|---------|--------|-------------------|
| Fast virtual drive | Convenient command-level file access | D64, D71, D81, PRG | No |
| Real Disk Drive | Experimental hardware-level 1571CR behavior | D64, D71 | Yes |

## Fast virtual drive

The default backend implements an IEC device at the KERNAL/command level. It
supports `DIRECTORY`, `LOAD`/`DLOAD`, `SAVE`/`DSAVE`, DOS status, `SCRATCH`,
and `RENAME`. Writes update the BAM and directory and replace the host image
atomically. `@:` replacement of an unlocked PRG is supported.

VICE-style direct-access `#` buffers support `U1`/`U2` and `B-R`/`B-W`/`B-P`.
Binary `M-W` and `M-R` access 32 KiB of virtual drive RAM. `M-E` is accepted
but uploaded drive code is not executed; software whose loader depends on
running custom drive code needs the ROM-backed backend.

D81 support describes the image/filesystem format, not 1581 hardware
emulation. D81 partitions, REL files, formatting, and some DOS commands remain
unfinished. Raw block writes can damage a filesystem, so keep backups.

An optional second virtual drive has its own image and IEC address. The two
units must use distinct addresses from #8 through #11.

## ROM-backed 1571CR

Enable **Advanced > Real Disk Drive**, select **1571CR** in Media, install a
compatible `dos1571cr.bin`, and restart. The integrated drive then runs its
DOS ROM through an independent NMOS 6502, 2 KiB RAM, two 6522 VIAs, and the
MOS5710's limited CIA-compatible registers. CIA2 and VIA1 exchange slow IEC
ATN, CLOCK, DATA, and ATNA over an open-collector bus.

VIA2 is connected to a GCR mechanism backed by D64/D71 sectors. It exposes
motor, head stepping, side selection, speed, sync, write protection, byte-ready
signals, and standard sector writes. Checksum-valid writes are decoded and
persisted atomically when the write gate closes, the head/side changes, or the
medium is ejected.

With Second Drive enabled and both hardware selectors set to 1571CR, two
independent ROM-backed drives share the IEC bus. Each keeps its own CPU,
address, image, write protection, LED, and monitor history.

The following are not yet implemented:

- 1571 burst/fast serial;
- WD1770/FDC2 MFM behavior;
- precise sub-instruction mechanism timing;
- persistence of nonstandard raw or protected tracks;
- a hardware-level 1581 backend.

If either enabled drive selects 1581 hardware, both drives fall back to the
fast virtual backend after restart. Mixed virtual and ROM-backed devices on
the same emulated IEC bus are not currently supported.

## Write safety

Both backends protect read-only images and detect external host-file changes.
The ROM-backed path refuses media replacement when a GCR write cannot be
saved. Resolve such an error before quitting: the original image remains
intact, but unrepresentable pending GCR data cannot survive process exit.
Always keep backups of valuable images.

## Activity indicators and monitors

Each enabled drive has its own footer LED. In ROM-backed mode the indicator
follows motor, head, ROM LED, and actual byte activity rather than a timer.

**Advanced > Drive Audio Monitor** mixes approximate 1541-family motor/head
samples into the SID output. **Drive Visual Monitor** plots reads above the
center line, writes below it, and head steps as full-height marks. With two
real drives, Drive 2 appears above Drive 1. The monitors do not affect emulated
timing and do not synthesize activity for the fast virtual backend.

## Compatibility note

GEOS 128 reaches its Desktop through the ROM-backed 1571CR path because its
loader uploads and executes drive code. Attach a backup of `GEOS128.D64`, use
`DLOAD"GEOS128"` followed by `RUN`, and select Mouse (1351) on the desired
control port if required.

See [USAGE.md](../USAGE.md) for media-overlay instructions and BASIC/DOS
examples, and [Development.md](../Development.md) for implementation detail.

