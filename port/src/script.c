/* Per-class behaviour and the story scripts - port of blank.entities.*.
 *
 * script_start()    = the AS3 "if (cycle == 0)" set-up block, run once when
 *                     the entity appears (after the player is placed)
 * script_update()   = Update() for one 25 Hz tick
 * script_interact() = Interact(), called when the player presses fire nearby
 *
 * Global progress flags (Entity.globalVariables):
 *   0 entrance id for the next map      1 story chapter (see below)
 *   2 Roneo/Starlet (0 sad, 1 gem found, 2 happy)    3 crank found
 *   4 well key found   5 Sunny Town greeting done   6 Ruby 2nd appearance seen
 *   10/11/12 ThreeLeaf/Roneo/Mitta nightmare trigger progress
 * Chapter (global 1): 0 start, 1 met Zecora, 2 path blocked, 3 Ruby seen,
 *   4/5 yells, 6 discovery (town turns dark), 7 final entrance, 9 finale.
 *
 * TI adaptation: NES camera pans and palette fades are approximated (the
 * window flips to keep the action on screen, fades blank the display), and
 * sound is left as SFX/MUSIC comment markers.
 */
#include "vdp.h"
#include "kscan.h"
#include "entity.h"

static int iabs(int v) { return v < 0 ? -v : v; }

static void start_dlg(int id) { if (!dialogue_active()) dialogue_start(id); }

/* "if Math.random() < p": p in percent */
static int chance(int pct) { return (int)(rnd() % 100) < pct; }

static void random_turn(Entity *e)            /* GreyHoof/Starlet/Gladstone */
{
    if (rnd() % 100 == 0) {                   /* Math.random() < 0.01 */
        switch (rnd() & 3) {
            case 0:  e->dir = 0; e->mirror = 0; break;
            case 1:  e->dir = 1; e->mirror = 0; break;
            case 2:  e->dir = 0; e->mirror = 1; break;
            default: e->dir = 2; e->mirror = 0; break;
        }
    }
}

/* ================================================================== */
/* Applebloom (the player)                                             */
/* keys: 0 In Everfree Forest, 1 Meeting, 2 Wait, 3 Sunny town, 4 Ruins, */
/*       5 Escape, 6 Final, 7 Intro                                    */
/* ================================================================== */
void applebloom_wait(int ticks)               /* Applebloom.Wait */
{
    if (!player) return;
    player->key = 2;
    player->ai = ticks;
}

static void start_final(Entity *e)            /* Applebloom.StartFinal */
{
    e->key = 6;
    e->ai = 1000;
    /* the NES camera rises with her into the clearing; hold the TI window on
     * its middle (rows 8-31) so Ruby and all four nightmares (rows 11 and
     * 29) are in view as they appear */
    camera_hold(SCREEN_W * 4, 20 * 8);
}

/* push the player out of solid entities (Applebloom.Update), nudging boxes */
static void box_nudge(Entity *b, int dx, int dy)
{
    int i, ox = b->x, oy = b->y;
    b->x += dx; b->y += dy;
    if (box_hits(b, b->x, b->y)) { b->x = ox; b->y = oy; return; }
    for (i = 0; i < entity_count; i++) {      /* Box.Update: boxes don't stack */
        Entity *o = &entity[i];
        if (o == b || o == player || !o->solid) continue;
        if (ent_overlap(b, o)) { b->x = ox; b->y = oy; return; }
    }
}

static void solid_pushout(Entity *e)
{
    int i, dx, dy, ov, f, push, ox = e->x, oy = e->y;
    for (i = 0; i < entity_count; i++) {
        Entity *o = &entity[i];
        if (o == e || !o->solid) continue;
        dx = (2 * e->x + e->w) - (2 * o->x + o->w);
        dy = (2 * e->y + e->h) - (2 * o->y + o->h);
        if (iabs(dx) >= e->w + o->w || iabs(dy) >= e->h + o->h) continue;
        /* GetFacingDirection(o): 0 o is right, 1 below, 2 left, 3 above */
        if (iabs(dx) > iabs(dy)) f = dx < 0 ? 0 : 2;
        else                     f = dy < 0 ? 1 : 3;
        push = (o->type == E_BOX) && (o->key == 0 || e->cycle % 3 == 0);
        if (f == 0 || f == 2) {
            ov = (e->w + o->w - iabs(dx) + 1) / 2;
            if (f == 0) { e->x -= ov; if (push) box_nudge(o, 1, 0); }
            else        { e->x += ov; if (push) box_nudge(o, -1, 0); }
        } else {
            ov = (e->h + o->h - iabs(dy) + 1) / 2;
            if (f == 1) { e->y -= ov; if (push) box_nudge(o, 0, 1); }
            else        { e->y += ov; if (push) box_nudge(o, 0, -1); }
        }
    }
    if (box_hits(e, e->x, e->y)) { e->x = ox; e->y = oy; }   /* MapCollision */
}

