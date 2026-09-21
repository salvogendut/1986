bin_PROGRAMS = 1986

# Man page — installed under $(mandir)/man1/, included in `make dist`.
dist_man1_MANS = 1986.1

1986_SOURCES = \
	src/config.c \
	src/main.c \
	src/display.c \
	src/overlay.c \
	src/kbd.c \
	src/c128.c \
	src/cpu.c \
	src/mem.c \
	src/mmu.c \
	src/vic.c \
	src/vdc.c \
	src/cia.c \
	src/sid.c \
	src/z80.c \
	src/z80dis.c \
	src/vice/maincpu.c \
	src/vice/interrupt.c \
	src/paste.c \
	src/monitor.c \
	src/gifcap.c \
	src/leds.c \
	src/notify.c \
	src/d64.c \
	src/drive.c \
	src/virtual_drive.c

noinst_HEADERS = \
	src/types.h \
	src/compat_win.h \
	src/startup_debug.h \
	src/config.h \
	src/display.h \
	src/overlay.h \
	src/kbd.h \
	src/c128.h \
	src/cpu.h \
	src/mem.h \
	src/mmu.h \
	src/vic.h \
	src/vdc.h \
	src/cia.h \
	src/sid.h \
	src/z80.h \
	src/z80dis.h \
	src/paste.h \
	src/monitor.h \
	src/gifcap.h \
	src/leds.h \
	src/notify.h \
	src/d64.h \
	src/drive.h \
	src/virtual_drive.h \
	src/vice/vice.h \
	src/vice/types.h \
	src/vice/log.h \
	src/vice/debug.h \
	src/vice/lib.h \
	src/vice/maincpu.h \
	src/vice/interrupt.h \
	src/vice/alarm.h \
	src/vice/clkguard.h \
	src/vice/machine.h \
	src/vice/mem.h \
	src/vice/monitor.h \
	src/vice/traps.h \
	src/vice/snapshot.h \
	src/vice/h6809regs.h \
	src/vice/mos6510.h \
	src/vice/6510core.h

1986_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS) -Wall -Wextra \
	-DROM_INSTALL_DIR=\"$(pkgdatadir)/roms\" \
	-DPROG_GIT_COMMIT=\"$(PROG_GIT_COMMIT)\"
1986_LDADD  = $(SDL3_LIBS) -lm

# Bundled ROM images installed to $(datadir)/1986/roms/
# (C128/KERNAL ROMs are free for emulator use; user-provided images can be
# dropped into the same directory and are picked up by name.)
romsdir       = $(pkgdatadir)/roms
dist_roms_DATA = roms/README

# Desktop integration: .desktop launcher, AppStream metainfo, hicolor icons.
desktopdir         = $(datadir)/applications
dist_desktop_DATA  = io.github.salvogendut.Emulator1986.desktop

metainfodir        = $(datadir)/metainfo
dist_metainfo_DATA = io.github.salvogendut.Emulator1986.metainfo.xml

# Files shipped in the dist tarball but not built (license, docs, examples).
EXTRA_DIST = Makefile LICENSE Development.md USAGE.md CONTROLS.md INSTALL.md \
	ROADMAP.md 1986.conf.example packaging/io.github.salvogendut.Emulator1986.yml \
	roms/README tests/Makefile tests/test_cpu.c tests/test_mmu.c \
	tests/test_config.c tests/test_gifcap.c tests/test_d64.c tests/test_sid.c
