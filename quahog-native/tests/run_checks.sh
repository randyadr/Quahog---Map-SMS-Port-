#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
CC=${CC:-cc}
TEST_OUT=${TEST_OUT:-/tmp/quahog-native-c-tests}
mkdir -p "$TEST_OUT"
FLAGS="-DQUAHOG_TEST -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror -Isrc -Isdk"
$CC $FLAGS tests/test_core.c src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c -lm -o "$TEST_OUT/core"
"$TEST_OUT/core" tests/fixtures assets
$CC $FLAGS -DQUAHOG_TEST -c src/quahog_native.c -o "$TEST_OUT/plugin.o"
for api in current LEGACY_13 LEGACY_14; do
    case "$api" in current) define="";; *) define="-D$api";; esac
    $CC $FLAGS $define tests/test_runtime.c "$TEST_OUT/plugin.o" src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c -lm -o "$TEST_OUT/runtime-$api"
    # Each invocation gets a new data folder; reruns are reproducible.
    case "$TEST_OUT" in /tmp/*) rm -rf "$TEST_OUT/data-$api";; *) test ! -e "$TEST_OUT/data-$api";; esac
    "$TEST_OUT/runtime-$api" tests/fixtures/game . "$TEST_OUT/data-$api"
done

$CC $FLAGS tests/test_cache.c src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c -lm -o "$TEST_OUT/cache"
case "$TEST_OUT" in /tmp/*) rm -rf "$TEST_OUT/data-cache";; *) test ! -e "$TEST_OUT/data-cache";; esac
"$TEST_OUT/cache" tests/fixtures/game . "$TEST_OUT/data-cache"

$CC $FLAGS tests/test_large_pe.c src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c -lm -o "$TEST_OUT/large-pe"
"$TEST_OUT/large-pe" "$TEST_OUT"

$CC $FLAGS tests/test_macho.c src/qcore.c src/qmacho.c -o "$TEST_OUT/macho"
"$TEST_OUT/macho"
$CC $FLAGS tests/test_posix.c src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c -lm -o "$TEST_OUT/posix"
"$TEST_OUT/posix"