/* Box.Update: a box shoves itself out of any solid entity overlapping it
 * (a Nightmare walking through the debris knocks it aside), then
 * MapCollision - here: a move into a wall is refused */
static void update_box(Entity *b)
{
    int i, dx, dy, ox, oy;
    for (i = 0; i < entity_count; i++) {
        Entity *o = &entity[i];
        if (o == b || !o->solid) continue;
        dx = (2 * b->x + b->w) - (2 * o->x + o->w);
        dy = (2 * b->y + b->h) - (2 * o->y + o->h);
        if (iabs(dx) >= b->w + o->w || iabs(dy) >= b->h + o->h) continue;
        ox = b->x; oy = b->y;
        if (iabs(dx) > iabs(dy)) {               /* o is left/right of the box */
            if (dx < 0) b->x -= (b->w + o->w + dx + 1) / 2;
            else        b->x += (b->w + o->w - dx + 1) / 2;
        } else {
            if (dy < 0) b->y -= (b->h + o->h + dy + 1) / 2;
            else        b->y += (b->h + o->h - dy + 1) / 2;
        }
        if (box_hits(b, b->x, b->y)) { b->x = ox; b->y = oy; }
    }
}

static void applebloom_control(Entity *e)
{
    int jx, jy, i, dx, dy, fire;
    joystfast(1);
    jx = KSCAN_JOYX; if (jx >= 0x80) jx -= 256;
    jy = KSCAN_JOYY; if (jy >= 0x80) jy -= 256;
    e->mvx = (jx < 0) ? -1 : (jx > 0) ? 1 : 0;
    e->mvy = (jy > 0) ? -1 : (jy < 0) ? 1 : 0;    /* joystick up = +y */
    move_collide(e, 3 * e->mvx, 3 * e->mvy);
    if (e->x < 0) e->x = 0;
    if (e->x > SCROLL_X * 8 - e->w) e->x = SCROLL_X * 8 - e->w;
    if (e->y < 0) e->y = 0;
    if (e->y > SCROLL_Y * 8 - e->h) e->y = SCROLL_Y * 8 - e->h;
    solid_pushout(e);

    /* action (fire) - one press, one interaction */
    kscanfast(1);
    fire = (KSCAN_KEY == 18);
    if (fire && !fire_locked) {
        fire_locked = 1;
        for (i = 0; i < entity_count; i++) {
            Entity *o = &entity[i];
            if (o == e || !o->visible) continue;
            dx = iabs((2 * e->x + e->w) - (2 * o->x + o->w));
            dy = iabs((2 * e->y + e->h) - (2 * o->y + o->h));
            if (dx < e->w + o->w + 20 && dy < e->h + o->h + 20) {
                script_interact(o);
                if (dialogue_active()) break;
            }
        }
    }
    if (!fire) fire_locked = 0;
}

/* nightmares in their array order (the finale counts them) */
static void final_nightmares(int activate_upto, int enable_all)
{
    int i, n = 0;
    for (i = 0; i < entity_count; i++) {
        Entity *m = &entity[i];
        if (m->type != E_NIGHTMARE) continue;
        if (enable_all) { m->enabled = 1; continue; }
        if (++n < activate_upto) {           /* Activate(own key), held still */
            m->a = 1; m->solid = 1; m->visible = 1;
            m->enabled = 0;
        }
    }
}

