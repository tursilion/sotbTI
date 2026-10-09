# Story of the Blanks — TI-99/4A port — DESIGN & STATUS (handoff)

Living design doc so a fresh session can resume. Pairs with the Claude memory
files (`sotb-ti99-port`, `gcc9900-use-int-not-char`, `tool-install-is-users-job`).

## Goal
Port the Flash game *Story of the Blanks* (donitz; `../story_of_the_blanks_by_donitz_d3kq34i.swf`,
AS3, decompiled to `../extracted/`) to the TI‑99/4A in C using libti99. The Flash
game is an NES‑style engine (`blank.NESEngine` = software PPU). On TI we use VDP
**Graphics I** (32×24 tiles) + **16×16 hardware sprites**, no scrolling.

## Project owner's rules (IMPORTANT)
- **Use `int`, not `char`**, except for true 8‑bit hardware values (VDP regs,
  byte buffers, asset bytes, tile/color indices). gcc9900 char handling is
  unreliable (init + sign‑extension). See `gcc9900-use-int-not-char` memory.
- **Don't assume compiler/hardware bugs — verify or ask.** The CPU simulator,
  banking, and Classic99 are all well proven; suspect our code first.
- Owner provisions tools/OS (no apt/downloads by us).
- Sound/music: **not ported** — leave `/* SFX/MUSIC: ... */` comment markers at
  trigger points. Big images → PNG + stub loader. Small gfx → single‑color 8×8/
  16×16 sprites (pick nearest VDP color).

## Toolchain & build
- Compiler: `/home/tursi/newtms9900-gcc/newgcc9900/bin/tms9900-{gcc,as,ld}` (gcc 4.4.0).
- libti99: `/mnt/d/work/libti99ALL` (lib at `buildti/libti99.a`). Headers: vdp.h, kscan.h, etc.
- Banked‑cart packer: `/mnt/d/work/ti/Elf2BankedCart/elf2bankedcart` (up to 32MB; data banks start at **physical bank 3**).
- Build the cart:  `cd port && BANKED=1 ALL=1 python3 tools/convert.py && make -f Makefile.cart`
  → `sotb.bin` (256KB cart ROM). Load in Classic99 as a **cartridge** (needs 32K RAM).
- `tms9900-as` dialect: directives need **leading whitespace**; `#` comments
  (not `*`); `data`/`byte`/`.ascii`/`.even`/`.word` OK; `text`/`aorg`/comma‑`data` NOT.
- `crt0_cart.asm` = cart header + .data copy + .bss clear + copies fixed 24K
  (banks 0‑2) to >A000 RAM + bank switching. Title string must stay 20 chars
  (keeps the 46‑byte header). `.data` init: the loader section must end word-aligned (`.even` at the end
  of crt0_cart.asm) - it used to be 0x12d bytes, so the .data image sat at an
  odd ROM address and the word-move copy shifted every initialised variable
  by one byte (libti99's gColor/gPattern/vsetchar... included). Fixed
  2026-10-08; `game_init()` still sets the game's critical globals too.
- `cart.ld` is **generated** by convert.py (`gen/cart.ld`): loader@0x6000,
  fixed@0xA000 (**0x5E00**, not 24K: crt0 copies only banks 0–2 minus the
  header/loader/.data, ~0x5E62 bytes, so a bigger fixed region would silently
  lose its tail — the linker now refuses instead), .data/.bss@0x2000 (8K, stack from ~0x3ff6), bankN sections
  → CARTN @0x602e. `fixed` must EXCLUDE `*bk[0-9][0-9].o` from BOTH .text AND
  .rodata (this gcc puts const data in **.text**).

## Asset converter — `tools/convert.py`
Reads `../extracted/` (AS3 + exported PNGs). Env: `BANKED=1 ALL=1` for the full
cart (also `SPR=`, `BG=`, `MAPS=`, `DLG=` to subset). `emit_banked()`:
- Sprite CHR: 128×128 PNGs → 256 TMS9918 8×8 patterns (`chr_bankN`,
  transparent→off).
- Background CHR: **per map, halftoned** (`tools/bgdither.py`). The NES colours
  a bg pixel from its CHR index + the cell's attribute sub-palette (red/white/
  blue → sub-palette slots 0/1/2, black → backdrop pal[3]).
  - One NES→TI colour decision per map (`colour_map`), so a colour looks the
    same in every tile: greys map only to black/grey/white; dark NES greens/
    teals/browns keep their hue; shades that collapse onto one TI colour are
    spread around the group's most-used shade (which stays solid) — darker
    shades halftone toward a darker TI colour, lighter toward a lighter one.
  - Each tile uses the two TI colours carrying most of it (drawn exactly);
    leftover colours halftone by luminance between them. Halftone = 4×4
    clustered-dot screen rounded DOWN to 0/25/50/75/100% (light = set pixel,
    25% is a 2×2 clump).
  - Font tiles (224–255: ! ? . , A–Z; 213–223 are box-frame pieces in the bg
    banks) never halftone and get at least 80
    luminance of contrast (darker side → black), so text stays readable.
  - Results are deduped, packed by colour pair into the 32 Graphics I colour
    groups, and the nametable is remapped. Each group is then flipped (patterns
    inverted, nibbles swapped) if needed so its background nibble is the colour
    covering most of the map — the run-time sprite-colour protection moves
    backgrounds, so it then acts on what fills the screen.
  - `TEXT_PATCH` rewrites the tutorial hints for the TI: map 0 "USE JOYST TO
    MOVE", map 1 "FIRE TO INTERACT" (same lengths; the bg font has no digits). Maps 11–16 get a second variant (bank 8 + the BoneFiend
  palette: red floor, black walls) on the same tile ids, with its own colour
  decisions; its blob is border(1) + colours[32] + CHR, loaded by
  video_load_chr, which also sets room_keep_black (the walls stay black even
  though the BoneFiends are black, as in the original). Map 0 uses bank 6 + the intro's palette (as the original
  intro switches to it). `python3 tools/bgdither.py MAP out.png` previews
  NES vs TI.
