#!/bin/bash
# Run the REAL banked cart on the harness TMS9900 core (16-bit, real codegen,
# real banking).
#   tools/cartsim/run.sh [frames] [trace_pc|0]  sotb.bin: hold RIGHT from frame 5,
#                                               print map + player each frame
#   tools/cartsim/run.sh --walk                 sotb_walk.bin: the story
#                                               walkthrough (make walktest)
# Hook addresses come from cart.map / walk.map.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="$HERE/../.."
H=/home/tursi/newtms9900-gcc/test/harness
[ "$HERE/cartsim" -nt "$HERE/cartsim.cc" ] || g++ -O2 -I$H "$HERE/cartsim.cc" $H/cpu.o -o "$HERE/cartsim"
cd "$HERE"
if [ "$1" = "--walk" ]; then
    M="$PORT/walk.map"
    a(){ grep -E "^ +0x[0-9a-f]+ +$1\$" "$M" | awk '{print substr($1,15)}'; }
    exec ./cartsim --walk "$PORT/sotb_walk.bin" $(a _start) $(a vdpwaitvint) $(a kscanfast) \
         $(a joystfast) $(a charsetlc) $(a hs_fire) $(a hs_fire_auto) $(a wt_done) \
         $(a wt_res) $(a wt_n) 0
fi
M="$PORT/cart.map"
a(){ grep -E "^ +0x[0-9a-f]+ +$1\$" "$M" | awk '{print substr($1,15)}'; }
./cartsim "$PORT/sotb.bin" $(a _start) $(a vdpwaitvint) $(a kscanfast) $(a joystfast) \
          $(a charsetlc) $(a player) "${1:-12}" "${2:-0}" $(a cur_map)
