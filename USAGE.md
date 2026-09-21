# 1986 - Usage

## Supported machine modes

1986 runs native Commodore 128 software in the 40-column VIC-IIe and
80-column VDC environments. The C64 compatibility personality is
intentionally not implemented. Entering `GO64`, or requesting C64 mode during
boot, displays a notification and safely returns to native C128 mode.

CP/M is a separate C128 operating mode and remains planned.

## Command line

```
1986 [options]
```

| Option | Description |
|--------|-------------|
| `--scale N` | Window scale factor (1..4, default 2). |
| `--fullscreen` | Start fullscreen. |
| `--fast` | Run the 8502 at 2 MHz (C128 fast mode). |
| `--rom DIR` | Directory holding the machine ROM images. |
| `--disk PATH` | Attach a D64, D71, or D81 image to Drive 1 at launch. |
| `--cart PATH` | Attach a generic C128 `.crt` or raw external function-ROM `.bin`/`.rom` at launch. |
| `--gif-out PATH` | Start recording a GIF at launch. |
| `--paste TEXT` | Inject text through the emulated keyboard. |
| `--paste-at N` | Delay `--paste` until emulated frame N. |
| `--frames N` | Exit after N emulated frames. |
| `--no-throttle` | Run without real-time frame pacing; SID state still advances, but host audio playback is disabled. |
| `--help` | Show help. |

## Configuration

Settings are read from `~/.config/1986/1986.conf` (or from `1986.conf` when
`HOME` is unset) and written by the options overlay. See
[`1986.conf.example`](1986.conf.example).
For an isolated run, `C128_CONFIG_PATH` can point to a specific config file.

**General > 40/80 key** selects the emulated keyboard's 40- or 80-column
default, equivalent to setting the C128's physical 40/80 key. It takes effect
immediately and is saved as `display_columns = 40` or `display_columns = 80`.
F10 switches between the outputs too; its last choice is saved when the
application closes normally. Existing configurations without this key default
to the 80-column VDC display. With Unified Display enabled, the shared window
shows the selected output. With Unified Display disabled, both output windows
open and the selected output receives window focus.

## Media overlay

Open the options overlay with F9 and select **Media > Drive 1 image** to insert
a D64, D71, or D81 image. Choosing another image immediately ejects the current
disk and inserts the new one, so the next `DIRECTORY` reads the new disk
without an application restart. Press Del on a populated image row to eject
that drive's disk. Press F9 or Esc to close the overlay.

Enable **Advanced > Second Drive** to show **Drive 2** and **Drive 2 image** in
Media. The two drives have independent images and IEC device numbers #8-#11;
Enter on a Drive row cycles its number while skipping the other drive's number.
Drive 1 defaults to #8, Drive 2 to #9. Turning Second Drive off disconnects
Drive 2 from IEC but remembers its image and unit for the next time it is on.
The toggle defaults to Off and requires Tinker to expose Advanced.

**Media > Cartridge** accepts generic native-C128 `.crt` images (type 0)
and raw external function-ROM `.bin`/`.rom` dumps of 8, 16, or 32 KiB.
A 64 KiB EPROM dump is accepted only when its two 32 KiB halves are identical;
larger or bank-switched images need a cartridge-specific mapper. Smaller raw
ROMs are mirrored through the 32 KiB function-ROM space. Inserting, replacing,
or ejecting a cartridge resets the machine, and Del ejects it. A failed
replacement leaves the slot empty and clears the saved path. C64-only CRTs
cannot be used because the C64 personality is intentionally unsupported.
Some cartridges draw on the VIC 40-column output even when the saved default
is VDC 80-column; select **General > 40/80 key > 40 columns (VIC)** to make
the VIC output the persistent default, or press F10 to switch while running.
Selection is restored at launch from `cart` in the config; `--cart PATH`
overrides it for that run. Tape selection remains a placeholder for now.

With **General > Tinker** enabled, **Media > U36 internal ROM** selects a raw
`.bin`/`.rom` image for the C128's internal function-ROM socket. Images must
be 8, 16, or 32 KiB; smaller images repeat through the 32 KiB address space.
U36 is independent of the external cartridge slot. Selecting a new image or
pressing Del to eject it resets the machine. The `u36` config setting restores
the selection at launch; an invalid replacement leaves the socket empty.

The 785260 diagnostic cartridge expects Commodore's external test harness.
1986 does not silently connect its user-port, serial, joystick, or cassette
loopback wiring. Those tests, including the CIA-to-CIA data-line part of
`INTERRUPT`, can report `BAD` without a harness. The two CIA chips do emulate
TOD alarms, serial shift-register interrupts, and FLAG edges internally.

`SAVE "NAME",8` and BASIC 7.0 `DSAVE "NAME"` write PRG files to the attached
D64, D71, or D81 image; `DIRECTORY` and `LOAD` see them immediately, and they
persist after the application closes. Saving an existing name leaves it
unchanged and sets DOS status `63,FILE EXISTS` (check with `PRINT DS$`). Use the DOS replace prefix,
for example `SAVE "@:NAME",8`, to overwrite an unlocked file.
Replacement is limited to PRG files; other file types (including D81
partitions) are left untouched and report `64,FILE TYPE MISMATCH`.
Writes modify the host disk-image file; keep a backup of any irreplaceable image.
Symlinked or read-only images can still be loaded but cannot be saved to.
The DOS command channel also supports scratch and rename on unlocked root
SEQ/PRG/USR files. For example, `OPEN 15,8,15,"S:OLD*":CLOSE 15` scratches
matching files, and `OPEN 15,8,15,"R:NEW=OLD":CLOSE 15` renames one.
`PRINT DS$` shows the command result; scratch reports the number of files removed.
The commands write the host image atomically. Locked files, REL files, D81
partitions, and malformed chains are not modified.

With Tinker enabled in General, Advanced > Real Disk Drive stores a future
backend preference. It defaults to Off. On currently displays `On (pending)`:
the hardware drive emulator is not yet implemented, so the fast virtual drive
remains active. It does not emulate 1571/1581 hardware or D81 partition and
REL-file operations.

The F9 overlay uses a compact top panel with smaller text; the running screen
remains visible below it.

## Sound

The C128DCR's 8580 SID plays through the default SDL3 audio device. At the
BASIC 7.0 prompt, try `VOL 15:PLAY "CDEFGAB"` or
`VOL 15:SOUND 1,40960,60`. The analog filter and combined waveforms are
approximations, so some music will sound different from a real 8580 or VICE.

## ROM layout

Drop the machine ROMs into a directory and pass `--rom DIR` (or set `rom_dir`
in `1986.conf`; the default is the install-time `pkgdatadir/roms`):

| File          | Size     | Purpose                       |
|---------------|----------|-------------------------------|
| `kernal.rom`  | 0x4000   | C128 KERNAL ($C000-$FFFF)     |
| `basic.rom`   | 0x8000   | BASIC 7.0 (low + high)        |
| `chargen.rom` | 0x2000   | C64 + native-C128 character banks |

These ROMs are copyrighted Commodore and are not bundled. The emulator still
renders its test pattern without them.