- Maps: `compact_map_bg()` re‑encodes each of 32 maps to a compact byte
  stream: `bank(1), border(1), colours[32] (TI fg<<4|bg per group), nametable
  byte‑RLE of per-map tile ids (3840 cells, storage x+y*64), collision[480]
  bitpacked, nent(1), nent×(type,tx,ty,key)`. Plus blobs `map_bg_N` (and
  `map_bg2_N`) with the map's halftone CHR.
- Dialogues: parses `Dialogue.as` StartDialogue cases → per‑dialogue
  strings/pointer‑array/names.
- Packs all blobs first‑fit‑decreasing into 8146‑byte banks (phys 3+), emits:
  `gen/bkNN.c` (per‑bank arrays; stale ones are deleted), `gen/tables.c` (FIXED
  manifest: `chr_spr[16]`, `chr_spr_phys[16]`, `maps[32]`, `map_phys[32]`,
  `map_bg/_phys/_len[32]`, `map_bg2/_phys/_bank[32]`, `dialogues[]`,
  `dlg_phys[]`), `gen/banks.h`, `gen/cart.ld`.
- NES 64‑color palette → nearest TI color: `gen/palette.{c,h}` (`nes_to_ti[64]`).

## Banking model
- `CART_BANK(b)` (game.h) = write `0x6000 + 2*b` to page an 8K bank into the
  0x6000 window (no‑op off‑target via `#ifdef TI99`). Switch the resource's
  physical bank (from `*_phys[]`) **before** reading it. Pointer tables live in
  FIXED/RAM; the pointers they hold are 0x602e+ window addresses (two banks can
  share address 0x602e — distinguished only by the phys bank you select).
- Access points: `map_load`→`CART_BANK(map_phys[idx])`; `video_load_chr`→
  `CART_BANK(map_bg_phys[cur_map])` (or map_bg2 when the scene switched bank); `draw_pony`/`draw_people`/`draw_object`→
  `CART_BANK(chr_spr_phys[e->sprBank])` per entity; `dialogue_start`→
  `CART_BANK(dlg_phys[id])` (stays switched through the conversation).
- Code runs from >A000 RAM (always resident) so bank‑switching never pages out
  code. Data‑only banks ⇒ no trampolines needed.

## VRAM layout (libti99 Graphics I, after `video_init`)
SIT 0x000 · SAL 0x300 · Color 0x380 (32 groups, 1 fg/bg pair per 8 chars) ·
PDT 0x800 (bg CHR) · **SDT relocated to 0x1000** (sprite patterns; libti99
defaults SDT=PDT=0x800 which we must NOT use, or sprite composes clobber bg).

## Source files (`src/`)
- `game.h` — E_* ids, CART_BANK, SCREEN_W/H, shared prototypes (map, video,
  camera, dialogue, transitions).
