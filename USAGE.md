# 1986 - Usage

## Supported machine modes

1986 runs Commodore 128 software in the 40-column VIC-IIe and 80-column VDC
environments. It is a C128-first emulator, not a general-purpose C64 emulator.
By default, entering `GO64` displays an unsupported-mode notification and the
machine remains in native C128 mode.

For hardware testing only, enable **General > Tinker**, install the optional
C64 BASIC and KERNAL ROMs listed below, then enable **Advanced > C64 Test
Mode**. `GO64` will then enter the C128's real C64 personality. The setting is
Off by default and the gate cannot be armed without both ROMs. The same C128
machine continues running—8502, VIC-IIe, SID, CIA, RAM, and IEC state are not
replaced by a separate C64 instance. Turn the setting Off to reset back into
native C128 mode. This temporary test facility is intended for shared-hardware
validation and C128-enhanced programs which start in C64 mode; broad C64 and
C64-cartridge compatibility is not guaranteed.

CP/M is a separate, supported C128 operating mode. Attach a bootable C128 CP/M
system disk before reset (or pass it with `--disk`) and allow the machine's
normal autoboot path to run. The authentic Z80 reset BIOS first hands control
to the 8502 KERNAL; the boot sector then returns ownership to the Z80 and CP/M
Plus reaches its `A>` prompt. See [`docs/Z80-CPM.md`](docs/Z80-CPM.md) for the
technical handoff sequence.

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
| `--disk PATH` | Attach a D64, D71, D81, or standalone PRG to Drive 1 at launch. |
| `--cart PATH` | Attach a generic C128 `.crt` or raw external function-ROM `.bin`/`.rom` at launch. |
| `--snapshot PATH` | Load a C128 VICE Snapshot File (`.vsf`) at launch. |
| `--save-snapshot PATH` | Save a `.vsf` snapshot immediately before exit. |
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

With Tinker enabled, **Advanced > Double Z80 Frequency** changes the Z80 from
its stock effective 2 MHz rate to an effective 4 MHz. It models the C128
dot-clock daughterboard modification by giving the Z80 four T-states per
1 MHz bus cycle while leaving video, CIA, SID, tape, and other shared-bus
timing unchanged. The setting defaults to Off, applies immediately, persists
as `double_z80_frequency`, and is reflected by the blue Z80 footer label.

To switch between the C128 upper/graphics and upper/lowercase character sets,
press host Shift+Alt (the C128 Shift+C= chord). If the desktop intercepts that
combination, press Caps Lock once as a shortcut for the same C128 chord. This
works on both the VIC 40-column and VDC 80-column displays. Escape is C128
RUN/STOP; Page Up is RESTORE, and Escape+Page Up sends RUN/STOP+RESTORE. To
see the rest of the host-key mappings, enable **General > Tinker**, then open
**Advanced > Keyboard map** with Enter. Enter or Esc closes the map.

## Media overlay

Open the options overlay with F9 and select **Media > Drive 1 image** to insert
a D64, D71, D81, or standalone `.prg` file. Choosing another file immediately
ejects the current medium and inserts the new one, so the next `DIRECTORY`
reads the new content without an application restart. Press Del on a populated
image row to eject that drive's medium. Press F9 or Esc to close the overlay.
For a standalone PRG, the drive presents a single read-only directory entry
named after the host file (without `.prg`, uppercased and limited to 16
characters). For BASIC PRGs, use `DIRECTORY`, then `DLOAD "NAME"` or
`LOAD "NAME",8`, followed by `RUN`;
`LOAD "*",8` selects that single entry too. The PRG's two-byte load address is
preserved. `SAVE`, `SCRATCH`, and `RENAME` report write protection, leaving the
host file untouched. The same behavior applies to Drive 2 and `--disk`.

Each Media file picker reopens in its own last-used directory, including after
ejecting its media or restarting the app. Drive 1, Drive 2, tape, cartridge,
and U36 have separate remembered directories. Existing configurations use the
directory of the selected file until a new choice is made. If a remembered
directory no longer exists, the picker falls back to a valid selected-file
directory or the system default. The General machine-ROM folder picker
reopens at the configured ROM directory when it exists. Cancelling a picker
does not change its remembered location.

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
are not supported by the experimental C64 test personality.
Some cartridges draw on the VIC 40-column output even when the saved default
is VDC 80-column; select **General > 40/80 key > 40 columns (VIC)** to make
the VIC output the persistent default, or press F10 to switch while running.
Selection is restored at launch from `cart` in the config; `--cart PATH`
overrides it for that run.

**Media > Tape** accepts Commodore `.tap` pulse images and `.t64` file
containers. Replacing or clearing the entry ejects the prior tape; Del ejects
it without forgetting the picker's last directory. F2 presses/releases the
Datasette Play button and F3 rewinds. For TAP, use `LOAD"",1` at the BASIC
prompt, then press F2 when asked to press Play. The C128 controls the motor,
and tape pulses feed CIA1. T64 contains files rather than recorded pulses;
it is ready for `LOAD"",1` as soon as it is mounted, and F3 returns to its
first file. `--tape PATH` attaches either format at launch; `--tape-play`
presses Play immediately, while `--tape-play-at N` delays that press until
frame N for automated testing. In experimental C64 Test Mode, TAP uses the
same physical cassette signals and T64 uses the C64 KERNAL's tape entry
points. Tape recording is not implemented.

