AC_INIT([1986], [0.1.0], [], [1986])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

# C compiler — require C11 (GNU extensions are used by the VICE-derived core)
AC_PROG_CC
AX_CHECK_COMPILE_FLAG([-std=gnu11],
    [AM_CFLAGS="$AM_CFLAGS -std=gnu11"],
    [AC_MSG_ERROR([GNU C11-capable compiler required])])
AC_SUBST([AM_CFLAGS])

# pkg-config
PKG_PROG_PKG_CONFIG

# Required libraries
PKG_CHECK_MODULES([SDL3], [sdl3], [], [AC_MSG_ERROR([SDL3 not found])])
AC_CHECK_LIB([m], [sqrt], [], [AC_MSG_ERROR([libm not found])])

# ffmpeg - optional. Enables WebM/VP9 capture and the post-capture GIF
# optimization pass. The F6-triggered built-in GIF encoder works without it.
AC_PATH_PROG([FFMPEG], [ffmpeg], [])
AS_IF([test -n "$FFMPEG"],
      [AC_DEFINE_UNQUOTED([FFMPEG_PATH], ["$FFMPEG"],
           [Path to ffmpeg binary for video capture])
       AC_DEFINE([HAVE_FFMPEG], [1], [ffmpeg available for video capture])
       AC_MSG_NOTICE([video capture: WebM and GIF optimization enabled via $FFMPEG])],
      [AC_MSG_NOTICE([video capture: WebM and GIF optimization disabled (no ffmpeg found)])])

# Short git commit shown in the About dialog alongside PACKAGE_VERSION.
PROG_GIT_COMMIT=`git -C "$srcdir" rev-parse --short HEAD 2>/dev/null || echo unknown`
AC_MSG_NOTICE([git commit: $PROG_GIT_COMMIT])
AC_SUBST([PROG_GIT_COMMIT])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