- `maps_rt.c` — `map_load()` (compact decode into RAM `nametable[3840]`,
  `collision[480]`, spawns entities, reads the Camera mode), `is_blocked()`,
  `camera_follow()`, `cur_map`, `globals[64]`.
- `video.c` — `video_init` (Graphics I, SDT→0x1000), `video_load_chr`,
  `draw_room`, `set_room_colors` (avoids sprite colours), `room_tint`
  (script tints), `video_blank/unblank`, `title_screen`,
  `credits_begin/credits_scroll`.
- `entity.h/.c` — engine core: all-`int` Entity struct, `ent_spawn`, the update
  loop (25 Hz tick, dialogue freeze, index-array y-sort so Entity pointers stay
  valid), sprite composition/drawing (ponies, people, Nightmare rising frames,
  door/box/item/tree/orb), collision helpers, Pony motion/facing, waypoint
  stepping, freeze/thaw, Portal + Fade (`portal_jump`, `load_map_direct`,
  `enter_map`, blanked-display fades).
- `script.c` — every per-class Update/Interact from the AS3 (see "Scripting").
- `dialogue.c` — full‑screen text page (charsetlc font; the display is blanked
  while switching to and from it, as is the credits page), one letter per tick
  with markers `< > digit / - *`, fire to continue/fast‑forward; all 35
  conversations. `*` (dialogue 24) turns the room's detail colour red and
  shows the fireplace skeleton: the original's face-6 portrait (pattern bank
  13), bone pixels doubled to 8×8 characters (codes 128–191, `skel_chr` in its
  own data bank), red on black, kept up to the end of the conversation.
- `main.c` — `game_init(); video_init(); enter_map(0);` then a 25 Hz logic
  tick derived from the 60 Hz vblank.

## Rendering
- Camera: the 64×60 tile map is shown 32×24 at a time. `camera_follow`
  re-centres the window on the player (else Twilight/Zecora in cutscene maps)
  when the player gets within 3 tiles of an edge, staying inside what the map's
  Camera entity allowed (key 0 no scroll, 1 horizontal, 2 vertical, 3 free).
  A "no scroll" axis keeps to its 32×30 NES screen. No Camera entity = free.
  Cutscenes can `camera_hold(x,y)` / `camera_release()` (Twilight's log scene
  centres between her and the tree; Ruby's first appearance tracks the point
  between Ruby and Applebloom; the finale holds rows 8–31 so Ruby and all
  four nightmares are in view), standing in for the NES pans. The Ruins
  arrival skips its pan and pause entirely (owner's call).
- Ponies = body layer + hair layer, two overlaid 16×16 sprites (single color
  each). 16×16 mode avoids the 4‑sprite/line limit that 8×8 would hit.
- No hardware sprite flip → left‑facing composed in software (`compose16` mirror:
  swap L/R quadrants + `revb` each row). SDT pattern "name" is a multiple of 4.
- `delsprite()` (libti99) only moves one sprite to y=192; it is NOT a list
  terminator. Use `sprites_end()`/`sprites_hide_all()` which write the real
  **0xD0** terminator at the first unused slot.
- Sprites partly off the LEFT edge use the Early Clock bit (colour byte bit 7,
  X + 32) instead of a negative X, which would wrap to the right side.
- Cull entities outside the current room before `sprite()` (screen pos passed as
  unsigned char ⇒ off‑room coords wrap mod 256). `draw_pony`/etc return if
  sx<=-16||sx>=256||sy<=-16||sy>=192.

## Sprite colours (2026-10-08)
- NES sprite palettes are GLOBAL (4 shared sub-palettes), so an NPC's mane
  often uses colours set by another character (e.g. Starlet's mane = AB hair
  palette, Gladstone/Ruby manes = AB body palette). Each layer's TI colour =
  the NES colour of that layer's dominant pixel slot in the resolved palette.
  ent_spawn falls back to a contrast colour if body == hair. Nightmare is all
  black in the original, so it gets a creative dark-blue mane.
- `tools/sprite_preview.py out.png [squash]` renders the NES original next to
  the TI two-sprite render, so you can check without Classic99 (keep its table
  in sync with ent_spawn).
- Pony/people art is 16x24 (tiles T, T+16, T+32). compose16 samples 16 of the
  24 rows (srow[]: drops every third row), so the whole pony shows. The owner
  chose this over the old T + T+32 (no torso). Sprites draw at y-4, so the feet
  sit on the 12px box bottom like the original 24-tall art at y-12.
  `sprite_preview.py out.png old` shows the previous look.
- Items: gem = tile 204 + sparkle 220-223 overlaid (same 8x8 spot, 2 sprites),
  from sprite bank 3. Door/Box bottom halves were wrongly tile 0 (now the
  double-height +16 tiles). compose16_q takes -1 for an empty quadrant.

## Room colours vs sprites
- Each map carries its own 32-entry colour table (see Asset converter).
  `set_room_colors()` moves every group's background (dark) colour, and the
  backdrop, off every colour a sprite in the map uses (`sprite_color_mask()`,
  hidden Nightmares included) via `ti_near[c][]`, keeping it different from
  that group's foreground. Script effects use `room_tint(bg, fg)` (final
  entrance → black, finale clearing → dark green; the dialogue-24 scare sets a
  red foreground tint). Tints reset on map load.
