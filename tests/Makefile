CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=

SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3)

ifeq ($(shell uname -s),Darwin)
GC_SECTIONS_LDFLAGS = -Wl,-dead_strip
else
GC_SECTIONS_LDFLAGS = -Wl,--gc-sections
endif

TESTS = test-cpu test-mmu test-mem test-cartridge test-cia test-sid test-config test-joyport test-c128-keyboard test-gifcap test-d64 test-d64-save test-disk-formats test-drive test-drive1571cr test-drive-monitor test-gcr-drive test-via6522 test-iec-bus test-prg-drive test-overlay test-display-footer test-vic test-vdc

.PHONY: all check clean

all: $(TESTS)

test-cpu: test_cpu.c ../src/cpu.c ../src/cpu.h ../src/vice/maincpu.c ../src/vice/interrupt.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -Wall -Wextra -I../src \
		test_cpu.c ../src/cpu.c ../src/vice/maincpu.c ../src/vice/interrupt.c -lm -o $@

test-mmu: test_mmu.c ../src/mmu.c ../src/mmu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_mmu.c ../src/mmu.c -o $@

test-mem: test_mem.c ../src/mem.c ../src/mem.h ../src/mmu.c ../src/mmu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_mem.c ../src/mem.c ../src/mmu.c -o $@

test-cartridge: test_cartridge.c ../src/cartridge.c ../src/cartridge.h ../src/mem.c ../src/mmu.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -Wall -Wextra -I../src \
		test_cartridge.c ../src/cartridge.c ../src/mem.c ../src/mmu.c -o $@

test-cia: test_cia.c ../src/cia.c ../src/cia.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_cia.c ../src/cia.c -o $@

test-sid: test_sid.c ../src/sid.c ../src/sid.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_sid.c ../src/sid.c -lm -o $@

test-config: test_config.c ../src/config.c ../src/config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_config.c ../src/config.c -o $@

test-joyport: test_joyport.c ../src/joyport.c ../src/joyport.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_joyport.c ../src/joyport.c -o $@

test-c128-keyboard: test_c128_keyboard.c ../src/c128.c ../src/kbd.c ../src/c128.h ../src/kbd.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=gnu11 -Wall -Wextra \
		-ffunction-sections -fdata-sections $(GC_SECTIONS_LDFLAGS) -I../src \
		test_c128_keyboard.c ../src/c128.c ../src/kbd.c $(SDL3_LIBS) -o $@

test-gifcap: test_gifcap.c ../src/gifcap.c ../src/gifcap.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_gifcap.c ../src/gifcap.c -o $@

test-d64: test_d64.c ../src/disk_image.c ../src/disk_image.h ../src/virtual_drive.c ../src/virtual_drive.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_d64.c ../src/disk_image.c ../src/virtual_drive.c -o $@

test-d64-save: test_d64_save.c ../src/disk_image.c ../src/disk_image.h ../src/virtual_drive.c ../src/virtual_drive.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_d64_save.c ../src/disk_image.c ../src/virtual_drive.c -o $@

test-disk-formats: test_disk_formats.c ../src/disk_image.c ../src/disk_image.h ../src/virtual_drive.c ../src/virtual_drive.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_disk_formats.c ../src/disk_image.c ../src/virtual_drive.c -o $@

test-drive: test_drive.c ../src/drive.c ../src/drive.h ../src/disk_image.c ../src/disk_image.h ../src/virtual_drive.c ../src/virtual_drive.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=gnu11 -Wall -Wextra -I../src \
		test_drive.c ../src/drive.c ../src/disk_image.c ../src/virtual_drive.c -o $@

test-drive1571cr: test_drive1571cr.c ../src/drive1571cr.c ../src/drive1571cr.h ../src/gcr_drive.c ../src/gcr_drive.h ../src/disk_image.c ../src/via6522.c ../src/via6522.h ../src/cia.c ../src/cia.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_drive1571cr.c ../src/drive1571cr.c ../src/gcr_drive.c ../src/disk_image.c ../src/via6522.c ../src/cia.c -o $@

test-drive-monitor: test_drive_monitor.c ../src/drive_monitor.c ../src/drive_monitor.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_drive_monitor.c ../src/drive_monitor.c -lm -o $@

test-gcr-drive: test_gcr_drive.c ../src/gcr_drive.c ../src/gcr_drive.h ../src/disk_image.c ../src/via6522.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_gcr_drive.c ../src/gcr_drive.c ../src/disk_image.c ../src/via6522.c -o $@

test-via6522: test_via6522.c ../src/via6522.c ../src/via6522.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_via6522.c ../src/via6522.c -o $@

test-iec-bus: test_iec_bus.c ../src/iec_bus.c ../src/iec_bus.h ../src/via6522.c ../src/via6522.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_iec_bus.c ../src/iec_bus.c ../src/via6522.c -o $@

test-prg-drive: test_prg_drive.c ../src/drive.c ../src/drive.h ../src/disk_image.c ../src/disk_image.h ../src/virtual_drive.c ../src/virtual_drive.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=gnu11 -Wall -Wextra -I../src \
		test_prg_drive.c ../src/drive.c ../src/disk_image.c ../src/virtual_drive.c -o $@

test-overlay: test_overlay.c ../src/overlay.c ../src/overlay.h ../src/config.c ../src/cartridge.c ../src/mem.c ../src/mmu.c ../src/drive.c ../src/disk_image.c ../src/virtual_drive.c ../src/joyport.c ../src/drive_monitor.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=gnu11 -Wall -Wextra -I../src \
		test_overlay.c ../src/overlay.c ../src/config.c ../src/cartridge.c ../src/mem.c ../src/mmu.c ../src/drive.c \
		../src/disk_image.c ../src/virtual_drive.c ../src/joyport.c ../src/drive_monitor.c $(SDL3_LIBS) -lm -o $@

test-display-footer: test_display_footer.c ../src/display.c ../src/display.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=gnu11 -Wall -Wextra -I../src \
		test_display_footer.c ../src/display.c $(SDL3_LIBS) -o $@

test-vic: test_vic.c ../src/vic.c ../src/vic.h ../src/display.h ../src/mem.c ../src/mem.h ../src/mmu.c ../src/mmu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_vic.c ../src/vic.c ../src/mem.c ../src/mmu.c -o $@

test-vdc: test_vdc.c ../src/vdc.c ../src/vdc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_vdc.c ../src/vdc.c -o $@

check: $(TESTS)
	./test-cpu
	./test-mmu
	./test-mem
	./test-cartridge
	./test-cia
	./test-sid
	./test-config
	./test-joyport
	./test-c128-keyboard
	./test-gifcap
	./test-d64
	./test-d64-save
	./test-disk-formats
	./test-drive
	./test-drive1571cr
	./test-drive-monitor
	./test-gcr-drive
	./test-via6522
	./test-iec-bus
	./test-prg-drive
	./test-overlay
	./test-display-footer
	./test-vic
	./test-vdc

clean:
	rm -f $(TESTS)
