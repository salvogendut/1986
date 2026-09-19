/*
 * types.h - VICE-compatible type definitions (minimal, self-contained).
 *
 * This file is part of 1986. The VICE-derived CPU core expects the classic
 * VICE base types (BYTE, WORD, DWORD, CLOCK, ...). This header provides the
 * same type names over standard C types, so the core builds standalone.
 *
 * Derived from VICE's arch/unix/types.h (GPLv2+); see Development.md.
 */

#ifndef VICE_TYPES_H
#define VICE_TYPES_H

#include "vice.h"
#include <stdint.h>

typedef unsigned char BYTE;
typedef signed char SIGNED_CHAR;
typedef unsigned short WORD;
typedef signed short SWORD;
typedef unsigned int DWORD;
typedef signed int SDWORD;
typedef uint32_t CLOCK;

#define CLOCK_MAX (~((CLOCK)0))

#define TRUE  1
#define FALSE 0

/* A small int type used by some VICE tables. */
typedef int BOOL;

/* STATIC_ASSERT / TRAP_OPCODE used by the 6510 core. */
#define STATIC_ASSERT(x)  ((void)0)
#define TRAP_OPCODE       0x02

#endif
