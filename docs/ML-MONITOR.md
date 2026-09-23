# Multiprocessor ML Monitor

Press **F8** to open the machine-language monitor. It is a separate resizable
80x25 terminal window inspired by the monitor in 1984, with independent MOS
8502 and Z80 contexts for the C128's shared address bus.

The status row shows the selected processor, its principal registers, whether
it owns the bus (`*`), the actual bus owner, and whether the machine is running
or paused. Press **Tab** to switch the selected context. Selecting a context
does not transfer the physical bus; MMU register `$D505` still decides which
processor can execute.

## Commands

Addresses and byte values are hexadecimal. Commands are case-insensitive.

| Command | Action |
|---------|--------|
| `CPU` | Show the selected context and current bus owner. |
| `CPU 8502` / `CPU Z80` | Select a processor for registers, memory, disassembly, and new breakpoints. |
| `CPU OWNER` | Select whichever processor currently owns the bus. |
| `R` | Show all registers for the selected processor. |
| `D [addr [end]]` | Disassemble 8502 or Z80 code. With no address, start at the selected PC. |
| `M addr [end]` | Show a hexadecimal and ASCII memory dump. |
| `E addr byte...` | Write bytes through the selected processor's current MMU mapping. |
| `B` | List breakpoints, including their CPU context. |
| `B addr` | Set a breakpoint for the selected processor. |
| `BE id` / `BD id` | Enable or disable a breakpoint. |
| `BC id` | Clear a breakpoint. |
| `P` | Pause at the current instruction boundary. |
| `N` | Execute one instruction of the selected processor. The selected processor must own the bus. |
| `G` or `GO` | Resume execution. |
| `MMU` | Show CPU ownership, MMU configuration, RAM bank, common-RAM setup, and machine mode. |
| `H` or `?` | Show the command summary. |
| `X` or `Q` | Close the monitor. |

F7 also pauses/resumes while the monitor has focus; F8 or Escape closes it.
Long disassemblies and memory dumps pause after one screen. Enter or Space
continues and Escape cancels the remaining output.

## Dual-CPU behavior

Breakpoints are tagged with a processor, so an 8502 and Z80 breakpoint may
exist at the same numeric address. They are checked at instruction boundaries
inside the normal scheduler. A hit pauses the whole C128 and brings the
monitor to the front with the stopped processor selected and its next
instructions displayed.

Memory and disassembly use the selected processor's current CPU-visible view.
The Z80 reset-BIOS mirror and its MMU mapping therefore differ from the 8502
view where the real hardware differs. Monitor reads of the I/O window are
non-destructive: taking a snapshot does not acknowledge CIA/VIC interrupts or
advance the VDC data pointer. The `E` command is intentionally a real bus
write and may change MMU or device state.

Only the processor owning the bus can be stepped. To follow a handoff, use
`CPU OWNER` after `$D505` changes ownership. This preserves the C128 model:
the two CPUs do not run concurrently.
