# Snapshots and VICE compatibility

1986 saves snapshots in a VICE Snapshot File (`.vsf`) container. The file
header, machine identifier, module headers, versions, lengths, and little-
endian encoding follow VICE 3.10's snapshot implementation.

Use **Media > Save snapshot** and **Media > Load snapshot** in the F9
overlay. The picker remembers its last directory. The equivalent command-line
options are `--snapshot FILE.vsf` and `--save-snapshot FILE.vsf`.

## Why 1986 has its own module

VICE's C128 snapshot writer currently records `MAINCPU`, `C128MEM`, CIA, SID,
VIC-II, drive, tape, keyboard, and joystick modules. Its C128 writer does not
record a Z80 CPU module, and the tested 40-column snapshot did not contain VDC
state. Those omissions prevent it from being a complete interchange format
for a dual-CPU, dual-display C128 emulator.

1986 therefore stores complete resumable state in a versioned `1986STATE`
module inside the VSF container. It includes the 8502 core and clock, Z80,
MMU and 128 KiB RAM, color RAM, VIC-IIe raster state, VDC registers and 64 KiB
video RAM, both CIAs, SID, keyboard, joyports, tape position, display mode,
and scheduler timing. Host pointers and framebuffer allocations are never
serialized. Mounted media remain external files and are not copied into the
snapshot. Save and restore at an idle BASIC/CP/M prompt: in-progress virtual
or ROM-drive IEC/GCR transactions are not yet serialized.

## Interoperability

- 1986 can import a VICE 3.10 C128 snapshot's `MAINCPU` and `C128MEM` modules.
  Because VICE does not provide the Z80/VDC state required for a full resume,
  1986 resets unsupported devices and reports **VICE SNAPSHOT IMPORTED - CORE
  STATE ONLY**.
- Snapshots created by 1986 round-trip the complete state described above.
- VICE cannot currently resume `1986STATE`. 1986 deliberately does not emit a
  misleading, incomplete set of VICE peripheral modules: VICE will reject an
  1986-authored file rather than partially applying it and becoming unstable.

This is intentionally asymmetric compatibility. Full two-way interchange
would require VICE and 1986 to agree on modules for the Z80 and VDC as well as
all peripheral state.
