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

- 1986 recognizes a plain VICE C128 snapshot but rejects it before changing
  the running machine. A `MAINCPU` + `C128MEM` projection is not resumable:
  the CPU interrupt state, VIC-II raster phase, CIA timers/interrupts, SID,
  drives, tape, keyboard, and joyports must remain synchronized. Resetting
  those devices while resuming a running CPU can make execution diverge into
  data and JAM the 8502.
- Snapshots created by 1986 round-trip the complete state described above.
- VICE cannot currently resume `1986STATE`. 1986 deliberately does not emit a
  misleading, incomplete set of VICE peripheral modules: VICE will reject an
  1986-authored file rather than partially applying it and becoming unstable.

The shared `.vsf` framing therefore does not currently imply shared machine
state. Full two-way interchange requires module translation for every device
plus an agreed representation for the Z80 and VDC state that VICE's C128
writer currently omits.
