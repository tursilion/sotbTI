/* Entity engine core - port of blank.Entity / blank.Ingame / Pony / Portal /
 * Fade. Per-class behaviour and the story scripts live in script.c.
 *
 * Update order follows Ingame.Update: every enabled entity updates, then the
 * list is sorted by y-centre (descending) for drawing - we sort an index
 * array so Entity pointers held by scripts stay valid.
 */
#include "vdp.h"
#include "kscan.h"
#include "entity.h"
#include "palette.h"

Entity entity[MAX_ENTITIES];
int entity_count;
Entity *player;
int fire_locked;
int pending_map = -1;             /* int, not signed char: this gcc port doesn't
                                     sign-extend char, so -1 read back as 0x00FF */
int portal_cd;                    /* ticks after a load during which exit
                                     portals only prime (no trigger) */
static int order[MAX_ENTITIES];   /* draw/update order (y-sorted indices) */
static int leave_col, leave_ticks;/* fade-out for the pending map change */

/* ------------------------------------------------------------------ */
/* sprite table management (SDT relocated to 0x1000 in video_init)     */
/* ------------------------------------------------------------------ */
static int spr_next;             /* next hardware sprite slot (0..31) */
static int pat_next;             /* next SDT pattern name (multiple of 4) */

static int revb(int b)
{
    b = ((b >> 4) | (b << 4)) & 0xff;
    b = ((b & 0xcc) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xaa) >> 1) | ((b & 0x55) << 1);
    return b;
}

/* copy one 8x8 CHR tile into an SDT quadrant; tile < 0 = empty quadrant */
static void quad(unsigned char *dst, int tile, int mirror, const unsigned char *chr)
{
    int i;
    if (tile < 0) { for (i = 0; i < 8; i++) dst[i] = 0; return; }
    tile &= 0xff;
    for (i = 0; i < 8; i++)
        dst[i] = mirror ? revb(chr[tile * 8 + i]) : chr[tile * 8 + i];
}

/* compose a 16x16 sprite pattern from four arbitrary CHR tiles (quadrants).
 * SDT byte order is TL, BL, TR, BR ("down-first"). mirror flips L/R + rows.
 * Pass -1 for a quadrant that should stay empty. */
static void compose16_q(int name, int tl, int bl, int tr, int br,
                        int mirror, const unsigned char *chr)
{
    unsigned char buf[32];
    if (!mirror) {
        quad(buf,      tl, 0, chr);
        quad(buf + 8,  bl, 0, chr);
        quad(buf + 16, tr, 0, chr);
        quad(buf + 24, br, 0, chr);
    } else {
        quad(buf,      tr, 1, chr);
        quad(buf + 8,  br, 1, chr);
        quad(buf + 16, tl, 1, chr);
        quad(buf + 24, bl, 1, chr);
    }
    vdpmemcpy(gSpritePat + name * 8, buf, 32);
}

/* pony/people art is 16x24: tiles base,base+1 / +16,+17 / +32,+33. A TI
 * sprite is 16 tall, so sample 16 of the 24 rows (drop every third row from
 * row 1) - keeps the whole pony (head, torso, mane, legs) slightly squashed. */
static const int srow[16] = { 0, 2, 3, 5, 6, 8, 9, 11,
                              12, 14, 15, 17, 18, 20, 21, 23 };

static void compose16(int name, int base, int mirror, const unsigned char *chr)
{
    unsigned char buf[32];
    int i, r, lt, rt, a, b;
    lt = mirror ? base + 1 : base;          /* source tile for the left column */
    rt = mirror ? base : base + 1;
    for (i = 0; i < 16; i++) {
        r = srow[i];
        a = chr[((lt + (r >> 3) * 16) & 0xff) * 8 + (r & 7)];
        b = chr[((rt + (r >> 3) * 16) & 0xff) * 8 + (r & 7)];
        if (mirror) { a = revb(a); b = revb(b); }
        buf[i] = a;                         /* SDT: left column rows 0-15 */
        buf[16 + i] = b;                    /*      right column rows 0-15 */
    }
    vdpmemcpy(gSpritePat + name * 8, buf, 32);
}