static void applebloom_final(Entity *e)
{
    int t = e->ai, i;
    Entity *ruby = find_ent(E_RUBY), *orb, *twi;
    if (t > 890) {                            /* walk up into the clearing */
        /* MUSIC: fade out */
        e->mvy = -1; e->mvx = 0;
        e->y -= 1;
        if (t < 900) e->x += 1;
    } else if (t > 850) {
        e->mvy = 0;
    } else if (t == 850) {
        if (ruby) { ruby->dir = 0; ruby->mirror = 0; }
        start_dlg(32);
        e->dir = 0; e->mirror = 1;
    } else if (t > 770) {
        /* MUSIC: Trapped (loop) */
        final_nightmares(6 - (t - 770) / 20, 0);
    } else if (t == 740) {
        if (ruby) { ruby->dir = 1; ruby->mirror = 0; }
        start_dlg(33);
        ent_spawn(E_ORB, 300, 120, 0);
    } else if (t == 700) {
        final_nightmares(0, 1);
    } else if (t <= 620 && t > 580) {
        if ((orb = find_ent(E_ORB)) != 0) { orb->x -= (t - 570) / 7; orb->y -= 2; }
    } else if (t == 580) {
        screen_flash(1, COLOR_WHITE);         /* Fade.FadeToWhite */
        /* SFX: Orb (audio.Play(3)); MUSIC: stop */
    } else if (t == 550) {
        e->mirror = 0; e->dir = 0;
        room_tint(COLOR_DKGREEN, 0);          /* the clearing brightens (pal0 = 11) */
        twi = ent_spawn(E_TWILIGHT, 300, 170, 10);
        if (twi) script_start(twi);
        for (i = 0; i < entity_count; i++)
            if (entity[i].type == E_NIGHTMARE || entity[i].type == E_RUBY)
                entity[i].visible = 0;
        screen_flash(0, 0);                   /* Fade.FadeFromWhite */
    } else if (t < 550) {
        twi = find_ent(E_TWILIGHT);
        orb = find_ent(E_ORB);
        if (!twi) return;
        if (t > 500)      { twi->mvx = -1; twi->x -= 3; }
        else if (t > 480) { twi->mvx = 0; }
        else if (t == 480) start_dlg(34);
        else if (t > 420) {
            if (orb) { orb->x += 4; orb->y += 1; }
            e->mvx = 1; e->x += 3;
            twi->x += 3; twi->mvx = 1;
        } else if (t == 420) {
            portal_jump(116);                 /* -> area 28, the escape */
            e->enabled = 0;
        }
    }
}

static void update_applebloom(Entity *e)
{
    if (e->ai > 0) {
        e->ai--;
        switch (e->key) {
            case 1:                           /* Meeting */
                if (e->ai > 60) { e->mvx = 1; e->x += 3; }
                else { e->dir = 1; e->mvx = 0; }
                break;
            case 2:                           /* Wait */
                e->mvx = e->mvy = 0;
                break;
            case 3:                           /* Sunny town */
                if (e->ai > 30) {
                    if (globals[5] == 1) e->ai = 0;
                    e->mvy = -1; e->y -= 3;
                } else if (e->ai > 1) {
                    e->dir = 0; e->mirror = 1; e->mvy = 0;
                } else if (e->ai == 0 && find_ent(E_GREYHOOF)) {
                    globals[5] = 1;
                    start_dlg(10);
                }
                break;
            case 5:                           /* Escape */
                e->y += 2; e->mvy = 1;
                break;
            case 6:                           /* Final */
                applebloom_final(e);
                break;
            default: break;
        }
        pony_motion(e);
        return;
    }
    applebloom_control(e);
    pony_motion(e);
}

/* ================================================================== */
/* Twilight Sparkle                                                    */
/* keys: 0 Meeting, 1 Lead the way, 2 ..Opening, 3 ..Passage, 4 ..Forest, */
/*       5 ..River, 6 ..Entrance, 7 Zecora meeting, 8 Blocked path,     */
/*       9 Escape, 10 Final.  a = transport key, b = next key,          */
/*       c = proximity, d = cutscene freeze done                        */
/* ================================================================== */
static const int wp_meeting[]   = { 14,-5 };
static const int wp_open0[]     = { 17,48, 17,34, 12,27, 12,2, 17,1 };
static const int wp_open1[]     = { 17,3, 20,18, 18,33, 15,53, 16,59 };
static const int wp_pass0[]     = { 16,49, 16,38, 17,31, 15,22, 16,11, 15,0 };
static const int wp_pass1[]     = { 15,8, 16,17, 15,42 };
static const int wp_forest[]    = { 21,43, 17,33, 24,26, 33,27, 41,29, 47,23, 49,0 };
static const int wp_river[]     = { 46,47, 46,38, 32,38, 32,24, 36,22, 36,9, 30,0 };
static const int wp_entrance[]  = { 15,6 };
#define NWP(a) ((int)(sizeof(a) / sizeof(a[0]) / 2))

static void set_wp(Entity *e, const int *wp, int n) { e->wp = wp; e->nwp = n; }

static void lead(Entity *e, const int *wp, int n)
{
    e->key = 1; e->ai = 2;
    set_wp(e, wp, n);
}

