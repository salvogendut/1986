bin_PROGRAMS = 1986

# Man page — installed under $(mandir)/man1/, included in `make dist`.
dist_man1_MANS = 1986.1

1986_SOURCES = \
	src/config.c \
	src/main.c \
	src/display.c \
	src/overlay.c \
	src/kbd.c \
	src/joyport.c \
	src/c128.c \
	src/cpu.c \
	src/mem.c \
	src/cartridge.c \
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
	src/disk_image.c \
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
	src/joyport.h \
	src/c128.h \
	src/cpu.h \
	src/mem.h \
	src/cartridge.h \
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
	src/disk_image.h \
	src/drive.h \
	src/virtual_drive.h \
	src/shutter_wav.h \
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
1986_LDADD  = $(SDL3_LIBS) -lm $(WIN_LIBS) $(WIN_RC_OBJ)
1986_DEPENDENCIES = $(WIN_RC_OBJ)

icons/1986-rc.o: $(srcdir)/icons/1986.rc $(srcdir)/icons/1986.ico
	$(WINDRES) -I$(srcdir)/icons -i $< -O coff -o $@

# Machine ROMs are not distributed. Install only the guidance file; users
# supply their own ROM images in this directory or configure a ROM path.
romsdir       = $(pkgdatadir)/roms
dist_roms_DATA = roms/README

# Desktop integration: .desktop launcher, AppStream metainfo, hicolor icons.
desktopdir         = $(datadir)/applications
dist_desktop_DATA  = io.github.salvogendut.Emulator1986.desktop

metainfodir        = $(datadir)/metainfo
dist_metainfo_DATA = io.github.salvogendut.Emulator1986.metainfo.xml

icon16dir          = $(datadir)/icons/hicolor/16x16/apps
dist_icon16_DATA   = icons/16x16/apps/io.github.salvogendut.Emulator1986.png
icon32dir          = $(datadir)/icons/hicolor/32x32/apps
dist_icon32_DATA   = icons/32x32/apps/io.github.salvogendut.Emulator1986.png
icon48dir          = $(datadir)/icons/hicolor/48x48/apps
dist_icon48_DATA   = icons/48x48/apps/io.github.salvogendut.Emulator1986.png
icon64dir          = $(datadir)/icons/hicolor/64x64/apps
dist_icon64_DATA   = icons/64x64/apps/io.github.salvogendut.Emulator1986.png
icon128dir         = $(datadir)/icons/hicolor/128x128/apps
dist_icon128_DATA  = icons/128x128/apps/io.github.salvogendut.Emulator1986.png
icon256dir         = $(datadir)/icons/hicolor/256x256/apps
dist_icon256_DATA  = icons/256x256/apps/io.github.salvogendut.Emulator1986.png
icon512dir         = $(datadir)/icons/hicolor/512x512/apps
dist_icon512_DATA  = icons/512x512/apps/io.github.salvogendut.Emulator1986.png

# Files shipped in the dist tarball but not built (license, docs, examples).
EXTRA_DIST = LICENSE README.md Development.md USAGE.md CONTROLS.md INSTALL.md \
	ROADMAP.md 1986.conf.example 1986_logo.png 1986.spec \
	src/vice/6510core.c \
	icons/1986.rc icons/1986.ico \
	packaging/io.github.salvogendut.Emulator1986.yml \
	packaging/debian/build-deb.sh packaging/macos/package.sh \
	tests/Makefile tests/test_cpu.c tests/test_mmu.c tests/test_mem.c \
	tests/test_cartridge.c tests/test_cia.c tests/test_sid.c tests/test_config.c \
	tests/test_joyport.c tests/test_c128_keyboard.c tests/test_gifcap.c \
	tests/test_d64.c tests/test_d64_save.c tests/test_disk_formats.c \
	tests/test_drive.c tests/test_prg_drive.c tests/test_overlay.c \
	tests/test_display_footer.c tests/test_vic.c tests/test_vdc.c
