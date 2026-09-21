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
It builds the current checkout, including on pull requests. To build locally,
run `flatpak-builder --install-deps-from=flathub --force-clean build-dir
packaging/io.github.salvogendut.Emulator1986.yml`.

## Release packages

GitHub Actions builds Fedora x86_64 RPM, Debian amd64 DEB, Windows x86_64
portable ZIP, macOS arm64 and x86_64 app ZIPs, and a Linux x86_64 Flatpak
bundle on pull requests and main. Pushing a `v*` tag publishes those artifacts
in a GitHub Release; the tag must match the versions in `configure.ac` and
`1986.spec` (for example, `v0.1.0`). The Linux packages install a desktop entry
and icons;
the Windows executable embeds the icon, and the macOS app includes an `.icns`.

The packages contain no copyrighted Commodore machine ROMs. Supply your own
ROM images as described in [`USAGE.md`](USAGE.md). Portable Windows and macOS
bundles look for a `roms` directory next to the executable. Installed Linux
packages use `/usr/share/1986/roms` by default (or a configured ROM path).

## Running headless (smoke test)

The emulator can be checked without a display:

```bash
SDL_VIDEODRIVER=dummy ./1986
```

## Machine ROMs

The emulator runs and shows its test pattern without ROMs. To boot the KERNAL
and BASIC, supply the machine ROMs (see [`USAGE.md`](USAGE.md)).
