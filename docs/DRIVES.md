# Disk and drive architecture

1986 has two intentionally different IEC storage backends. Both use the same
neutral disk-image/media layer, but they do not share an emulated drive CPU.

| Backend | Purpose | Images | Drive CPU/DOS ROM |
|---------|---------|--------|-------------------|
| Fast virtual drive | Convenient command-level file access | D64, D71, D81, PRG | No |
| Real Disk Drive: 1571CR | Experimental hardware-level drive | D64, D71 | Yes |
| Real Disk Drive: 1581 | Experimental, read-only hardware-level drive | D81 | Yes |

## Fast virtual drive

The default backend implements an IEC device at the KERNAL/command level. It
supports `DIRECTORY`, `LOAD`/`DLOAD`, `SAVE`/`DSAVE`, DOS status, `SCRATCH`,
and `RENAME`. Writes update the BAM and directory and replace the host image
atomically. `@:` replacement of an unlocked PRG is supported.

VICE-style direct-access `#` buffers support `U1`/`U2` and `B-R`/`B-W`/`B-P`.
Binary `M-W` and `M-R` access 32 KiB of virtual drive RAM. `M-E` is accepted
but uploaded drive code is not executed; software whose loader depends on
running custom drive code needs the ROM-backed backend.

In the fast backend, D81 support describes the filesystem, not hardware
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

**Advanced > Unthrottled drive** (default Off) temporarily removes host frame
pacing while either real drive is busy: its motor is running and its hardware
busy LED or write gate is on. The whole machine speeds up together, preserving
CPU/IEC/drive clock ratios, then returns to normal speed when neither drive is
busy. Motor spin-down alone and error flashes with the motor stopped do not
trigger acceleration. Custom loaders control these signals too: a loader that
keeps the busy LED off while reading remains paced, while software leaving
both motor and busy LED on stays accelerated until you turn this option off.

The preference is saved and takes effect immediately with a running real
drive. It is inactive with the fast virtual backend (including fallback due
to unavailable drive hardware/ROMs). Pausing still pauses and paces the host
loop; `--no-throttle` still requests continuous unthrottled operation.
SID/tape/drive audio is muted and its queue cleared across acceleration
transitions; LEDs and visual monitors keep updating. Leave this option Off
to hear loading music or drive sounds at their normal pace.

The following are not yet implemented:

- 1571 burst/fast serial;
- the 1571's WD1770/FDC2 MFM behavior;
- precise sub-instruction mechanism timing;
- persistence of nonstandard raw or protected tracks.

If an enabled drive's selected DOS ROM is missing, both drives fall back to
the fast virtual backend after restart, with a console diagnostic. Mixed
virtual and ROM-backed devices on
the same emulated IEC bus are not currently supported.

## ROM-backed 1581 (read-only, #152)

Enable **Advanced > Real Disk Drive**, select **1581 (D81 read-only)** for
either drive in Media, supply a 32 KiB `dos1581.bin` in the machine ROM
directory, and restart. The VICE filename `dos1581-318045-02.bin` is also
recognized. ROMs are not supplied with the emulator.

`DIRECTORY`, `LOAD`, and `BLOAD` use the native KERNAL and DOS ROM over the
physical slow IEC bus. Two 1581s, or a 1571CR and a 1581 in either slot, share
that bus at distinct addresses #8–#11. Hardware type and unit changes require
restart; toggling Second Drive reconnects the existing running model.
Each device has its own LED, monitor history and audio. The existing audio
samples are reused as an approximate mechanism monitor, not a 1581-specific
sound model. Unthrottled drive also recognizes 1581 controller activity.

Use ordinary 80-track D81 media. D64/D71/PRG media in a real 1581 report an
incompatible-media notification and leave its mechanism empty. Replacing or
ejecting a disk cancels controller transfers before freeing the old image,
and signals disk change so DOS invalidates its track cache.

The core shares the independent NMOS 6502 instruction engine with the 1571,
and has its own 2 MHz clock budget, 8 KiB RAM, 32 KiB ROM, CIA timers/IRQs,
IEC pin interface, side/motor/LED outputs, and WD1770 register window. The
address map, CIA wiring, head polarity, and WD command timing were checked
against VICE 3.10's `memiec.c`, `cia1581d.c`, `wd1770.c`, and `fdd.c`.

The WD1770 currently reads ordinary 80-track D81 images through decoded
512-byte physical sectors (two CBM DOS blocks). It implements seek/step,
read-sector, read-address, data-ready/lost-data status, force-interrupt,
media-change handling, and read/seek activity counters. Sector spacing is
approximated; this is not yet a raw MFM-track or protection emulator. All
images report write protection, and write commands cannot modify them.

All media are currently **write-protected**, even if the host file is
writable. BASIC write attempts report the DOS write-protect error and do not
change the image. Atomic sector writes and further VICE comparisons are the
next step. Burst serial, the 8520 binary TOD counter, raw-track commands,
and drive snapshot state remain unimplemented. The browser frontend still
offers its existing real 1571 switch; 1581 selection is desktop-only for now.

Tests use synthetic firmware and generated media by default. An optional
private-ROM check boots DOS 318045-02 and submits five read jobs spanning
both sides and tracks 1/40/80, checking the returned bytes:

```sh
make -C tests test-drive1581 test-wd1770
./tests/test-wd1770
C128_1581_ROM=/path/to/dos1581-318045-02.bin ./tests/test-drive1581
```

No DOS ROM is included or copied by this test. Its disposable D81 is removed
afterwards. A separate windowless host test covers native `DIRECTORY`/`BLOAD`,
write protection, media replacement, two 1581s at #10/#11, and mixed models:

```sh
make -C tests test-real-drives
C128_TEST_ROM_DIR=/path/to/machine-roms \
C128_1581_ROM=/path/to/dos1581-318045-02.bin \
C128_TEST_1571_ROM=/path/to/dos1571cr.bin ./tests/test-real-drives
```

Omit the 1571 variable to skip the mixed-model cases. Without private ROM
variables, this test exercises synthetic CPU clocks, bus gates and fallback.

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
