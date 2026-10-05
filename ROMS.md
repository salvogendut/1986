# ROM requirements

1986 does **not** include Commodore machine or drive ROMs. Supply your own
images from a source you are authorized to use. ROM dumps stay untracked and
are not included in source archives or release packages.

## Where to put them

Put the files in `roms/` beside the desktop executable, or choose your ROM
folder in **General > ROMS PATH**. You can also use:

```sh
./1986 --rom /path/to/roms
```

Without an explicit folder, the desktop first checks `roms/` beside the
executable, then its installation ROM directory (normally
`/usr/share/1986/roms` in Linux packages). All machine and drive ROMs are
loaded from the selected folder. Restart after adding ROMs or changing the
ROM folder or Real Disk Drive setting. Changing a drive hardware type in Media
instead triggers an immediate full power cycle using the already-loaded ROMs.
If a required drive ROM is unavailable, the change is cancelled and the current
hardware stays active; add the ROM and restart before trying again.

Use the exact filenames below, including case. Only one accepted name is
needed for each image; alternatives are tried in the order shown.

## Native C128DCR

These three images form the normal machine ROM set. They are needed
regardless of whether you use the fast or real drive.

| Recommended filename | Size | Purpose | Other accepted filenames |
|---|---|---|---|
| `basic.bin` | 32 KiB (32,768 bytes) | Both halves of BASIC 7.0 | `basic.rom`, `318023-02.u32` |
| `kernal.bin` | 16 KiB (16,384 bytes), or a compatible 32 KiB chip image | C128 screen editor, Z80 BIOS and KERNAL | `kernal.rom`, `318022-02.u34` |
| `chargen.bin` | 8 KiB (8,192 bytes) | C64 and native-C128 character banks | `chargen.rom`, `characters-c128d` |

The 16 KiB `kernal.bin` layout is:

| File offset | Contents |
|---|---|
| `$0000–$0FFF` | 4 KiB screen editor |
| `$1000–$1FFF` | 4 KiB Z80 BIOS |
| `$2000–$3FFF` | 8 KiB KERNAL |

For a compatible 32 KiB KERNAL chip dump, 1986 uses its **upper 16 KiB** as
that layout. An 8 KiB C64 KERNAL alone is not a C128 `kernal.bin`.
Use a complete 32 KiB BASIC image, not just one 16 KiB half.

Legacy 4 KiB character dumps are accepted and mirrored, but the full 8 KiB
character image is recommended for both character banks. The desktop
extracts the Z80 BIOS from `kernal.bin`: it does **not** load a separate
`z80bios.bin`. CP/M additionally needs a compatible system disk, not another
machine ROM.

## Real disk drives

The default **fast virtual drive needs no drive DOS ROM**, including for
D81 files. With **Advanced > Real Disk Drive** enabled, supply the ROM for
each hardware model selected in Media (enable General > Tinker to show
Advanced):

| Selected hardware | Required filename | Size | Other accepted filenames |
|---|---|---|---|
| 1571CR | `dos1571cr.bin` | 32 KiB (32,768 bytes) | None |
| 1581 | `dos1581.bin` | 32 KiB (32,768 bytes) | `dos1581-318045-02.bin` |

The 1571CR requires its matching CR DOS image; a file called `dos1571.bin`
is not loaded automatically. Both drive slots share the ROM files: two
1581s need only one 1581 image, while a mixed 1571CR/1581 pair needs both
images. Real 1581 emulation reads and writes ordinary D81 images, respecting
host-file write protection. Keep backups; see [drive support](docs/DRIVES.md)
for safety rules and limitations.

**If any enabled drive is missing its selected DOS ROM, both drives fall
back to the fast virtual backend on startup.** A working directory or load
does not by itself prove the real drive is active. Check the terminal:

```text
1986: drive 1: 1581 on line-level IEC #8 (D81)
```

The fallback instead reports `real-drive backend unavailable; using fast
virtual drive`. The audio and visual mechanism monitors require an active
real backend, so they stay silent/hidden in fallback mode even when their
toggles are On. Also turn **Unthrottled drive** Off to hear drive audio:
audio is intentionally muted during accelerated disk operations.

## Optional C64 development mode

Native C128 operation does not require these. **Advanced > C64 Test Mode**
requires both images, in addition to the native machine set:

| Recommended filename | Size | Other accepted filenames |
|---|---|---|
| `basic64.bin` | 8 KiB (8,192 bytes) | `basic64.rom`, `basic64-901226-01.bin` |
| `kernal64.bin` | 8 KiB (8,192 bytes) | `kernal64.rom`, `kernal64-901227-03.bin` |

No separate C64 character ROM is needed; the machine uses `chargen.bin`.

## Optional cartridges and U36 function ROMs

Cartridges and the U36 socket are optional software, not part of the
required machine set. Select them separately in Media; they can have any
filename. U36 accepts raw 8, 16 or 32 KiB `.bin`/`.rom` images. Native C128
cartridges accept supported `.crt` files or raw function ROMs; see
[media instructions](USAGE.md#media-overlay) for supported formats.

## Browser build

The current `web/Makefile` requires these exact names in `ROM_DIR` (default
`../roms`, relative to `web/`); desktop filename aliases do not apply:

- `basic.bin` — 32 KiB.
- `kernal.bin` — 16 KiB, or a compatible 32 KiB chip image as above.
- `chargen.bin` — 8 KiB recommended.
- `z80bios.bin` — 4 KiB.
- `dos1571cr.bin` — 32 KiB, even if the browser starts in fast-drive mode.

`z80bios.bin` is currently a browser build/embedding dependency, although
the shared machine core obtains its BIOS from `kernal.bin`. For a 16 KiB
KERNAL image, those bytes are at offsets `$1000–$1FFF`; for a 32 KiB chip
image, they are at `$5000–$5FFF`.

The browser currently offers the real 1571, not 1581 selection or the C64
test-mode ROM set. Embedded ROM bytes are part of the generated WASM
package: only publish it if you have permission to distribute those ROMs.
See the [browser guide](web/README.md).
