CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=

SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3)

TESTS = test-cpu test-mmu test-config test-gifcap

.PHONY: all check clean

all: $(TESTS)

test-cpu: test_cpu.c ../src/cpu.c ../src/cpu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_cpu.c ../src/cpu.c -o $@

test-mmu: test_mmu.c ../src/mmu.c ../src/mmu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_mmu.c ../src/mmu.c -o $@

test-config: test_config.c ../src/config.c ../src/config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_config.c ../src/config.c -o $@

test-gifcap: test_gifcap.c ../src/gifcap.c ../src/gifcap.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -I../src \
		test_gifcap.c ../src/gifcap.c -o $@

check: $(TESTS)
	./test-cpu
	./test-mmu
	./test-config
	./test-gifcap

clean:
	rm -f $(TESTS)
