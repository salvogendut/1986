# 1986 - Controls

This is the reference for 1986's keyboard and input controls. Machine setup,
media, and configuration are covered in [`USAGE.md`](USAGE.md).

## Host keys

| Key | Action |
|-----|--------|
| F2  | Tape Play/Stop |
| F3  | Rewind tape |
| F4  | Save a PPM screenshot |
| F5  | Reset |
| F6  | Toggle animated GIF recording |
| F7  | Pause or resume |
| F8  | Monitor/disassembler |
| F9  | Open / save-and-close the options overlay |
| F10 | Switch between 40-column VIC-II and 80-column VDC |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Ctrl++ / Ctrl+- | Adjust window scale |
| Ctrl+V | Paste host clipboard into the C128 |

These mirror the sibling projects (1983, 1984, 1985): F9 owns the overlay,
F6 owns capture, F12 quits.

## Options overlay

Left/Right change section, Up/Down select, Enter toggles, F9 saves and
closes, Escape closes (offering to discard if there are unsaved changes).
Sections:

- **General** - fullscreen, smoothing, fast (2 MHz) mode, model, scale,
  reset to defaults.
- **Video** - CRT effect, scanlines, brightness, contrast.
- **Capture** - GIF width/fps, ffmpeg optimization, save & close.

## GIF capture

**F6** (or `--gif-out PATH`) records an animated GIF of the current
framebuffer. The Capture section sets resolution and frame rate.

## Clipboard paste

Ctrl+V replays the host clipboard into the C128 keyboard one key at a time.
The ASCII->matrix map is a scaffold subset; it will grow with the keyboard
module.