- MAX_ENTITIES is 32 (maps 11 and 23 have 30 and 32 entities).

## Entities (type ids in game.h)
Player E_APPLEBLOOM(16). Ponies: GreyHoof(1), Roneo(2), Starlet(3),
Gladstone(5), Ruby(33), Zecora(41), Twilight(35), Nightmare(29). People:
Mitta(6), ThreeLeaf(4). Objects: Door(19), Box(30), Item(24), Tree(20), and
E_ORB(42, spawned by the finale). Logic-only: Portal(31), Camera(18),
Fade(21), Trigger(22), Credits(34).

## Scripting (script.c) — the whole game, ported 2026-10-08
- Map transitions (`enter_map`): the display stays blanked with the backdrop
  at the fade colour (`keep_backdrop`) until the new scene is drawn - a
  blanked TMS9918 shows the backdrop over the whole screen, so changing it
  mid-load flashed. Triggers get one look at the player before the display
  comes on; if one sends her straight on (caught in the ruins -> map 20 ->
  "back to town"), the in-between map is never shown. Being caught pulses
  light/dark red.
- Timing: the original runs at **25 updates/s** (Game.GameUpdatesPerSecond)
  and every script is timed in those ticks, so main.c runs one logic tick per
  25/60 of a vblank. Movement is 3 px/tick = the original 75 px/s.
- `script_start` = the AS3 `cycle == 0` block (run once after the map loads
  and the player is placed). `script_update` = Update(). `script_interact` =
  Interact() (fire is edge-triggered, one press = one action).
- Story flags (globals): 0 entrance id, 1 chapter (0 start, 1 met Zecora,
  2 path blocked, 3 Ruby seen, 4/5 yells, 6 discovery, 7 final entrance,
  9 finale), 2 Roneo/Starlet (0 sad, 1 gem, 2 happy), 3 crank, 4 well key,
  5 Sunny Town greeting, 6 Ruby's 2nd appearance, 10/11/12 nightmare triggers.
- Ported: Applebloom (Meeting, Wait, Sunny town, Ruins, Escape, Final),
  Twilight (Meeting, all lead-the-way routes and map hand-offs, Zecora meeting,
  Blocked path tree cutscene, Escape, finale), Zecora, Ruby, all 10 Nightmare
  kinds (rising animation, waypoints with dialogues, random delays, chase,
  proximity wake-up, catch → flash → dialogue 31 → reload map 17/20), all 24
  Trigger kinds, Roneo (waypoints to Starlet), Starlet, GreyHoof, Gladstone,
  Mitta, ThreeLeaf, Door, Item (gem/crank/well), Box pushing + Box.Update
  (boxes shove themselves out of any solid entity, e.g. a Nightmare knocking
  the debris aside), solid-entity push-out, Credits/The end, title → new game.
