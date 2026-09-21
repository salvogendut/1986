#pragma once
#include "types.h"
#include <stdbool.h>

/* Native C128 external function ROM: $8000-$FFFF, selected in either 16K
 * half by the MMU. Generic C128 CRT and raw 8/16/32K ROM dumps are supported;
 * repeated 64K EPROM images are accepted as well.
 * The cartridge is deliberately independent of C64-mode cartridge hardware. */
#define CARTRIDGE_ROM_SIZE 0x8000

typedef struct {
    u8 rom[CARTRIDGE_ROM_SIZE];
    char name[33];
    bool attached;
} Cartridge;

typedef enum {
    CART_OK = 0,
    CART_ERR_OPEN,
    CART_ERR_FORMAT,
    CART_ERR_UNSUPPORTED,
    CART_ERR_SIZE
} CartridgeResult;

void cartridge_detach(Cartridge *cart);
/* On failure, the existing cartridge remains attached. The caller can eject
 * first to model a physical replacement. */
CartridgeResult cartridge_attach(Cartridge *cart, const char *path);
const char *cartridge_result_name(CartridgeResult result);
