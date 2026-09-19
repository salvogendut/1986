# 1986 - Installation

## Dependencies

- A C11 compiler (gcc or clang)
- GNU autotools (autoconf, automake, autoreconf)
- pkg-config
- SDL3 development files (`sdl3-devel` on Fedora, `libsdl3-dev` on Debian)

## Build

```bash
autoreconf -iv
./configure
make -j"$(nproc)"
./1986
```

To install system-wide:

```bash
sudo make install
```

## Flatpak

A Flatpak manifest is provided in
[`packaging/io.github.salvogendut.Emulator1986.yml`](packaging/io.github.salvogendut.Emulator1986.yml).

## Running headless (smoke test)

The emulator can be checked without a display:

```bash
SDL_VIDEODRIVER=dummy ./1986
```

## Machine ROMs

The emulator runs and shows its test pattern without ROMs. To boot the KERNAL
and BASIC, supply the machine ROMs (see [`USAGE.md`](USAGE.md)).