- Deliberate TI difficulty change (owner: audience isn't gamers): the Starlet
  nightmare in the ruins wakes STARLET_GRACE (25 ticks = 1 s) after its trigger
  instead of at once - in the original it ambushes you at the mouth of the
  debris corridor you're slowly pushing through.
- TI approximations: NES camera pans are paged with camera_hold for the log
  scene and Ruby; the Ruins pan/pause is dropped; Final and the ThreeLeaf
  trigger pans are not paged; palette fades are blanked-display holds in
  black/white; only image-palette slots 0/1 affect the TI room (other
  SetImagePaletteColor / SetSpritePaletteColor changes are ignored);
  MapCollision is a per-axis "don't enter blocked tiles" check (an entity
  already inside a wall, e.g. at a waypoint, may move until it is out); the scrolling
  intro is the static title (no C-for-credits key); sound is comment markers.

## Input
`joystfast(1)` fills KSCAN_JOYX/JOYY (direction); `kscanfast(1)` fills KSCAN_KEY
(fire=18). Movement reads joystick: `jx = KSCAN_JOYX; if (jx>=0x80) jx-=256;`
(int). `kscanfast` does NOT fill the joystick axes — use `joystfast` for motion.

## Debugging / tests
- **Story walkthrough** `tools/hostsim/walkthrough.c` plays the whole game
  (teleporting between points; portals, triggers, cutscenes, dialogues and
  Nightmares run for real) and checks 75 story/map milestones.
  - Host (32-bit): `tools/hostsim/run.sh tools/hostsim/walkthrough.c`
  - TMS9900 (16-bit, real cart code + banking): `make -f Makefile.cart walktest`
    (builds `sotb_walk.bin` with walkthrough.c as main, runs it in cartsim).
    Camera-framing checks are host-only (`#ifndef TI99`, with an `#else` that
    keeps the same ticks) so the test cart fits the fixed region. Run both
    after script changes.
- **Host sim** `tools/hostsim/run.sh <file.c>` — native build of the game logic
  with stub vdp.h/kscan.h (CART_BANK no‑op, flat data). Input: KSCAN_JOYX/Y
  directly, `hs_fire`, or `hs_fire_auto` (alternating presses). `test.c` is a
  scratch driver. CAVEAT: host int is 32‑bit, TI is 16‑bit.
- **Cart emulator** `tools/cartsim/run.sh [frames] [trace_pc|0]` — runs the REAL
  `sotb.bin` on the harness TMS9900 core (`~/newtms9900-gcc/test/harness/cpu.o`)
  with cart bank switching + VDP ports. It intercepts vdpwaitvint, kscanfast,
  joystfast and charsetlc by PC (addresses from cart.map), holds RIGHT from
  frame 5, and prints map + player x/y each vblank. `--walk` mode runs the
  walkthrough cart. It catches target-only (16-bit, codegen, banking) bugs.
  After a run, `python3 tools/cartsim/screenshot.py out.png` renders the TI
  screen (tiles + sprites) from the VRAM dump.
- After building, scan the generated .s for the corrupt-prologue pattern (see
  RESOLVED below): `subreg extend` right after a `mov rN,*r0+` register save.
- Classic99 (owner runs) for visual.

## STATUS
- Verified on cart by the owner: title, tile backgrounds, two-colour ponies,
  talking, map transitions, room colours that avoid sprite colours.
- 2026-10-08: the whole game's scripting is ported (see "Scripting"); the
  75-check story walkthrough passes on the host and on the TMS9900 core with
  identical results. Awaiting the owner's Classic99 playtest.
- Budgets: fixed code 0x4a90 of 24K; RAM .bss ends 0x39fa (stack from 0x3ff6,
  peak use ~112 bytes in the walkthrough).

## RESOLVED — AB couldn't move in Sunny Town (2026-10-08)
- Symptom: Applebloom turned with input but never moved.
- Evidence (via `tools/cartsim`, running the real sotb.bin): map/collision
  decode in RAM was byte-identical to the map data, but every frame one
  `box_hits` probe hit garbage tiles like (128,32), which returned "blocked".
- Cause: the gcc -Os output for `box_hits` had a corrupt register-save
  prologue — `mov r13,r0 ; sra r0,8 ; movhi subreg extend ; mov r0,*r0+` in
  place of `mov r13,*r0+`. That clobbered the save pointer, so r13–r15 (holding
  the caller's x/y) came back wrong. It was triggered by `Entity.w/h` being
  `unsigned char` (byte→int extension). This was the only such sequence in the
  project's generated .s files.
- Fix: `Entity.w/h` → `int` (the owner's int-not-char rule). Verified in cartsim:
  x 120→123→…→141 at 3 px/frame, matching the host sim.
- Lesson: Entity is now all `int` (done with the scripting rewrite).
  Makefile.cart now tracks header deps (-MMD -MP), so editing a .h rebuilds what uses it.

## TODO
- Owner playtest of the full game in Classic99 (scripts verified in host sim
  and on the TMS9900 core, but not yet seen on screen).
- Optional polish: camera pans for the cutscenes, dialogue portraits, more
  palette effects (red windows, BoneFiend palettes), the 4-sprites-per-line
  limit when many Nightmares share a row, a title "credits" key.
