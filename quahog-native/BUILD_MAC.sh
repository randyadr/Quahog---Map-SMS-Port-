#!/bin/sh
# Source rebuild only. Players use the included dylibs without build tools.
set -eu
cd "$(dirname "$0")"
CC=${CC:-clang}
for arch in arm64 x86_64; do
    case "$arch" in arm64) target=arm64-apple-macos11; minimum=11.0;;
                       *) target=x86_64-apple-macos10.15; minimum=10.15;; esac
    out="bin/macos-$arch"
    mkdir -p "$out"
    $CC -target "$target" -mmacosx-version-min="$minimum" -std=c11 -O2 \
        -Wall -Wextra -Werror -ffp-contract=off -dynamiclib -Isdk \
        src/quahog_native.c src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c \
        -Wl,-install_name,@rpath/quahog_native.dylib -o "$out/quahog_native.dylib"
    codesign --force --sign - "$out/quahog_native.dylib"
    codesign --verify --strict "$out/quahog_native.dylib"
done