static void start_twilight(Entity *e)
{
    int i;
    Entity *t;
    e->a = -1; e->b = -1; e->c = 50;
    switch (e->key) {
        case 0: e->ai = 100; break;
        case 7: e->ai = 50; break;
        case 9: e->ai = 1000; break;
        case 2:                                   /* Opening */
            if (globals[1] == 0) { lead(e, wp_open0, NWP(wp_open0)); e->a = 36; }
            else {
                lead(e, wp_open1, NWP(wp_open1)); e->a = 29;
                e->x = wp_open1[0] * 8; e->y = wp_open1[1] * 8;
            }
            break;
        case 3:                                   /* Passage */
            if (globals[1] == 0) { lead(e, wp_pass0, NWP(wp_pass0)); e->a = 32; }
            else {
                lead(e, wp_pass1, NWP(wp_pass1));
                e->c = 70;
                for (i = 0; i < 4; i++) {         /* the fallen trees */
                    t = ent_spawn(E_TREE, 110 + (i % 2) * 4,
                                  350 + (i > 0 ? 30 : 0) + i * 9, 0);
                    if (t && i == 0) t->a = 1;    /* the one she throws */
                }
                e->x = wp_pass1[0] * 8; e->y = wp_pass1[1] * 8;
                e->b = 8;
            }
            break;
        case 4: lead(e, wp_forest, NWP(wp_forest)); break;
        case 5: lead(e, wp_river, NWP(wp_river)); break;
        case 6: lead(e, wp_entrance, NWP(wp_entrance)); break;
        default: break;
    }
}

static Entity *thrown_tree(void)
{
    int i;
    for (i = 0; i < entity_count; i++)
        if (entity[i].type == E_TREE && entity[i].a == 1) return &entity[i];
    return 0;
}

static void twilight_lead(Entity *e)
{
    Entity *ab = player;
    if (e->nwp > 0 && ab && iabs(ab->x - e->x) < e->c && iabs(ab->y - e->y) < e->c) {
        e->ai = 50;
        if (step_to(e, e->wp[0], e->wp[1], 3, 2)) {
            e->wp += 2; e->nwp--;
            if (e->nwp == 0) {
                if (e->a > -1) {                  /* lead on into the next map */
                    portal_jump(e->a);
                    if (player) player->enabled = 0;
                }
                if (e->b != -1) { e->key = e->b; e->ai = 170; }
            }
        }
    } else {
        if (e->ai < 20 && ab) { face_toward(e, ab); e->ai = 19; }
        e->mvx = e->mvy = 0;
    }
}

static void twilight_blocked(Entity *e)
{
    int t = e->ai;
    Entity *tree = thrown_tree();
    if (t > 150) {                                /* camera pans down to her */
        /* MUSIC: fade out */
        if (!e->d) {
            freeze_all(e); e->d = 1;
            /* the NES pans down; centre the TI window between Twilight and
             * the tree she is about to throw so the whole scene is in view */
            camera_hold(tree ? (e->x + tree->x + 16) / 2 : e->x,
                        tree ? (e->y + tree->y + 8) / 2 : e->y);
        }
    } else if (t == 150) {
        start_dlg(4);
    } else if (t > 110 && t < 150) {
        if (tree) tree->y -= 1;                   /* she lifts the tree */
    } else if (t > 80 && t < 100) {
        if (tree) { tree->x += 20; tree->y -= 5; }   /* ...and throws it */
        /* SFX: Throw (t == 90) */
    } else if (t == 70) {
        start_dlg(5);
    } else if (t == 20) {
        start_dlg(6);
    } else if (t == 19) {
        thaw_all();
        camera_release();
        globals[1] = 2;
    }
}

static void update_twilight(Entity *e)
{
    if (e->ai > 0) {
        e->ai--;
        switch (e->key) {
            case 0:                               /* Meeting */
                if (e->ai < 60 && e->ai > 10) { e->mvy = -1; e->y -= 3; }
                else {
                    e->dir = 2; e->mvy = 0;
                    if (e->ai == 0) {
                        start_dlg(1);
                        e->key = 1; e->ai = 2;
                        set_wp(e, wp_meeting, 1);
                    }
                }
                break;
            case 7:                               /* Zecora meeting */
                if (e->ai > 30) { e->mvy = -1; e->y -= 2; }
                else { e->dir = 2; e->mvy = 0; }
                break;
            case 1: twilight_lead(e); break;
            case 8: twilight_blocked(e); break;
            case 9: e->y += 2; e->mvy = 1; break; /* Escape */
            default: break;
        }
    }
    pony_motion(e);
}

/* ================================================================== */
/* Zecora (map 8: the meeting, then back to the forest)                */
/* ================================================================== */
static void update_zecora(Entity *e)
{
    Entity *twi = find_ent(E_TWILIGHT);
    e->ai--;
    if (e->ai > 60)      { e->mvy = 1; e->y += 1; }
    else if (e->ai > 50) { e->mvy = 0; }
    else if (e->ai == 50) start_dlg(2);
    else if (e->ai < 40) {
        e->mvy = -1; e->y -= 1;
        if (twi) { twi->mvy = 1; twi->y += 2; }
        if (e->ai == 11) {
            e->y = -50;
            start_dlg(3);
        } else if (e->ai == 10) {
            globals[0] = 1;
            globals[1] = 1;
            load_map_direct(7);
        }
    }
    pony_motion(e);
}

