/*
 * vice.h - VICE-compatibility umbrella header (minimal, self-contained).
 *
 * This file is part of 1986. The VICE-derived CPU core (in src/vice/) expects a
 * few VICE base macros/types; this header provides just enough of them so the
 * core builds standalone, without pulling in the rest of VICE.
 *
 * The 6510/8502 core itself is derived from VICE and remains under the GPLv2+
 * (see the headers in src/vice/ and Development.md).
 */

#ifndef VICE_H
#define VICE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZEOF_UNSIGNED_INT   4
#define SIZEOF_UNSIGNED_LONG  8
#define SIZEOF_UNSIGNED_SHORT 2

#ifndef VICE_CAST
#define VICE_CAST(t, v) ((t)(v))
#endif

#endif
