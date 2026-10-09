/* VDP setup and background rendering (Graphics I, 16x16 sprites).
 *
 * IMPORTANT VRAM layout note: libti99's set_graphics puts the background
 * pattern table (PDT) and the sprite pattern table (SDT) at the SAME address
 * (0x800). We relocate the SDT to 0x1000 so composing sprite patterns does not
 * clobber background tiles. VRAM map after video_init():
 *   SIT   0x000   (screen image table, 32x24)
 *   SAL   0x300   (sprite attribute list)
 *   Color 0x380   (32 entries, one fg/bg pair per 8-char group)
 *   PDT   0x800   (background patterns - the map's CHR _bg bank)
 *   SDT   0x1000  (sprite patterns - composed from the sprite CHR bank)
 */
#include "vdp.h"
#include "kscan.h"
#include "game.h"
#include "palette.h"
#include "entity.h"

#define SDT_ADDR 0x1000

/* Graphics I + 16x16 sprites mode-1 value, and screen blank/unblank used to
 * hide the room rebuild during a map transition (our stand-in for Fade). */
#define R1_ON (VDP_MODE1_16K | VDP_MODE1_UNBLANK | VDP_MODE1_INT | VDP_MODE1_SPRMODE16x16)
void video_blank(void)   { VDP_SET_REGISTER(VDP_REG_MODE1, R1_ON & ~VDP_MODE1_UNBLANK); }
#ifndef TI99
int shown_maps[16], shown_n;              /* host tests: maps actually shown */
#endif
void video_unblank(void)
{
#ifndef TI99
    if (shown_n < 16) shown_maps[shown_n++] = cur_map;
#endif
    VDP_SET_REGISTER(VDP_REG_MODE1, R1_ON);
}

void video_init(void)
{
    unsigned char i;
    set_graphics(VDP_SPR_16x16);        /* also sets KSCAN mirror correctly */
    /* give sprites their own pattern table so they don't overwrite bg tiles */
    VDP_SET_REGISTER(VDP_REG_SDT, SDT_ADDR / 0x800);
    gSpritePat = SDT_ADDR;
    for (i = 0; i < 32; i++) delsprite(i);
}

/* Title screen: text page using the console font, waits for a fire press.
 * (The original's scrolling NES title doesn't map to Graphics I, so this is a
 * clean static title.) The caller reloads scene CHR afterwards. */
void title_screen(void)
{
    unsigned char g;
    charsetlc();
    for (g = 0; g < 32; g++) vdpchar(gColor + g, (COLOR_WHITE << 4) | COLOR_DKBLUE);
    VDP_SET_REGISTER(VDP_REG_COL, COLOR_DKBLUE);
    vdpmemset(gImage, ' ', 32 * 24);
    writestring(7,  6, "STORY OF THE BLANKS");
    writestring(9,  5, "a flash game by Donitz");
    writestring(12, 0, "based on My Little Pony / Hasbro");   /* 32 chars */
    writestring(17, 11, "PRESS FIRE");
    video_unblank();                     /* map transitions run blanked */
    /* SFX: Next (audio.Play(7)) on the start press */
    /* wait for release, then a fresh press */
    do { kscanfast(1); } while (KSCAN_KEY == 18);
    do { kscanfast(1); } while (KSCAN_KEY != 18);
}

/* load the current map's halftone background CHR (built per map by
 * tools/bgdither.py). `bank` is the NES pattern bank the scene wants: maps
 * whose background switches (BoneFiends wake -> bank 8) carry a second CHR
 * on the same tile ids. */
void video_load_chr(int bank)
{
    const unsigned char *p = map_bg[cur_map];
    int phys = map_bg_phys[cur_map], i;
    if (map_bg2[cur_map] && bank == map_bg2_bank[cur_map]) {
        /* the variant carries its own colours: border(1), colours[32] */
        p = map_bg2[cur_map];
        CART_BANK(map_bg2_phys[cur_map]);
        room_keep_black = 1;             /* black walls, like the original */
        map_border = *p++;
        for (i = 0; i < 32; i++) map_colors[i] = *p++;
        vdpmemcpy(gPattern, p, map_bg_len[cur_map]);
        return;
    }
    if (!p) return;
    CART_BANK(phys);                     /* page in this CHR's data bank */
    vdpmemcpy(gPattern, p, map_bg_len[cur_map]);
}

/* Graphics I: one fg/bg pair per group of 8 characters (32 groups). The map
 * carries its own colour table (each halftoned tile's dark/light pair, tiles
 * grouped by pair). At run time the background (dark) colours are moved off
 * every colour a sprite in this map uses, and scripts can tint the room. */