With Tinker enabled, **Advanced > Tape Audio Monitor** plays the TAP signal
itself through the normal audio output, without artificial motor or button
sounds. **Tape Video Monitor** shows Play/motor state, position, and recent
pulses above the footer; it stacks above the drive monitor if both are on.
T64 has no recorded waveform, so audio remains silent and the visual panel
shows its file status instead.

With **General > Tinker** enabled, **Media > U36 internal ROM** selects a raw
`.bin`/`.rom` image for the C128's internal function-ROM socket. Images must
be 8, 16, or 32 KiB; smaller images repeat through the 32 KiB address space.
U36 is independent of the external cartridge slot. Selecting a new image or
pressing Del to eject it resets the machine. The `u36` config setting restores
the selection at launch; an invalid replacement leaves the socket empty.
For BASIC 8, use the 80-column VDC display and hold CTRL through the C128
startup to activate the ROM. Standard 640x200 bitmap output is supported;
400-line interlace and some advanced VDC effects are still unfinished.

General > Main input selects which C128 control port receives the host input.
F1 swaps that host input between Joy Port 1 and Joy Port 2 immediately and
persists the choice; Shift+F1 still sends the C128 function key.
General > Joy Port 1/2 independently selects Joystick or Mouse (1351). A USB
gamepad supplies directions and fire in joystick mode (D-pad or left stick;
South/East button for fire). In mouse mode, click the emulator window to
capture the pointer; movement and left/right buttons reach the selected 1351
port through SID POTX/POTY and the CIA pins. The first click captures the system
pointer in the active VIC/VDC window. Ctrl+Enter releases it; opening the F9
overlay, changing output with F10, or swapping input ports with F1 releases it
too. These settings persist in `1986.conf`.
Advanced > Joystick HIDAPI takes effect after restart.

## Snapshots

Open F9 and choose **General > Save snapshot** or **Load snapshot**. Snapshot
files use VICE's `.vsf` container and the picker remembers its last directory.
Snapshots made by 1986 preserve both CPUs, both video systems, RAM, CIA/SID,
input, tape position, and scheduler timing. Disk and tape images remain
external files rather than being embedded.
Save and load at an idle BASIC/CP/M prompt; an IEC/GCR operation already in
progress inside a disk drive is not yet part of the snapshot.

Plain VICE C128 snapshots can be imported, but only their 8502/MMU/RAM core
state is portable: VICE 3.10 does not save the C128 Z80, and its 40-column
snapshot path does not save VDC state. The emulator displays a partial-import
notification and resets unsupported devices. VICE does not understand
1986's complete `1986STATE` module. See [snapshot compatibility](docs/SNAPSHOTS.md)
for the format and interoperability details.

The 785260 diagnostic cartridge expects Commodore's external test harness.
1986 does not silently connect its user-port, serial, or cassette
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

The fast virtual drive also has VICE-style direct-access `#` channels with
`U1`/`U2` and `B-R`/`B-W`/`B-P` block commands. For example,
`OPEN 2,8,2,"#":OPEN 15,8,15:PRINT#15,"U1:2,0,18,0"` reads track 18,
sector 0 into channel 2's buffer. `U2` and `B-W` write raw sectors directly to
the image; they can damage its filesystem, so use a backup. Errors appear in
`DS$`. VICE-style binary `M-W` and `M-R` commands can write and read the fast
drive's 32 KiB of virtual RAM. `M-E` is accepted but does not execute uploaded
drive code; GEOS still requires the real 1571 backend to boot.

Advanced > Real Disk Drive defaults to Off. With it On, Media set to 1571CR,
and `dos1571cr.bin` installed, restart to use the ROM-backed integrated 1571
over slow IEC. Normal D64/D71 GCR sector reads and writes work, including
BASIC `SAVE`; writes atomically replace the host disk image. Keep a backup of
valuable disks. Read-only media and external host edits are protected, and
Media refuses an eject/replacement while a write remains unsaved. The real
drive does not yet support nonstandard raw tracks, burst serial, or the 1581
hardware backend. With Second Drive enabled and its Media hardware type also
set to 1571CR, both ROM-backed drives share one IEC bus at separate addresses;
each has its own D64/D71 image, LED, and write protection. If either selected
hardware type is 1581, both drives use the fast virtual backend after restart;
mixed physical and virtual IEC is not available. Advanced has independent audio
and visual drive monitors, both Off by default; the visual
scope sits above the function-key footer and shows Drive 2's track above
Drive 1's. The audio monitor mixes both drives' mechanism sounds.
If a write error appears, resolve it before quitting: the original image stays
intact, but unsaved in-memory GCR data cannot survive exit.

The bottom bar also includes processor activity. The white 8502 lamp is labeled
`1MHZ` or `2MHZ` from the current CPU clock, including VIC-IIe `$D030` changes;
the blue `Z80 2MHZ` lamp lights only while the Z80 is executing.

GEOS 128 from `GEOS128.D64` reaches the Desktop with Real Disk Drive On and
the 1571CR selected. At the BASIC prompt, use `DLOAD"GEOS128"` and then `RUN`.
Its loader uploads drive code with `M-W`/`M-E`, so the fast virtual drive cannot
boot it. Set the desired joy port to Mouse (1351) and click the emulator window
to capture the pointer. GEOS may write to its disk; keep a backup of the image.

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
| `basic64.rom` | 0x2000   | Optional C64 BASIC V2 test ROM    |
| `kernal64.rom`| 0x2000   | Optional C64 KERNAL test ROM      |

These ROMs are copyrighted Commodore and are not bundled. The emulator still
renders its test pattern without them. The optional files may instead use the
VICE-set names `basic64-901226-01.bin` and `kernal64-901227-03.bin`.
