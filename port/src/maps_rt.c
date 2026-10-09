/* Runtime map loader - decodes the compact TI map stream (see convert.py).
 *
 * Stream: bank(1), border(1), colours[32], nametable byte-RLE of the map's own
 * halftone tile ids (3840 cells, storage order x+y*64), collision[480]
 * bitpacked, nent(1), nent*(type,tx,ty,key).
 * The map data lives in the switched map bank, so we page it in first and read
 * through the 0x6000 window; the decoded nametable/collision go to RAM.
 */
#include "game.h"
#include "entity.h"

unsigned char  nametable[SCROLL_X * SCROLL_Y];
unsigned char  collision[(SCROLL_X * SCROLL_Y) / 8];
int            map_chr;
unsigned char  map_colors[32];
int            map_border;
int cam_tx, cam_ty;
int cam_mode;
int cur_map = -1;                 /* map index currently loaded (debug/tests) */
static int cam_force;
static int hold_x = -1, hold_y;   /* scripted camera focus (pixels), -1 = off */
short globals[MAX_GLOBALS];

int is_blocked(int tx, int ty)
{
    int idx;
    if (tx < 0 || ty < 0 || tx >= SCROLL_X || ty >= SCROLL_Y) return 1;
    idx = tx + ty * SCROLL_X;
    return (collision[idx >> 3] >> (idx & 7)) & 1;
}

void map_load(int idx)
{
    const unsigned char *p;
    int pos, i, nent, cnt, val, t, tx, ty, k;
    Entity *cam;

    CART_BANK(map_phys[idx]);         /* page in this map's data bank */
    p = maps[idx];
    if (!p) return;
    cur_map = idx;

    map_chr = *p++;
    map_border = *p++;
    for (i = 0; i < 32; i++) map_colors[i] = *p++;
    room_bg_tint = room_fg_tint = 0;
    room_keep_black = 0;

    pos = 0;                          /* nametable byte-RLE */
    while (pos < SCROLL_X * SCROLL_Y) {
        cnt = *p++;
        val = *p++;
        while (cnt-- > 0) nametable[pos++] = (unsigned char)val;
    }

    for (i = 0; i < (int)sizeof(collision); i++) collision[i] = *p++;

    nent = *p++;
    ents_clear();
    for (i = 0; i < nent; i++) {
        t  = *p++;
        tx = *p++;
        ty = *p++;
        k  = *p++;
        ent_spawn(t, tx * TILE, ty * TILE, k);
    }

    /* Camera entity key: 0 no scroll, 1 horizontal, 2 vertical, 3 free.
     * Maps without one (the ending) follow freely. */
    cam = find_ent(E_CAMERA);
    cam_mode = cam ? (cam->key & 3) : 3;
    cam_tx = 0; cam_ty = 0;
    cam_force = 1;
    hold_x = -1;
    portal_cd = 12;                   /* let the player leave a portal before it can fire */
}

/* who the camera watches: the player, else the scene's lead character */
static Entity *cam_focus(void)
{
    Entity *f;
    if (player && player->y >= 0) return player;
    if ((f = find_ent(E_TWILIGHT)) != 0) return f;
    return find_ent(E_ZECORA);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void camera_reset(void) { cam_force = 1; }

/* cutscenes: look at a point (pixels) instead of the player, e.g. the NES
 * camera panning down to Twilight and the fallen tree */
void camera_hold(int x, int y)        /* moving it later is cheap: the view */
{                                     /* only flips when it nears an edge  */
    if (hold_x < 0) cam_force = 1;
    hold_x = x; hold_y = y;
}
void camera_release(void)      { hold_x = -1; cam_force = 1; }

/* The NES scrolled smoothly; the TI flips a 32x24 window. The window stays
 * inside what the original Camera mode could show (a "no scroll" axis keeps
 * to its 32x30 screen) and re-centres on the focus when it nears an edge. */
void camera_follow(void)
{
    int ptx, pty, minx, maxx, miny, maxy, cx, cy;
    Entity *f = cam_focus();
    if (hold_x >= 0) {
        ptx = clampi(hold_x >> 3, 0, SCROLL_X - 1);
        pty = clampi(hold_y >> 3, 0, SCROLL_Y - 1);
    } else {
        if (!f) return;
        ptx = clampi((f->x + f->w / 2) >> 3, 0, SCROLL_X - 1);
        pty = clampi((f->y + f->h / 2) >> 3, 0, SCROLL_Y - 1);
    }
    if (cam_mode & 1) { minx = 0; maxx = SCROLL_X; }
    else              { minx = (ptx / 32) * 32; maxx = minx + 32; }
    if (cam_mode & 2) { miny = 0; maxy = SCROLL_Y; }
    else              { miny = (pty / 30) * 30; maxy = miny + 30; }
    if (!cam_force &&
        ptx >= cam_tx + 3 && ptx <= cam_tx + SCREEN_W - 4 &&
        pty >= cam_ty + 3 && pty <= cam_ty + SCREEN_H - 4 &&
        cam_tx >= minx && cam_tx + SCREEN_W <= maxx &&
        cam_ty >= miny && cam_ty + SCREEN_H <= maxy)
        return;
    cx = clampi(ptx - SCREEN_W / 2, minx, maxx - SCREEN_W);
    cy = clampi(pty - SCREEN_H / 2, miny, maxy - SCREEN_H);
    if (cam_force || cx != cam_tx || cy != cam_ty) {
        cam_tx = cx;
        cam_ty = cy;
        draw_room();
    }
    cam_force = 0;
}
