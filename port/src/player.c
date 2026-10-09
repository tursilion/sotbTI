/* Player (Applebloom) - movement, tile collision, 16x16 sprite rendering.
 *
 * Ported from blank.entities.Pony / Applebloom. Values from Applebloom.as:
 *   collision box 12x12; BodyIndex=1, HairIndex=1; BodyPalette 0, HairPalette 1;
 *   body palette (NES) 56/47/39, hair 22/20/48; sprite CHR bank = 1 (Everfree).
 * Pony.Draw body tile: BodyIndex*56 + 8*dir + (dir>0 && BodyIndex==1 ? 32:0)
 *                       + (walking ? 2+((frame/delay)%3)*2 : 0)
 * Pony.Draw hair tile: 144 + HairIndex*6 + 2*dir   (HairIndex=1 -> 150+2*dir)
 *
 * Sprite patterns are composed from the sprite CHR bank into the SDT in TI
 * down-first quadrant order: TL=base, BL=base+32, TR=base+1, BR=base+33.
 */
#include "vdp.h"
#include "game.h"
#include "chr.h"
#include "palette.h"
#include "kscan.h"

#define PL_W 12
#define PL_H 12
#define SPD  3                 /* Applebloom.Update uses 3 px/step */

#define BODY_NAME 0            /* SDT slot (16x16 => multiple of 4) */
#define HAIR_NAME 4

static unsigned char anim;
static int last_body = -1, last_hair = -1;
static unsigned char last_mirror = 2;

/* reverse the 8 bits of a byte (horizontal flip of one tile row) */
static unsigned char revb(unsigned char b)
{
    b = (unsigned char)((b >> 4) | (b << 4));
    b = (unsigned char)(((b & 0xcc) >> 2) | ((b & 0x33) << 2));
    b = (unsigned char)(((b & 0xaa) >> 1) | ((b & 0x55) << 1));
    return b;
}

/* compose a 16x16 sprite pattern (32 bytes) from four sprite-CHR tiles.
 * The TMS9918 has no sprite-flip bit, so left-facing is done in software:
 * swap the left/right quadrants and bit-reverse every row. */
static void compose16(int name, int base, unsigned char mirror)
{
    const unsigned char *chr = chr_bank1;        /* Applebloom's sprite bank */
    unsigned char buf[32];
    int i;
    int tl = base & 0xff, bl = (base + 32) & 0xff;
    int tr = (base + 1) & 0xff, br = (base + 33) & 0xff;
    if (!mirror) {
        for (i = 0; i < 8; i++) buf[i]      = chr[tl * 8 + i];
        for (i = 0; i < 8; i++) buf[8 + i]  = chr[bl * 8 + i];
        for (i = 0; i < 8; i++) buf[16 + i] = chr[tr * 8 + i];
        for (i = 0; i < 8; i++) buf[24 + i] = chr[br * 8 + i];
    } else {                                     /* left quadrant = flipped right */
        for (i = 0; i < 8; i++) buf[i]      = revb(chr[tr * 8 + i]);
        for (i = 0; i < 8; i++) buf[8 + i]  = revb(chr[br * 8 + i]);
        for (i = 0; i < 8; i++) buf[16 + i] = revb(chr[tl * 8 + i]);
        for (i = 0; i < 8; i++) buf[24 + i] = revb(chr[bl * 8 + i]);
    }
    vdpmemcpy(gSpritePat + name * 8, buf, 32);
}

static int box_hits(int x, int y)
{
    int l = x, r = x + PL_W - 1, t = y, b = y + PL_H - 1;
    if (is_blocked(l >> 3, t >> 3)) return 1;
    if (is_blocked(r >> 3, t >> 3)) return 1;
    if (is_blocked(l >> 3, b >> 3)) return 1;
    if (is_blocked(r >> 3, b >> 3)) return 1;
    return 0;
}

static void reflip_if_needed(void)
{
    int ptx = (pl_x + PL_W / 2) >> 3;
    int pty = (pl_y + PL_H / 2) >> 3;
    int cx = (ptx / SCREEN_W) * SCREEN_W;
    int cy = (pty / SCREEN_H) * SCREEN_H;
    if (cx > SCROLL_X - SCREEN_W) cx = SCROLL_X - SCREEN_W;
    if (cy > SCROLL_Y - SCREEN_H) cy = SCROLL_Y - SCREEN_H;
    if ((unsigned char)cx != cam_tx || (unsigned char)cy != cam_ty) {
        cam_tx = (unsigned char)cx;
        cam_ty = (unsigned char)cy;
        draw_room();
    }
}

void player_update(void)
{
    signed char jx, jy;
    int nx = pl_x, ny = pl_y;

    joystfast(1);                      /* joystick 1 direction; pure CRU, no VDP R1 */
    jx = (signed char)KSCAN_JOYX;
    jy = (signed char)KSCAN_JOYY;

    pl_walking = 0;

    if (jx < 0)      { nx -= SPD; pl_dir = 0; pl_mirror = 1; pl_walking = 1; }
    else if (jx > 0) { nx += SPD; pl_dir = 0; pl_mirror = 0; pl_walking = 1; }
    if (nx < 0) nx = 0;
    if (nx > SCROLL_X * 8 - PL_W) nx = SCROLL_X * 8 - PL_W;
    if (!box_hits(nx, pl_y)) pl_x = nx;

    if (jy > 0)      { ny -= SPD; pl_dir = 2; pl_walking = 1; }   /* up */
    else if (jy < 0) { ny += SPD; pl_dir = 1; pl_walking = 1; }   /* down */
    if (ny < 0) ny = 0;
    if (ny > SCROLL_Y * 8 - PL_H) ny = SCROLL_Y * 8 - PL_H;
    if (!box_hits(pl_x, ny)) pl_y = ny;

    if (pl_walking) anim++;
    reflip_if_needed();
}

void player_draw(void)
{
    int sx = pl_x - (int)cam_tx * 8;
    int sy = pl_y - (int)cam_ty * 8 - 12;      /* art sits 12px above the box */

    int body = 56 + 8 * pl_dir
             + ((pl_dir > 0) ? 32 : 0)
             + (pl_walking ? (2 + ((anim >> 1) % 3) * 2) : 0);
    int hair = 150 + 2 * pl_dir;

    unsigned char body_col = nes_to_ti[56 & 63];   /* body palette colour 0 */
    unsigned char hair_col = nes_to_ti[22 & 63];   /* hair palette colour 0 */

    if (body != last_body || pl_mirror != last_mirror)
        compose16(BODY_NAME, body, pl_mirror);
    if (hair != last_hair || pl_mirror != last_mirror)
        compose16(HAIR_NAME, hair, pl_mirror);
    last_body = body; last_hair = hair; last_mirror = pl_mirror;

    /* hair = sprite 0 (on top of body = sprite 1); VDP y is top scanline - 1 */
    sprite(0, HAIR_NAME, hair_col, (unsigned char)(sy - 1), (unsigned char)sx);
    sprite(1, BODY_NAME, body_col, (unsigned char)(sy - 1), (unsigned char)sx);
    delsprite(2);
}
