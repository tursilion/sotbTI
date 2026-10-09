#!/bin/bash
# Host simulation of the game LOGIC (native gcc, 32-bit int).
# Compiles the game .c + generated data with stub vdp.h/kscan.h so the logic
# (map decode, entity update, collision, portals) can be run and printed on the
# host. CART_BANK is a no-op off-target (data is flat), so banking isn't tested
# here - only the C logic. NOTE: host int is 32-bit; the TI is 16-bit, so this
# does NOT catch 16-bit-specific issues. For those, use the TMS9900 harness at
# /home/tursi/newtms9900-gcc/test (harness/harness -f X.bin).
#
# Usage: put your main() in a test .c and pass it:  ./run.sh mytest.c
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="$HERE/../.."
TEST="${1:-$HERE/test.c}"
cd "$PORT"                     # cd in so the gen/bk*.c glob works despite spaces in path
gcc -I "$HERE" -I src -I gen \
    "$TEST" \
    src/entity.c src/script.c src/maps_rt.c src/video.c src/dialogue.c \
    gen/tables.c gen/palette.c gen/bk*.c \
    -o "$HERE/simtest"
"$HERE/simtest"
