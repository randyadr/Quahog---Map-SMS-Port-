#!/bin/sh
# Run on a Mac: loads the actual release dylib into a synthetic host executable.
set -eu
cd "$(dirname "$0")/.."
case "$(uname -s)" in Darwin) ;; *) echo "This check needs macOS." >&2; exit 1;; esac
arch=$(uname -m)
case "$arch" in arm64|x86_64) ;; *) echo "Unsupported CPU: $arch" >&2; exit 1;; esac
CC=${CC:-cc}
TEST_OUT=$(mktemp -d /tmp/quahog-macos.XXXXXX)
trap 'rm -rf "$TEST_OUT"' EXIT HUP INT TERM
library="$PWD/bin/macos-$arch/quahog_native.dylib"
codesign --verify --strict "$library"
for api in current LEGACY_13 LEGACY_14; do
    case "$api" in current) define="";; *) define="-D$api";; esac
    for lookup in exported hidden; do
        case "$lookup" in hidden) visibility="-fvisibility=hidden -DQUAHOG_HIDDEN_HOST";;
                               *) visibility="-Wl,-export_dynamic";; esac
        host="$TEST_OUT/host-$api-$lookup"
        $CC -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror -DQUAHOG_DYNAMIC_TEST \
            $define $visibility -Isrc -Isdk tests/test_runtime.c src/qcore.c -lm -o "$host"
        codesign --force --sign - "$host"
        "$host" tests/fixtures/game . "$TEST_OUT/data-$api-$lookup" "$library"
    done
done
echo "Actual macOS $arch dylib: direct exports, Mach-O fallback and unload/reload passed."
