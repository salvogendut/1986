EMCC ?= emcc
ROM_DIR ?= ../roms
BUILD_DIR := build
DIST_DIR := dist
CFLAGS := -O2 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Icompat -I../src -I../src/vice -I.

SRCS := \
	../src/config.c ../src/kbd.c ../src/joyport.c ../src/c128.c \
	../src/cpu.c ../src/mem.c ../src/cartridge.c ../src/mmu.c \
	../src/vic.c ../src/vdc.c ../src/cia.c ../src/sid.c ../src/z80.c \
	../src/vice/maincpu.c ../src/vice/interrupt.c \
	../src/disk_image.c ../src/drive.c ../src/drive1571cr.c \
	../src/drive_monitor.c ../src/tape.c ../src/gcr_drive.c \
	../src/iec_bus.c ../src/via6522.c ../src/virtual_drive.c

CORE_OBJS := $(patsubst ../src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJS := $(CORE_OBJS) $(BUILD_DIR)/poc_main.o $(BUILD_DIR)/stubs.o
ROM_FILES := $(addprefix $(ROM_DIR)/,basic.bin kernal.bin chargen.bin z80bios.bin dos1571cr.bin)
WEB_ASSETS := index.html styles.css theme-c128-dcr.css theme-retro-crt.css \
	theme-sapporo.css theme-sapporo-dark.css app.js gamepad.js media-url.js

all: $(DIST_DIR)/1986.js $(addprefix $(DIST_DIR)/,$(WEB_ASSETS)) $(DIST_DIR)/brand-1986.png

$(DIST_DIR)/1986.js: $(OBJS) $(ROM_FILES) | $(DIST_DIR)
	$(EMCC) $(CFLAGS) $(OBJS) -o $@ -lm \
	  -sMODULARIZE -sEXPORT_NAME=create1986 -sALLOW_MEMORY_GROWTH=1 \
	  -sINITIAL_MEMORY=100663296 -Wl,-z,stack-size=1048576 \
	  -sEXPORTED_FUNCTIONS='_poc_init,_poc_reset,_poc_step,_poc_pixels,_poc_width,_poc_height,_poc_display,_poc_set_display,_poc_key,_poc_joy,_poc_load_disk,_poc_eject_disk,_poc_load_tape,_poc_eject_tape,_poc_tape_control,_poc_load_cart,_poc_eject_cart,_poc_set_real_drive,_poc_real_drive,_poc_activity,_poc_audio_avail,_poc_audio_read_pos,_poc_audio_buffer,_poc_audio_advance,_poc_audio_reset,_malloc,_free' \
	  -sEXPORTED_RUNTIME_METHODS='HEAPU8,HEAPU32,HEAP16,FS,ccall,cwrap' \
	  --embed-file $(ROM_DIR)/basic.bin@/roms/basic.bin \
	  --embed-file $(ROM_DIR)/kernal.bin@/roms/kernal.bin \
	  --embed-file $(ROM_DIR)/chargen.bin@/roms/chargen.bin \
	  --embed-file $(ROM_DIR)/z80bios.bin@/roms/z80bios.bin \
	  --embed-file $(ROM_DIR)/dos1571cr.bin@/roms/dos1571cr.bin

$(BUILD_DIR)/%.o: ../src/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/poc_main.o: poc_main.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/stubs.o: stubs.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c $< -o $@

$(DIST_DIR)/%: % | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/brand-1986.png: ../1986_logo.png | $(DIST_DIR)
	cp $< $@

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

test:
	node test_media_url.js
	node test_gamepad.js
	node test_ui_contract.js

test-wasm: all
	node test_wasm_smoke.js

serve: all
	python3 -m http.server 8080 --directory $(DIST_DIR)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

.PHONY: all clean serve test test-wasm
