#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 BINARY APP_DIR VERSION" >&2
    exit 2
fi

binary=$1
app=$2
version=$3
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
contents="$app/Contents"
macos="$contents/MacOS"
resources="$contents/Resources"
frameworks="$contents/Frameworks"
plist="$contents/Info.plist"
iconset="$resources/1986.iconset"
plistbuddy=/usr/libexec/PlistBuddy

if [ -e "$app" ]; then
    echo "macOS bundle output already exists: $app" >&2
    exit 1
fi

mkdir -p "$macos" "$resources" "$frameworks" "$iconset"
install -m 0755 "$binary" "$macos/1986"
mkdir -p "$macos/roms"
cp "$root/roms/README" "$macos/roms/"
cp "$root/LICENSE" "$resources/LICENSE.txt"
cp "$root/README.md" "$resources/README.md"

for size in 16 32 128 256 512; do
    cp "$root/icons/${size}x${size}/apps/io.github.salvogendut.Emulator1986.png" \
        "$iconset/icon_${size}x${size}.png"
done
cp "$root/icons/32x32/apps/io.github.salvogendut.Emulator1986.png" \
    "$iconset/icon_16x16@2x.png"
cp "$root/icons/64x64/apps/io.github.salvogendut.Emulator1986.png" \
    "$iconset/icon_32x32@2x.png"
cp "$root/icons/256x256/apps/io.github.salvogendut.Emulator1986.png" \
    "$iconset/icon_128x128@2x.png"
cp "$root/icons/512x512/apps/io.github.salvogendut.Emulator1986.png" \
    "$iconset/icon_256x256@2x.png"
sips -z 1024 1024 \
    "$root/icons/512x512/apps/io.github.salvogendut.Emulator1986.png" \
    --out "$iconset/icon_512x512@2x.png" >/dev/null
iconutil -c icns "$iconset" -o "$resources/1986.icns"
rm -rf "$iconset"

plutil -create xml1 "$plist"
"$plistbuddy" -c "Add :CFBundleDevelopmentRegion string en" "$plist"
"$plistbuddy" -c "Add :CFBundleDisplayName string 1986" "$plist"
"$plistbuddy" -c "Add :CFBundleExecutable string 1986" "$plist"
"$plistbuddy" -c "Add :CFBundleIconFile string 1986.icns" "$plist"
"$plistbuddy" -c "Add :CFBundleIdentifier string io.github.salvogendut.Emulator1986" "$plist"
"$plistbuddy" -c "Add :CFBundleInfoDictionaryVersion string 6.0" "$plist"
"$plistbuddy" -c "Add :CFBundleName string 1986" "$plist"
"$plistbuddy" -c "Add :CFBundlePackageType string APPL" "$plist"
"$plistbuddy" -c "Add :CFBundleShortVersionString string $version" "$plist"
"$plistbuddy" -c "Add :CFBundleVersion string $version" "$plist"
"$plistbuddy" -c "Add :LSApplicationCategoryType string public.app-category.entertainment" "$plist"
"$plistbuddy" -c "Add :LSMinimumSystemVersion string 15.0" "$plist"
"$plistbuddy" -c "Add :NSHighResolutionCapable bool true" "$plist"

brew_prefix=$(brew --prefix)
dylibbundler -od -b \
    -x "$macos/1986" \
    -d "$frameworks" \
    -p "@executable_path/../Frameworks/" \
    -s "$brew_prefix/lib" \
    -s "$(brew --prefix sdl3)/lib"

if find "$macos" "$frameworks" -type f -print0 |
        xargs -0 otool -L |
        grep -F "$brew_prefix/"; then
    echo "macOS bundle still contains Homebrew library references" >&2
    exit 1
fi

codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"
