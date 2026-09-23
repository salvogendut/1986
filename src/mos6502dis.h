#pragma once
#include "types.h"
#include <stddef.h>

/* Disassemble one NMOS 6502/8502 instruction from a CPU-visible snapshot.
 * Returns the number of bytes consumed. Common NMOS undocumented opcodes are
 * decoded as well so listings of compatibility-mode software stay aligned. */
int mos6502dis(const u8 *mem, u16 pc, char *out, size_t outsz);