void sprites_begin(void) { spr_next = 0; pat_next = 0; }

void sprites_end(void)
{
    /* NOTE: libti99 delsprite() writes y=192 which only nudges ONE sprite off
     * screen - it is NOT a list terminator, so later sprites keep rendering.
     * Write the real 0xD0 terminator so everything past spr_next is hidden. */
    if (spr_next < 32) {
        VDP_SET_ADDRESS_WRITE(gSprite + (spr_next << 2));
        VDPWD(0xD0);
    }
}

void sprites_hide_all(void) { sprites_begin(); sprites_end(); }

static int on_screen(int sx, int sy)
{
    return !(sx <= -16 || sx >= 256 || sy <= -16 || sy >= 192);
}

static int alloc_name(void)
{
    int name;
    if (spr_next >= 30 || pat_next > 252) return -1;   /* out of room */
    name = pat_next; pat_next += 4;
    return name;
}

static void emit(int name, int sx, int sy, int col)
{
    /* a sprite partly off the LEFT edge can't have a negative X (it would
     * wrap to the right side): use the Early Clock bit, which draws it 32
     * pixels further left, so it clips at the edge like the NES */
    if (sx < 0) { col |= 0x80; sx += 32; }
    sprite(spr_next++, (unsigned char)name, (unsigned char)col,
           (unsigned char)(sy - 1), (unsigned char)sx);
}

/* 16x24 pony/people cell squashed into one sprite */
static void put_cell(int sx, int sy, int base, int mirror, int col,
                     const unsigned char *chr)
{
    int name = alloc_name();
    if (name < 0) return;
    compose16(name, base, mirror, chr);
    emit(name, sx, sy, col);
}

/* 16x16 sprite from four arbitrary CHR tiles (objects, doors, etc) */
static void put_tiles16(int sx, int sy, int tl, int tr, int bl, int br,
                        int mirror, int col, const unsigned char *chr)
{
    int name;
    if (!on_screen(sx, sy)) return;
    name = alloc_name();
    if (name < 0) return;
    compose16_q(name, tl, bl, tr, br, mirror, chr);
    emit(name, sx, sy, col);
}

