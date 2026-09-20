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
| `--disk PATH` | Attach a D64 image at launch. |
| `--gif-out PATH` | Start recording a GIF at launch. |
| `--paste TEXT` | Inject text through the emulated keyboard. |
| `--paste-at N` | Delay `--paste` until emulated frame N. |
| `--frames N` | Exit after N emulated frames. |
| `--no-throttle` | Run without real-time frame pacing. |
| `--help` | Show help. |

## Configuration

Settings are read from `~/.config/1986/1986.conf` (or from `1986.conf` when
`HOME` is unset) and written by the options overlay. See
[`1986.conf.example`](1986.conf.example).

The last display selected with F10 is stored as `display_columns = 40` or
`display_columns = 80` when the application closes normally and restored on
the next launch. Existing configurations without this key default to the
80-column VDC display. With Unified Display enabled, the shared window shows
that output. With Unified Display disabled, both output windows open and the
last selected output receives window focus.

## Media overlay

Open the options overlay with F9 and select **Media > Disk image** to insert a
D64 image. Choosing another image immediately ejects the current disk and
inserts the new one, so the next `DIRECTORY` reads the new disk without an
application restart. With an image selected, press Esc on the Disk image row
to eject it and clear the saved entry; press F9 to close the overlay.

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
