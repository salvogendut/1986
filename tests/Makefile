CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=

SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3)

TESTS = test-cpu test-mmu test-mem test-cia test-sid test-config test-gifcap test-d64 test-d64-save test-disk-formats test-drive test-vic

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

test-cia: test_cia.c ../src/cia.c ../src/cia.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_cia.c ../src/cia.c -o $@

test-sid: test_sid.c ../src/sid.c ../src/sid.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_sid.c ../src/sid.c -lm -o $@

test-config: test_config.c ../src/config.c ../src/config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_config.c ../src/config.c -o $@

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

test-vic: test_vic.c ../src/vic.c ../src/vic.h ../src/display.h ../src/mem.c ../src/mem.h ../src/mmu.c ../src/mmu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_vic.c ../src/vic.c ../src/mem.c ../src/mmu.c -o $@

check: $(TESTS)
	./test-cpu
	./test-mmu
	./test-mem
	./test-cia
	./test-sid
	./test-config
	./test-gifcap
	./test-d64
	./test-d64-save
	./test-disk-formats
	./test-drive
	./test-vic

clean:
	rm -f $(TESTS)