static const unsigned char *page_chr(Entity *e)
{
    CART_BANK(chr_spr_phys[e->sprBank]);    /* page in this entity's CHR */
    return chr_spr[e->sprBank];
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
static void draw_pony(Entity *e)
{
    int sx = e->x - cam_tx * 8;
    int sy = e->y - cam_ty * 8 - 4;         /* feet on the box bottom, as
                                               the 24-tall original (y-12) */
    int body, hair, f;
    const unsigned char *chr;
    if (!on_screen(sx, sy)) return;         /* cull (avoids unsigned wrap) */
    chr = page_chr(e);
    if (!chr) return;

    if (e->type == E_NIGHTMARE && e->b < 36) {   /* Nightmare.Draw: rising */
        if (e->b == 0) return;
        f = e->b / 6;
        body = 156 + (f / 2) * 32 + (f % 2) * 2;
        if (f > 3) put_cell(sx, sy, body, e->mirror, e->bodyCol, chr);
        else put_tiles16(sx, sy, body, body + 1, body + 16, body + 17,
                         e->mirror, e->bodyCol, chr);   /* top 16 rows only */
        return;
    }
    body = e->bodyIndex * 56 + 8 * e->dir
         + (((e->dir > 1 && e->bodyIndex == 0) ||
             (e->dir > 0 && e->bodyIndex == 1)) ? 32 : 0)
         + (e->walking ? (2 + ((e->anim / e->animDelay) % 3) * 2) : 0);
    hair = 144 + e->hairIndex * 6 + (e->hairIndex > 1 ? 36 : 0) + 2 * e->dir;
    /* hair first = lower sprite index = drawn in front of body */
    if (e->drawHair) put_cell(sx, sy, hair, e->mirror, e->hairCol, chr);
    put_cell(sx, sy, body, e->mirror, e->bodyCol, chr);
}

/* Mitta / Three Leaf: two fixed 16x24 layers (158 outline + 156 body) */
static void draw_people(Entity *e)
{
    int sx = e->x - cam_tx * 8;
    int sy = e->y - cam_ty * 8 - 4;
    const unsigned char *chr;
    if (!on_screen(sx, sy)) return;
    chr = page_chr(e);
    if (!chr) return;
    put_cell(sx, sy, 158, e->mirror, e->hairCol, chr);
    put_cell(sx, sy, 156, e->mirror, e->bodyCol, chr);
}

/* tile-based objects */
static void draw_object(Entity *e)
{
    int sx = e->x - cam_tx * 8;
    int sy = e->y - cam_ty * 8;
    int t;
    const unsigned char *chr;
    if (sx <= -32 || sx >= 256 || sy <= -16 || sy >= 192) return;
    chr = page_chr(e);
    if (!chr) return;
    switch (e->type) {
        case E_DOOR:   /* double-height tiles: 236,237 locked / 238,239 open */
            if (e->key == 0) put_tiles16(sx, sy, 236, 237, 252, 253, 0, e->bodyCol, chr);
            else             put_tiles16(sx, sy, 238, 239, 254, 255, 0, e->bodyCol, chr);
            break;
        case E_BOX:    /* key 0: double-height 220,221; else 2x2 240-243 */
            if (e->key == 0) put_tiles16(sx, sy, 220, 221, 236, 237, 0, e->bodyCol, chr);
            else             put_tiles16(sx, sy, 240, 241, 242, 243, 0, e->bodyCol, chr);
            break;
        case E_ITEM:   /* one 8x8 tile; the gem adds an animated sparkle on
                        * top of it (same spot, front sprite, second colour).
                        * Key 2 (the well) is an invisible interaction spot. */
            if (e->key == 0)
                put_tiles16(sx, sy, 220 + (e->cycle / 3) % 4, -1, -1, -1, 0, e->hairCol, chr);
            if (e->key <= 1)
                put_tiles16(sx, sy, 204, -1, -1, -1, 0, e->bodyCol, chr);
            break;
        case E_TREE:   /* Everfree canopy: double-height 172-175 = 32x16 */
            put_tiles16(sx,      sy, 172, 173, 188, 189, 0, e->bodyCol, chr);
            put_tiles16(sx + 16, sy, 174, 175, 190, 191, 0, e->bodyCol, chr);
            break;
        case E_ORB:    /* 2x2, four animation frames */
            t = 240 + (e->cycle % 4) * 4;
            if (e->cycle % 4 == 3) t -= 8;
            put_tiles16(sx, sy, t, t + 1, t + 2, t + 3, 0, e->bodyCol, chr);
            break;
        default: break;
    }
}

static void ent_draw(Entity *e)
{
    switch (e->type) {
        case E_APPLEBLOOM: case E_GREYHOOF: case E_STARLET: case E_RONEO:
        case E_GLADSTONE: case E_RUBY: case E_ZECORA: case E_TWILIGHT:
        case E_NIGHTMARE: draw_pony(e); break;
        case E_MITTA:
        case E_THREELEAF: draw_people(e); break;
        case E_DOOR: case E_BOX: case E_ITEM: case E_TREE:
        case E_ORB:       draw_object(e); break;
        default: break;
    }
}

/* colours used by the sprites of this map (the player always included),
 * so set_room_colors can keep the background off them. Hidden entities
 * count too: Nightmares start invisible and rise later. */
unsigned int sprite_color_mask(void)
{
    unsigned int m = 0;
    int i;
    for (i = 0; i < entity_count; i++) {
        Entity *e = &entity[i];
        switch (e->type) {
            case E_APPLEBLOOM: case E_GREYHOOF: case E_STARLET: case E_RONEO:
            case E_GLADSTONE: case E_RUBY: case E_ZECORA: case E_TWILIGHT:
            case E_NIGHTMARE: case E_MITTA: case E_THREELEAF:
                if (e->drawHair) m |= (1u << e->hairCol);
                /* fall through */
            case E_DOOR: case E_BOX: case E_TREE: case E_ORB:
                m |= (1u << e->bodyCol);
                break;
            case E_ITEM:
                if (e->key <= 1) m |= (1u << e->bodyCol);
                if (e->key == 0) m |= (1u << e->hairCol);   /* gem sparkle */
                break;
            default: break;
        }
    }
    return m;
}

/* ------------------------------------------------------------------ */
/* shared helpers                                                      */
/* ------------------------------------------------------------------ */
/* xorshift16 - well-mixed bits (an LCG's low bits are too correlated) */
static unsigned int rng = 0xACE1;
unsigned int rnd(void)
{
    rng ^= (rng << 7) & 0xffff;
    rng ^= rng >> 9;
    rng ^= (rng << 8) & 0xffff;
    return rng;
}

static int iabs(int v) { return v < 0 ? -v : v; }

int box_hits(Entity *e, int x, int y)
{
    int l = x, r = x + e->w - 1, t = y, b = y + e->h - 1;
    if (is_blocked(l >> 3, t >> 3)) return 1;
    if (is_blocked(r >> 3, t >> 3)) return 1;
    if (is_blocked(l >> 3, b >> 3)) return 1;
    if (is_blocked(r >> 3, b >> 3)) return 1;
    return 0;
}

/* stand-in for Entity.MapCollision: move per axis, refusing blocked tiles.
 * Something already inside a wall (waypoint walks ignore walls, e.g.
 * Gladstone's nightmare ends its route half in one) may move freely until it
 * is out, as the original's push-out would let it. */
void move_collide(Entity *e, int dx, int dy)
{
    int stuck = box_hits(e, e->x, e->y);
    if (dx && (stuck || !box_hits(e, e->x + dx, e->y))) e->x += dx;
    if (dy && (stuck || !box_hits(e, e->x, e->y + dy))) e->y += dy;
}

int ent_overlap(Entity *a, Entity *b)
{
    return a->x < b->x + b->w && a->x + a->w > b->x &&
           a->y < b->y + b->h && a->y + a->h > b->y;
}

/* Entity.GetFacingDirection(t) from e: 0 = t is to the right of e,
 * 1 = below, 2 = left, 3 = above (atan2 quadrants, centre to centre) */
static int facing(Entity *e, Entity *t)
{
    int dx = (2 * e->x + e->w) - (2 * t->x + t->w);
    int dy = (2 * e->y + e->h) - (2 * t->y + t->h);
    if (iabs(dx) > iabs(dy)) return dx < 0 ? 0 : 2;
    return dy < 0 ? 1 : 3;
}

void face_toward(Entity *e, Entity *t)       /* Pony.Interact */
{
    switch (facing(e, t)) {
        case 0:  e->dir = 0; e->mirror = 0; break;
        case 1:  e->dir = 1; e->mirror = 0; break;
        case 2:  e->dir = 0; e->mirror = 1; break;
        default: e->dir = 2; e->mirror = 0; break;
    }
}

void pony_motion(Entity *e)                  /* Pony.Update */
{
    e->walking = 1;
    if (e->mvx < 0)      { e->dir = 0; e->mirror = 1; }
    else if (e->mvx > 0) { e->dir = 0; e->mirror = 0; }
    else if (e->mvy > 0) { e->dir = 1; e->mirror = 0; }
    else if (e->mvy < 0) { e->dir = 2; e->mirror = 0; }
    else e->walking = 0;
    if (e->walking) e->anim++;
}

/* one waypoint step (Twilight/Roneo/Nightmare): returns 1 when arrived */
int step_to(Entity *e, int tx, int ty, int v, int tol)
{
    int arrived = 0;
    tx *= 8; ty *= 8;
    if (tx < e->x - tol)      { e->x -= v; e->mvx = -1; }
    else if (tx > e->x + tol) { e->x += v; e->mvx = 1; }
    else                      { e->x = tx; e->mvx = 0; arrived++; }
    if (ty < e->y - tol)      { e->y -= v; e->mvy = -1; }
    else if (ty > e->y + tol) { e->y += v; e->mvy = 1; }
    else                      { e->y = ty; e->mvy = 0; arrived++; }
    return arrived == 2;
}

void freeze_all(Entity *except)
{
    int i;
    for (i = 0; i < entity_count; i++) {
        entity[i].held = entity[i].enabled;
        entity[i].enabled = 0;
    }
    if (except) except->enabled = 1;
}

void thaw_all(void)
{
    int i;
    for (i = 0; i < entity_count; i++) entity[i].enabled = entity[i].held;
}

Entity *find_ent(int type)
{
    int i;
    for (i = 0; i < entity_count; i++)
        if (entity[i].type == type) return &entity[i];
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fade (Fade.as) - the TI can't fade a palette, so the display is      */
/* blanked to a black or white backdrop for the length of the fade.     */
/* ------------------------------------------------------------------ */
#define FADE_TICKS 4             /* ~ one NES palette fade (FadeDelay 1) */
#define FADE_SLOW  16            /* FadeDelay 5 */

static void wait_ticks(int t)
{
    int f = (t * 60) / 25;       /* game ticks are 25 Hz, vblank is 60 Hz */
    while (f-- > 0) vdpwaitvint();
}

static void fade_hold(int col, int ticks)
{
    VDP_SET_REGISTER(VDP_REG_COL, col);
    video_blank();
    wait_ticks(ticks);
}

void screen_flash(int on, int color)
{
    if (on) { VDP_SET_REGISTER(VDP_REG_COL, color); video_blank(); }
    else    { set_room_colors(); video_unblank(); }
}

/* Fade keys: 0 Enter black, 1 Enter white, 2 Leave black, 3 Leave white,
 * 4 E/L black, 5 E/L white, 6 Enter black/leave white, 7 Enter white/leave
 * black, 8 Enter black slow/leave black, 9 Enter black/leave black slow,
 * 10 Enter white slow/leave white, 11 Enter white/leave white slow */
static const int fade_in_col[12]  = { 1,15,0,0, 1,15, 1,15, 1, 1,15,15 };
static const int fade_out_col[12] = { 0,0, 1,15, 1,15,15, 1, 1, 1,15,15 };

static int fade_key(void)
{
    Entity *f = find_ent(E_FADE);
    return (f && f->key >= 0 && f->key < 12) ? f->key : -1;
}

/* ------------------------------------------------------------------ */
/* map transitions (Portal.as / Maps.LoadMap)                          */
/* ------------------------------------------------------------------ */
void portal_jump(int key)           /* an exit portal fired (or a script's) */
{
    int k = fade_key();
    if (pending_map >= 0) return;
    /* SFX: Screen (audio.Play(2)) */
    globals[0] = key & 3;                    /* entrance id in the target map */
    pending_map = (key - 4) >> 2;
    leave_col = (k >= 0) ? fade_out_col[k] : 0;
    leave_ticks = (k == 9 || k == 11) ? FADE_SLOW : FADE_TICKS;
    freeze_all(0);                           /* Portal disables everything */
}

void load_map_direct(int m)
{
    if (pending_map >= 0) return;
    pending_map = m;
    leave_col = 0;
}

static void update_portal(Entity *e)
{
    int ov;
    if (e->key <= 3) return;                 /* entrance portal: passive */
    ov = (player && ent_overlap(player, e));
    /* fire only on a fresh entry and never during the post-load cooldown,
     * otherwise being placed beside a portal would chain through maps */
    if (ov && !e->state && !portal_cd) portal_jump(e->key);
    e->state = ov;
}

/* place the player at the entrance portal matching globals[0] */
static int place_at_entrance(void)
{
    int i;
    if (!player) return 0;
    for (i = 0; i < entity_count; i++) {
        Entity *e = &entity[i];
        if (e->type == E_PORTAL && e->key <= 3 && e->key == globals[0]) {
            player->x = e->x;
            player->y = e->y;
            player->mirror = (e->x > 100);   /* face inward (per Portal.as) */
            return 1;
        }
    }
    return 0;
}

void enter_map(int m)
{
    int i, k, entered;
    if (leave_col) fade_hold(leave_col, leave_ticks);
    else video_blank();
    leave_col = 0;
    keep_backdrop = 1;                       /* the screen stays the fade colour */
    if (m == 0) {                            /* Applebloom "Intro" */
        keep_backdrop = 0;
        title_screen();
        video_blank();
        keep_backdrop = 1;
        for (i = 0; i < MAX_GLOBALS; i++) globals[i] = 0;  /* ClearGlobalVariables */
        /* MUSIC: Intro (once) */
    }
    map_load(m);
    entered = place_at_entrance();
    for (i = 0; i < entity_count; i++) script_start(&entity[i]);
    video_load_chr(map_chr);
    set_room_colors();
    camera_reset();
    camera_follow();
    draw_room();
    ingame_draw();
    /* let the triggers look at the player once while still blanked: one that
     * sends her straight on (caught -> map 20 -> "back to town" -> the ruins)
     * then never shows the map in between */
    for (i = 0; i < entity_count; i++)
        if (entity[i].type == E_TRIGGER) script_update(&entity[i]);
    if (pending_map >= 0) {
        leave_col = 0;                       /* already dark: no second fade */
        return;
    }
    k = fade_key();                          /* Fade.OnEnter */
    if (entered && k >= 0 && fade_in_col[k])
        fade_hold(fade_in_col[k], (k == 8 || k == 10) ? FADE_SLOW : FADE_TICKS);
    keep_backdrop = 0;
    set_room_colors();                       /* the room's own backdrop */
    video_unblank();
}

/* ------------------------------------------------------------------ */
/* spawn / init                                                        */
/* ------------------------------------------------------------------ */
static int ti(int nes) { return nes_to_ti[nes & 63]; }

/* Explicit runtime init of mutable globals. The banked-cart crt0's .data
 * initialization is not reliably setting these (pending_map was reading 0
 * instead of -1, which triggered a bogus map load on frame 1), so we set them
 * here rather than trusting static initializers. */
void game_init(void)
{
    pending_map = -1;
    portal_cd = 0;
    fire_locked = 0;
    leave_col = 0;
    rng = 0xACE1;
}

void ents_clear(void) { entity_count = 0; player = 0; }

Entity *ent_spawn(int type, int x, int y, int key)
{
    Entity *e;
    if (entity_count >= MAX_ENTITIES) return 0;
    order[entity_count] = entity_count;
    e = &entity[entity_count++];
    e->type = type; e->x = x; e->y = y; e->key = key;
    e->enabled = 1; e->visible = 1; e->solid = 0; e->held = 1;
    e->dir = 1; e->mirror = 0; e->mvx = e->mvy = 0;
    e->walking = 0; e->anim = 0; e->animDelay = 2;
    e->bodyIndex = e->hairIndex = 0; e->drawHair = 1;
    e->sprBank = 2; e->bodyCol = COLOR_WHITE; e->hairCol = COLOR_WHITE;
    e->cycle = 0; e->state = 0; e->ai = 0;
    e->a = e->b = e->c = e->d = 0; e->wp = 0; e->nwp = 0;
    e->w = 16; e->h = 16;

    switch (type) {
        case E_APPLEBLOOM:
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 1;
            e->bodyIndex = 1; e->hairIndex = 1;
            e->bodyCol = ti(56); e->hairCol = ti(22);
            player = e;
            break;
        case E_GREYHOOF:    /* hair layer is mostly palette slot 1 (black) */
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 2;
            e->bodyCol = ti(16); e->hairCol = ti(63);
            break;
        case E_STARLET:
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 2;
            e->hairIndex = 2;
            e->bodyCol = ti(32); e->hairCol = ti(22);
            break;
        case E_RONEO:
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 2;
            e->hairIndex = 3;
            e->bodyCol = ti(56); e->hairCol = ti(17);  /* mane = slot 2 */
            break;
        case E_MITTA:   /* people: 156 layer (bodyCol) + 158 layer (hairCol) */
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 2;
            e->bodyCol = ti(16); e->hairCol = ti(6);
            break;
        case E_THREELEAF:   /* 156 layer green, 158 layer black detail */
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 3;
            e->mirror = 1;                   /* ThreeLeaf.mirrored = true */
            e->bodyCol = ti(25); e->hairCol = ti(63);
            break;
        case E_GLADSTONE:
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 3;
            e->hairIndex = 2;
            e->bodyCol = ti(24); e->hairCol = ti(39);  /* mane: AB body pal slot 2 */
            break;
        case E_RUBY:        /* bank follows the scene's sprite pattern */
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = (key == 1) ? 3 : 1;
            e->bodyIndex = 1; e->hairIndex = 3;
            e->bodyCol = ti(16); e->hairCol = ti(56);  /* mane: AB body pal slot 0 */
            break;
        case E_ZECORA:
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 1;
            e->hairIndex = 2;
            e->bodyCol = ti(16); e->hairCol = ti(15);
            break;
        case E_TWILIGHT:
            e->w = 12; e->h = 12; e->solid = 1; e->sprBank = 1;
            e->bodyCol = ti(51); e->hairCol = ti(3);
            break;
        case E_NIGHTMARE:
            e->w = 12; e->h = 12; e->sprBank = (key >= 8) ? 5 : 4;
            /* all-black in the original; give the mane a night-sky blue so
             * the two layers read as a pony instead of one silhouette */
            e->bodyCol = ti(31); e->hairCol = COLOR_DKBLUE;
            break;
        case E_DOOR:
            e->solid = 1; e->sprBank = 3;
            e->bodyCol = ti(28);
            break;
        case E_BOX:         /* crate (map 21) / debris (map 24) */
            e->w = 15; e->h = 15; e->solid = 1; e->sprBank = (key == 0) ? 2 : 4;
            e->bodyCol = ti(16);
            break;
        case E_ITEM:        /* gem (key 0, map 19) / crank (1, map 21) / well (2, map 20) */
            e->w = 8; e->h = 8; e->solid = 1; e->sprBank = (key == 1) ? 2 : 3;
            if (key == 0)      { e->bodyCol = ti(22); e->hairCol = ti(48); } /* gem + sparkle */
            else               e->bodyCol = ti(16);  /* crank: sub-palette 2 (grey);
                                                        red vanished on the red floor */
            break;
        case E_TREE:        /* Everfree canopy; the 64x64 trunk box blocks */
            e->w = 64; e->h = 64; e->solid = 1; e->sprBank = 1;
            e->bodyCol = COLOR_DKGREEN;
            break;
        case E_ORB:
            e->sprBank = 5; e->bodyCol = ti(48);
            break;
        default:
            break;
    }
    /* two-layer characters must never collapse into one solid colour */
    if (e->hairCol == e->bodyCol)
        e->hairCol = (e->bodyCol == COLOR_BLACK) ? COLOR_DKBLUE : COLOR_BLACK;
    return e;
}

/* ------------------------------------------------------------------ */
/* Ingame update/draw with y-sort (painter order)                      */
/* ------------------------------------------------------------------ */
static void ysort(void)                /* insertion sort, descending y-centre */
{
    int i, j, t, tc;
    for (i = 1; i < entity_count; i++) {
        t = order[i];
        tc = entity[t].y + entity[t].h / 2;
        j = i;
        while (j > 0 && entity[order[j - 1]].y + entity[order[j - 1]].h / 2 < tc) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = t;
    }
}

void ingame_update(void)
{
    int k;
    if (pending_map >= 0) {                 /* a portal/script asked for a map */
        int m = pending_map;
        pending_map = -1;
        enter_map(m);
        return;
    }
    if (portal_cd) portal_cd--;             /* prime exit portals after a load */
    if (dialogue_active()) { dialogue_update(); return; }   /* entities frozen */
    for (k = 0; k < entity_count; k++) {
        Entity *e = &entity[order[k]];
        if (!e->enabled) continue;
        if (e->type == E_PORTAL) update_portal(e);
        else script_update(e);
        e->cycle++;
        /* StartDialogue disables every entity; a LoadMap ends the frame */
        if (dialogue_active() || pending_map >= 0) break;
    }
    ysort();
}

void ingame_draw(void)
{
    int k;
    if (dialogue_active()) return;      /* dialogue owns the whole screen */
    sprites_begin();                    /* each entity pages in its own CHR */
    for (k = 0; k < entity_count; k++)
        if (entity[order[k]].visible) ent_draw(&entity[order[k]]);
    sprites_end();
}
