#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 OUTPUT_DIR" >&2
    exit 2
fi

output_dir=$1
version=$(sed -n 's/^PACKAGE_VERSION = //p' Makefile)
arch=$(dpkg --print-architecture)
if [ -z "$version" ]; then
    echo "configure must be run before building the Debian package" >&2
    exit 1
fi

mkdir -p "$output_dir"
output_dir=$(CDPATH= cd "$output_dir" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM

make install DESTDIR="$stage"
install -d "$stage/DEBIAN" "$stage/usr/share/doc/1986"
install -m 0644 LICENSE "$stage/usr/share/doc/1986/copyright"
install -m 0644 README.md INSTALL.md USAGE.md "$stage/usr/share/doc/1986/"

depends=$(dpkg-shlibdeps -O -e"$stage/usr/bin/1986" | sed -n 's/^shlibs:Depends=//p')
if [ -z "$depends" ]; then
    echo "dpkg-shlibdeps returned no runtime dependencies" >&2
    exit 1
fi

printf 'Package: 1986\nVersion: %s\nArchitecture: %s\nMaintainer: Salvatore Bognanni <salvogendut@gmail.com>\nSection: games\nPriority: optional\nDepends: %s\nHomepage: https://github.com/salvogendut/1986\nDescription: Commodore C128DCR emulator\n SDL3-based C128DCR emulator with VIC-IIe and VDC displays, SID audio,\n cartridge and disk image support. Machine ROMs are not included.\n' \
    "$version" "$arch" "$depends" > "$stage/DEBIAN/control"

dpkg-deb --root-owner-group --build "$stage" \
    "$output_dir/1986_${version}_${arch}.deb"