/* ================================================================== */
/* Ruby - a scripted apparition with no dialogue of her own            */
/* keys: 0 First appearance, 1 Second appearance, 2 Final, 3 Dark       */
/* ================================================================== */
static void ruby_vanish(Entity *e)
{
    e->visible = 0; e->enabled = 0; e->solid = 0;
    if (e->key == 0) camera_release();
}

static void update_ruby(Entity *e)
{
    int t;
    e->mvx = e->mvy = 0;
    if (e->ai > 0) {
        t = --e->ai;
        switch (e->key) {
            case 0:                               /* walk off to the left */
                /* the NES pans up to her; keep both her and Applebloom in
                 * the TI window by watching the point between them */
                if (player) camera_hold((e->x + player->x) / 2 + 6,
                                        (e->y + player->y) / 2 + 6);
                if (t < 80 && t > 25) { e->mvx = -1; e->x -= 2; }
                else if (t == 25) ruby_vanish(e);
                break;
            case 1:                               /* run up and away, once */
                if (globals[6] == 1) ruby_vanish(e);
                else if (t > 10) { e->mvy = -1; e->y -= 3; }
                else if (t == 10) { globals[6] = 1; ruby_vanish(e); }
                break;
            case 3:                               /* unseen; speaks once */
                e->visible = 0;
                if (t == 100) start_dlg(35);
                break;
            default: break;
        }
    }
    pony_motion(e);
}

/* ================================================================== */
/* Nightmares                                                          */
/* keys: 0 GreyHoof 1 Gladstone 2 ThreeLeaf 3 Starlet 4-6 Roneo 1-3     */
/*       7 Mitta 8 BoneFiend 9 Final boneFiend                         */
/* a = activated, b = rising animation (0..36), c = delay, d = caught,  */
/* state = waypoints passed                                            */
/* ================================================================== */
typedef struct {
    int vel, minD, maxD, pct;         /* speed, random delay range, chance % */
    int dangerous, chase, drawHair, hair;
    int dlgAt, dlg;                   /* dialogue at waypoint # (-1 none) */
    int rising, upd, animD, prox;
    const int *wp; int nwp;
} NightmareDef;

static const int nw0[] = { 26,-2 };
static const int nw1[] = { 11,44, 15,35, 18,31, 26,31 };
static const int nw2[] = { 10,14 };
static const int nw3[] = { 36,32, 35,39, 38,47 };
static const int nw4[] = { 13,8, 14,30 };
static const int nw5[] = { 24,23 };
static const int nw6[] = { 3,18 };
static const int nw7[] = { 14,55 };
static const int nw9[] = { 15,23 };

static const NightmareDef nm_def[10] = {
    { 1,0,0,0,   0,0,0,0,  1,25, 36,1,4,0,  nw0, 1 },   /* GreyHoof   */
    { 1,0,2,20,  1,1,1,3,  2,27,  0,1,4,0,  nw1, 4 },   /* Gladstone  */
    { 1,1,7,0,   1,0,1,0,  1,26,  0,1,3,0,  nw2, 1 },   /* ThreeLeaf  */
    { 5,3,5,20,  1,1,1,2, -1,28,  0,1,1,0,  nw3, 3 },   /* Starlet    */
    { 2,1,3,0,   1,0,1,3,  2,29,  0,1,2,0,  nw4, 2 },   /* Roneo 1    */
    { 2,1,3,0,   1,0,1,2, -1,29,  0,1,2,0,  nw5, 1 },   /* Roneo 2    */
    { 2,1,3,0,   1,0,1,0, -1,29,  0,1,2,0,  nw6, 1 },   /* Roneo 3    */
    { 1,0,0,0,   0,0,0,0,  1,30,  0,1,2,0,  nw7, 1 },   /* Mitta      */
    { 1,1,1,10,  1,1,0,0, -1,0,   0,1,2,70, 0,   0 },   /* BoneFiend  */
    { 1,0,0,0,   0,0,0,0, -1,0,  36,3,5,0,  nw9, 1 },   /* Final      */
};

static void nightmare_activate(int key)       /* Nightmare.Activate */
{
    int i;
    for (i = 0; i < entity_count; i++) {
        Entity *m = &entity[i];
        if (m->type == E_NIGHTMARE && m->key == key) {
            m->a = 1; m->solid = 1; m->visible = 1;
        }
    }
}

