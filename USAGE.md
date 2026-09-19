# 1986 - Usage

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
| `--gif-out PATH` | Start recording a GIF at launch. |
| `--help` | Show help. |

## Configuration

Settings are read from `1986.conf` in the current directory (or written there
by the options overlay). See [`1986.conf.example`](1986.conf.example).

## ROM layout

Drop the machine ROMs into a directory and pass `--rom DIR` (or set `rom_dir`
in `1986.conf`; the default is the install-time `pkgdatadir/roms`):

| File          | Size     | Purpose                       |
|---------------|----------|-------------------------------|
| `kernal.rom`  | 0x4000   | C128 KERNAL ($C000-$FFFF)     |
| `basic.rom`   | 0x8000   | BASIC 7.0 (low + high)        |
| `chargen.rom` | 0x1000   | Character generator           |

These ROMs are copyrighted Commodore and are not bundled. The emulator still
renders its test pattern without them.