/* nearest colour to c (c itself first) that is not in the 'used' bitmask */
static int avoid_color(int c, unsigned int used)
{
    int i;
    if (!(used & (1u << c))) return c;
    for (i = 0; i < 14; i++)
        if (!(used & (1u << ti_near[c][i]))) return ti_near[c][i];
    return c;
}

int room_bg_tint, room_fg_tint;           /* TI colour or 0 (scripts) */
int keep_backdrop;                        /* map transition: leave the (blanked)
                                             backdrop at the fade colour */
int room_keep_black;                      /* nightmare variant: walls stay black
                                             even though BoneFiends are black */
unsigned char room_colors[32];            /* the table as written (tests) */

void set_room_colors(void)
{
    unsigned int used = sprite_color_mask();
    int g, fg, bg, flat;
    if (room_keep_black) used &= ~(1u << COLOR_BLACK);
    for (g = 0; g < 32; g++) {
        fg = map_colors[g] >> 4;
        bg = map_colors[g] & 15;
        flat = (fg == bg);               /* a solid-colour group */
        if (room_fg_tint) fg = room_fg_tint;
        if (room_bg_tint) bg = room_bg_tint;
        /* keep the room off every colour a sprite in this map uses (black
         * manes, Nightmares, ...) so no character vanishes into the scenery */
        bg = avoid_color(bg, flat ? used : used | (1u << fg));
        if (flat) fg = bg;
        room_colors[g] = (unsigned char)((fg << 4) | bg);
        vdpchar(gColor + g, room_colors[g]);
    }
    bg = room_bg_tint ? room_bg_tint : map_border;
    /* a blanked display shows the backdrop over the whole screen, so while
     * a transition is in progress it must stay the fade colour */
    if (!keep_backdrop) VDP_SET_REGISTER(VDP_REG_COL, avoid_color(bg, used));
}

void draw_room(void)
{
    int r, c;
    unsigned char row[SCREEN_W];
    for (r = 0; r < SCREEN_H; r++) {
        int base = (cam_ty + r) * SCROLL_X + cam_tx;
        for (c = 0; c < SCREEN_W; c++)
            row[c] = nametable[base + c];
        vdpmemcpy(gImage + r * SCREEN_W, row, SCREEN_W);
    }
}

/* stand-in for the scripts' NESEngine.SetImagePaletteColor effects: force
 * every group's background (dark) and/or foreground (light) colour */
void room_tint(int bg, int fg)
{
    room_bg_tint = bg;
    room_fg_tint = fg;
    if (!dialogue_active()) set_room_colors();
}

/* Credits.as - the original printed these into the map and scrolled the
 * camera down cycle/2 px. Rows are the original map rows. */
static const struct { int row, col; const char *s; } credit_lines[] = {
    { 31, 13, "CREDITS" },
    { 33, 12, "DEVELOPER" },
    { 34, 12, "DONITZ" },
    { 36, 14, "MUSIC" },
    { 38,  3, "CRTified Technician" },
    { 39,  3, "Friendship is magic Theme" },
    { 41,  3, "Zero V2        CC BY NC SA" },
    { 42,  3, "Naked to the Errant Breeze" },
    { 43,  3, "I Mourn For My Dear Ones" },
    { 45,  3, "DarkNES        CC BY NC SA" },
    { 46,  3, "Cartomancy" },
    { 47,  3, "Mystic. no percussion" },
    { 48,  3, "Tenebrous Neuroticism" },
    { 50, 10, "Beta tester" },
    { 51, 10, "capt slowpoke" },
    { 53,  9, "TI-99/4A port" },
    { 54,  8, "PORTED BY TURSI" },
    { 56,  9, "My Little Pony" },
    { 57,  5, "Friendship is Magic is" },
    { 58,  3, "copyright to Hasbro Studios" },
};
#define N_CREDITS ((int)(sizeof(credit_lines) / sizeof(credit_lines[0])))

void credits_begin(void)
{
    int g;
    video_blank();                       /* hide the font/colour switch */
    sprites_hide_all();
    charsetlc();
    for (g = 0; g < 32; g++) vdpchar(gColor + g, (COLOR_WHITE << 4) | COLOR_BLACK);
    VDP_SET_REGISTER(VDP_REG_COL, COLOR_BLACK);
    vdpmemset(gImage, ' ', 32 * 24);
    video_unblank();
}

void credits_scroll(int top)
{
    int i, r;
    vdpmemset(gImage, ' ', 32 * 24);
    for (i = 0; i < N_CREDITS; i++) {
        r = credit_lines[i].row - top;
        if (r >= 0 && r < SCREEN_H)
            writestring(r, credit_lines[i].col, (char *)credit_lines[i].s);
    }
}