static void start_nightmare(Entity *e)
{
    const NightmareDef *d = &nm_def[e->key % 10];
    e->visible = 0; e->solid = 0;
    e->drawHair = d->drawHair; e->hairIndex = d->hair;
    e->animDelay = d->animD;
    e->b = d->rising;
    set_wp(e, d->wp, d->nwp);
    if (e->key == 8) {                            /* BoneFiend */
        e->enabled = (globals[1] == 6);
        if (e->enabled) {
            map_chr = 8;                          /* the nightmare forest */
            e->mirror = rnd() & 1;
        }
    } else if (e->key == 9) {
        e->mirror = e->x > 150;
    }
}

static void update_nightmare(Entity *e)
{
    const NightmareDef *d = &nm_def[e->key % 10];
    Entity *ab = player;
    int dx, dy;
    if (e->cycle % d->upd != 0) { pony_motion(e); return; }
    if (!ab) return;
    dx = (2 * e->x + e->w) - (2 * ab->x + ab->w);   /* centre deltas, x2 */
    dy = (2 * e->y + e->h) - (2 * ab->y + ab->h);
    if (!e->a) {
        if (d->prox > 0 && iabs(dx) < 2 * d->prox && iabs(dy) < 2 * d->prox) {
            /* this.Activate(SelectedKey): wakes only THIS one - every
             * BoneFiend shares key 8, so each rises as you come near it */
            e->a = 1; e->solid = 1; e->visible = 1;
        }
        pony_motion(e);
        return;
    }
    if (e->d > 0) {                               /* caught: the screen flashes */
        screen_flash(1, (e->d & 4) ? COLOR_LTRED : COLOR_DKRED);   /* gentle pulse */
        e->d++;
        if (e->d == 40) {
            screen_flash(0, 0);
            start_dlg(31);
        } else if (e->d == 41) {
            globals[0] = 0; globals[10] = 0; globals[11] = 0; globals[12] = 0;
            load_map_direct(e->key == 8 ? 17 : 20);
        }
    } else if (d->dangerous && e->b > 30 && iabs(dx) < 32 && iabs(dy) < 32) {
        ab->enabled = 0;                          /* caught */
        e->d = 1;
        /* MUSIC: stop; SFX: Caught (audio.Play(4)) */
        return;
    }
    if (e->b < 36) { e->b++; pony_motion(e); return; }   /* rising */
    if (e->c > 0) { e->c--; return; }
    if (d->pct && chance(d->pct))
        e->c = d->minD + (d->maxD ? (int)(rnd() % d->maxD) : 0);
    if (e->nwp > 0) {
        if (step_to(e, e->wp[0], e->wp[1], d->vel, d->vel)) {
            e->mvx = e->mvy = 0;
            if (e->state + 1 == d->dlgAt) { e->c = 0; start_dlg(d->dlg); }
            e->state++;
            e->wp += 2; e->nwp--;
        }
        pony_motion(e);
        return;
    }
    if (d->chase) {
        int mx = 0, my = 0;
        if (ab->x < e->x - 8) mx = -d->vel; else if (ab->x > e->x + 8) mx = d->vel;
        if (ab->y < e->y - 8) my = -d->vel; else if (ab->y > e->y + 8) my = d->vel;
        e->mvx = mx < 0 ? -1 : (mx > 0);
        e->mvy = my < 0 ? -1 : (my > 0);
        move_collide(e, mx, my);
    }
    pony_motion(e);
}

/* ================================================================== */
/* Sunny Town folk                                                     */
/* ================================================================== */
static const int wp_roneo[] = { 9,16, 19,16, 19,25, 23,25 };

static void start_roneo(Entity *e)
{
    if (globals[2] == 2) {                        /* already happy */
        e->key = 1;
        e->x = 23 * 8; e->y = 25 * 8;
    } else set_wp(e, wp_roneo, NWP(wp_roneo));
}

static void update_roneo(Entity *e)
{
    if (e->key == 1 && e->nwp > 0) {              /* off to see Starlet */
        if (step_to(e, e->wp[0], e->wp[1], 3, 2)) { e->wp += 2; e->nwp--; }
    } else {
        e->mvx = e->mvy = 0;
        e->dir = 1;
    }
    pony_motion(e);
}

/* ================================================================== */
/* Triggers (Trigger.as) - invisible 16x16 zones                        */
/* ================================================================== */
enum { T_RUBY, T_HIDDEN, T_YELL1, T_YELL2, T_HEART, T_DISCOVERY, T_BACKTOWN,
       T_REDWIN, T_ACT_GH, T_ACT_GLAD, T_ACT_3L, T_ACT_STAR, T_ACT_RONEO,
       T_ACT_MITTA, T_VILLAGE, T_BLACKOPEN, T_FINALENT, T_FINAL,
       T_MUS_EVER, T_MUS_LAB, T_MUS_SUN, T_MUS_DARK, T_MUS_NM, T_MUS_STOP };

