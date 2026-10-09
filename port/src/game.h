/* Story of the Blanks - TI-99/4A port
 * Common game types and declarations.
 *
 * Ported from the AS3 engine (blank.*). The original is an NES-style game with
 * a software PPU (blank.NESEngine). On the TI we use VDP Graphics I: a 32x24
 * tile background (room-flip, no scrolling) plus 8x8 hardware sprites.
 *
 * Map coordinate system: the full map is SCROLL_X x SCROLL_Y (64x60) tiles,
 * i.e. 512x480 pixels. Entities carry pixel positions in that full-map space.
 * The camera shows one 32x24 "room" at a time (SCREEN_W x SCREEN_H tiles) and
 * flips when the player crosses a room boundary.
 */
#ifndef GAME_H
#define GAME_H

#include "banks.h"           /* SCROLL_X/Y, MAP/DIALOGUE counts, resource tables */

#define SCREEN_W   32
#define SCREEN_H   24        /* TI shows 24 of the NES's 30 rows */
#define TILE       8


/* Banked cartridge: writing to 0x6000 + 2*bank pages an 8K data bank into the
 * 0x6000 window. Switch to a resource's bank before reading it. This is a
 * harmless no-op on the non-banked EA5 build (data lives in the fixed region
 * there), so the same C works for both builds. Physical bank numbers must match
 * the elf2bankedcart bank map (user data banks start at 3). */
#ifdef TI99
#define CART_BANK(b) (*(volatile unsigned char *)(0x6000 + ((b) << 1)) = 0)
#else
#define CART_BANK(b) ((void)(b))         /* host sim: data is flat */
#endif
/* physical bank per resource comes from the generated *_phys[] tables */

/* Entity type indices (from blank.Entity.CreateEntity switch) */
#define E_BLANK     0
#define E_GREYHOOF   1
#define E_RONEO      2
#define E_STARLET    3
#define E_THREELEAF  4
#define E_GLADSTONE  5
#define E_MITTA      6
#define E_APPLEBLOOM 16      /* the player */
#define E_CAMERA     18
#define E_DOOR       19
#define E_TREE       20
#define E_FADE       21
#define E_TRIGGER    22
#define E_ITEM       24
#define E_NIGHTMARE  29
#define E_BOX        30
#define E_PORTAL     31
#define E_RUBY       33
#define E_CREDITS    34
#define E_TWILIGHT   35
#define E_ZECORA     41

/* ---- decoded current map (lives in RAM) -------------------------------- */
extern unsigned char  nametable[SCROLL_X * SCROLL_Y];   /* tile index per cell, [x + y*64] */
extern unsigned char  collision[(SCROLL_X * SCROLL_Y) / 8]; /* 1 bit per cell */
extern int            map_chr;                           /* CHR bank index for this map */
extern unsigned char  map_colors[32];                    /* TI fg<<4|bg per 8-tile group */
extern int            map_border;                        /* TI backdrop colour */
extern int room_bg_tint, room_fg_tint;   /* script tints (TI colour, 0 = none) */
extern int room_keep_black;              /* nightmare variant keeps black walls */
extern int keep_backdrop;                /* transition: don't touch the backdrop */
extern unsigned char room_colors[32];    /* colour table as last written */

/* ---- camera (room origin, in tiles) ------------------------------------ */
extern int cam_tx, cam_ty;

/* globals shared by entity logic (Entity.globalVariables), 64 of them */
#define MAX_GLOBALS 64
extern short globals[MAX_GLOBALS];

/* fire/action is edge-triggered (Input.IsKeyHeld(key, true)): set while the
 * button is held, so one press = one action; dialogue_end sets it too */
extern int fire_locked;

/* ---- api --------------------------------------------------------------- */
void map_load(int idx);               /* decode maps[idx] into RAM, spawn entities */
int  is_blocked(int tx, int ty);      /* collision bit test (returns 0/1) */

void video_init(void);                /* Graphics I + load CHR bank to VDP */
void video_load_chr(int bank);
void draw_room(void);                 /* blit current 32x24 room to the screen */
void set_room_colors(void);           /* pick Graphics I char-group colors */
void video_blank(void);
void video_unblank(void);
void title_screen(void);
void room_tint(int bg, int fg);       /* script room tint, TI colours (0 = off) */
void credits_begin(void);             /* Credits.as text page */
void credits_scroll(int top);         /* show credit rows top..top+23 */

void camera_follow(void);             /* flip the view to keep the focus on screen */
void camera_reset(void);              /* force a re-centre on the next follow */
void camera_hold(int x, int y);       /* cutscene: centre on a point (pixels) */
void camera_release(void);            /* back to following the player */
extern int cam_mode;                  /* Camera key: bit0 horizontal, bit1 vertical */
extern int cur_map;                   /* map index currently loaded */
unsigned int sprite_color_mask(void); /* bit n set = TI colour n used by a sprite */

/* map transitions (Portal). pending_map >= 0 requests a load next tick. */
extern int pending_map;
extern int portal_cd;                 /* post-load exit-portal priming window */
void enter_map(int m);

/* dialogue system (dialogue.c) */
void dialogue_start(int id);
void dialogue_update(void);
int  dialogue_active(void);
int  dialogue_portrait(void);         /* skeleton portrait showing (tests) */

#endif