static void start_trigger(Entity *e)
{
    Entity *f;
    switch (e->key) {
        case T_REDWIN:      /* g1 == 6: the windows glow red (palette 5) */
            break;
        case T_BLACKOPEN:
            if (globals[1] == 6 && (f = find_ent(E_FADE)) != 0) f->key = 2;
            break;
        case T_FINALENT:
            if (globals[1] < 7) { globals[1] = 7; room_tint(COLOR_BLACK, 0); }
            break;
        case T_MUS_EVER:  /* MUSIC: Everfree (loop) */  break;
        case T_MUS_LAB:   /* MUSIC: Labyrinth (loop) */ break;
        case T_MUS_SUN:   /* MUSIC: SunnyTown (loop) */ break;
        case T_MUS_DARK:  /* MUSIC: DarkTown (loop) */  break;
        case T_MUS_NM:    /* MUSIC: Nightmare (loop) if global 1 == 6 */ break;
        case T_MUS_STOP:  /* MUSIC: stop */ break;
        default: break;
    }
}

static void trigger_portal(Entity *e, int key)
{
    portal_jump(key);
    if (player) player->enabled = 0;
    (void)e;
}

/* TI port: the Starlet nightmare (map 24) waits behind the debris corridor
 * as an ambush, and the slow push leaves no time to react - wake it this many
 * ticks (25 = 1 s) after the trigger instead of at once. Owner's call: the
 * audience isn't gamers. */
#define STARLET_GRACE 25

static void update_trigger(Entity *e)
{
    Entity *ab = player, *r;
    if (e->ai > 0) {                              /* a delayed wake-up */
        if (--e->ai == 0) nightmare_activate(3);
        return;
    }
    if (!ab || !ent_overlap(ab, e)) return;
    switch (e->key) {
        case T_RUBY:
            if (globals[1] == 2) {
                applebloom_wait(50);
                globals[1] = 3;
                r = ent_spawn(E_RUBY, 140, 80, 0);
                if (r) script_start(r);
            }
            break;
        case T_HIDDEN:    if (globals[1] == 3) trigger_portal(e, 40); break;
        case T_YELL1:
            if (globals[1] < 4) { globals[1] = 4; start_dlg(8); }
            break;
        case T_YELL2:
            if (globals[1] == 4) { globals[1] = 5; start_dlg(9); }
            break;
        case T_HEART:
            if (globals[1] == 6) { e->y = -50; trigger_portal(e, 108); }
            break;
        case T_VILLAGE:
            if (globals[1] < 6) { e->y = -50; trigger_portal(e, 76); }
            break;
        case T_DISCOVERY:
            if (globals[1] < 6) { globals[1] = 6; start_dlg(24); }
            break;
        case T_BACKTOWN:  if (globals[1] == 6) trigger_portal(e, 100); break;
        case T_ACT_GH:    e->y = -50; nightmare_activate(0); break;
        case T_ACT_GLAD:  e->y = -50; nightmare_activate(1); break;
        case T_ACT_3L:                            /* NES camera pan: not paged */
            if (globals[10] < 90) { globals[10]++; applebloom_wait(110); }
            else { e->y = -50; nightmare_activate(2); }
            break;
        case T_ACT_STAR:  e->y = -50; e->ai = STARLET_GRACE; break;
        case T_ACT_RONEO:
            if (globals[11] == 0) {
                globals[11] = 1; applebloom_wait(180);
                ab->dir = 2; ab->mirror = 0;
            } else {
                e->y = -50;
                nightmare_activate(4); nightmare_activate(5); nightmare_activate(6);
            }
            break;
        case T_ACT_MITTA:
            if (globals[12] == 0) {
                globals[12] = 1; applebloom_wait(70);
                ab->dir = 1; ab->mirror = 0;
            } else { e->y = -50; nightmare_activate(7); }
            break;
        case T_FINAL:
            if (globals[1] < 8) { globals[1] = 9; start_final(ab); }
            break;
        default: break;
    }
}

/* ================================================================== */
/* Credits (map 29) / The end (map 30)                                 */
/* ================================================================== */
static void update_credits(Entity *e)
{
    int top;
    if (e->key == 0) {                            /* The end */
        if (e->cycle == 0) start_dlg(0);
        if (e->cycle > 60) load_map_direct(29);
        return;
    }
    if (e->cycle == 0) {
        /* MUSIC: Intro (once) */
        credits_begin();
        e->a = -1;
    }
    top = e->cycle / 16 + 6;          /* scroll y = cycle/2 px; 24 of 30 rows */
    if (top != e->a) { credits_scroll(top); e->a = top; }
    kscanfast(1);
    if (e->cycle > 650 || (KSCAN_KEY == 18 && e->cycle > 25))
        load_map_direct(0);                       /* back to the title */
}

/* ================================================================== */
/* dispatch                                                            */
/* ================================================================== */
void script_start(Entity *e)
{
    switch (e->type) {
        case E_APPLEBLOOM:
            switch (e->key) {
                case 1: e->ai = 90; break;        /* Meeting */
                case 3: e->ai = 65; break;        /* Sunny town */
                /* 4 Ruins: the NES pans the ruined town for 100 ticks;
                 * on the TI we start straight on Applebloom (owner's call) */
                case 5: e->ai = 1000; break;      /* Escape */
                default: break;                   /* 7 Intro = title_screen */
            }
            break;
        case E_TWILIGHT:  start_twilight(e); break;
        case E_ZECORA:    e->ai = 100; break;
        case E_RUBY:
            if (e->key == 0 || e->key == 1) e->ai = 100;
            else if (e->key == 3) { e->ai = 250; e->visible = 0; e->solid = 0; }
            break;
        case E_NIGHTMARE: start_nightmare(e); break;
        case E_RONEO:     start_roneo(e); break;
        case E_TRIGGER:   start_trigger(e); break;
        case E_ITEM:                              /* already collected? */
            if ((e->key == 0 && globals[2] > 0) || (e->key == 1 && globals[3] > 0) ||
                (e->key == 2 && globals[4] > 0))
                e->y = -50;
            break;
        default: break;
    }
}

void script_update(Entity *e)
{
    switch (e->type) {
        case E_APPLEBLOOM: update_applebloom(e); break;
        case E_TWILIGHT:   update_twilight(e); break;
        case E_ZECORA:     update_zecora(e); break;
        case E_RUBY:       update_ruby(e); break;
        case E_NIGHTMARE:  update_nightmare(e); break;
        case E_RONEO:      update_roneo(e); break;
        case E_GREYHOOF:
        case E_GLADSTONE:  random_turn(e); break;
        case E_STARLET:
            random_turn(e);
            if (globals[2] == 2) { e->dir = 0; e->mirror = 1; }  /* eyes on Roneo */
            break;
        case E_TRIGGER:    update_trigger(e); break;
        case E_CREDITS:    update_credits(e); break;
        case E_BOX:        update_box(e); break;
        default: break;
    }
}

void script_interact(Entity *o)
{
    Entity *ab = player;
    switch (o->type) {
        case E_GREYHOOF:  start_dlg(11); break;
        case E_GLADSTONE: start_dlg(16); break;
        case E_STARLET:   if (globals[2] < 2) start_dlg(14); break;
        case E_RONEO:
            if (o->nwp > 0 && globals[2] == 2) return;   /* walking to her */
            if (globals[2] == 0) start_dlg(12);
            else if (globals[2] == 1) { start_dlg(13); o->key = 1; globals[2] = 2; }
            else if (find_ent(E_STARLET)) start_dlg(15);
            break;
        case E_MITTA:     start_dlg(18); break;
        case E_THREELEAF:
            start_dlg(17);
            if (ab) o->mirror = ab->x < o->x;
            return;
        case E_TWILIGHT:  if (globals[1] > 1) start_dlg(7); break;
        case E_DOOR:      /* locked until the well key is found (global 4) */
            if (globals[4] == 0) start_dlg(20);
            else if (globals[1] < 6) { o->solid = 0; o->key = 1; /* SFX: Door */ }
            return;
        case E_ITEM:
            if (o->key == 0)      { start_dlg(22); globals[2] = 1; /* SFX: Pickup */ }
            else if (o->key == 1) { start_dlg(19); globals[3] = 1; /* SFX: Pickup */ }
            else if (globals[3] == 0) start_dlg(21);
            else { start_dlg(23); globals[4] = 1; /* SFX: Pickup */ }
            o->y = -50;
            return;
        default: break;
    }
    /* Pony.Interact: ponies turn to face whoever spoke to them */
    switch (o->type) {
        case E_GREYHOOF: case E_GLADSTONE: case E_STARLET: case E_RONEO:
        case E_TWILIGHT: case E_RUBY: case E_ZECORA: case E_NIGHTMARE:
            if (ab) face_toward(o, ab);
            break;
        default: break;
    }
}
