/* Walkthrough: plays Story of the Blanks start to finish through the real
 * game logic (entity/script/maps/dialogue), checking the story flags and map
 * changes at each step. Movement between points is done by teleporting the
 * player (the route-finding is the human's job); everything else - portals,
 * triggers, cutscenes, dialogues, Nightmares - runs for real.
 *   host (32-bit):      tools/hostsim/run.sh tools/hostsim/walkthrough.c
 *   TMS9900 (16-bit):   make -f Makefile.cart walktest   (runs the real cart
 *                       code in tools/cartsim; results come back via wt_res[])
 */
#include "vdp.h"
#include "game.h"
#include "entity.h"

int hs_fire, hs_fire_auto;               /* input mailbox for the stubs/cartsim */
#define NOINL __attribute__((noinline))  /* keep the test small on the cart */
static int fails, ticks, dialogues_seen;

#ifdef TI99
/* no stdio on the cart: results go to RAM for tools/cartsim to read */
volatile int wt_res[96];                 /* (map << 2) | 1 ok / 2 fail, in order */
volatile int wt_n, wt_done;              /* volatile: only cartsim reads them */
#define LOG0(f)        ((void)0)
#define LOG2(f, a, b)  ((void)0)
#else
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
unsigned int gImage, gPattern, gSprite, gSpritePat, gColor;
unsigned char KSCAN_KEY, KSCAN_JOYX, KSCAN_JOYY;
int hs_color_pair, hs_reg7;
#define LOG0(f)        printf(f)
#define LOG2(f, a, b)  printf(f, a, b)

static void watchdog(int sig)                 /* a stuck loop: say where */
{
    (void)sig;
    fprintf(stderr, "\nSTUCK: map %d tick %d pending %d dialogue %d g1 %d player %s (%d,%d)\n",
            cur_map, ticks, pending_map, dialogue_active(), globals[1],
            player ? (player->enabled ? "on" : "off") : "none",
            player ? player->x : 0, player ? player->y : 0);
    exit(2);
}
#endif

NOINL static void tick(void)
{
    int was = dialogue_active();
    hs_fire_auto = was || pending_map == 0;  /* click through dialogues/title */
    ingame_update();
    if (!dialogue_active()) camera_follow();
    ingame_draw();
    if (!was && dialogue_active()) dialogues_seen++;
    ticks++;
}
NOINL static void run(int n) { while (n-- > 0) tick(); }
#ifdef TI99
/* the check names would only waste the cart's fixed space */
#define check(what, ok) check_ti(ok)
NOINL static void check_ti(int ok)
{
    if (wt_n < 96) wt_res[wt_n++] = (ok ? 1 : 2) | (cur_map << 2);
    if (!ok) fails++;
}
#else
static void check(const char *what, int ok)
{
    printf("%s %s  (map %d, tick %d, g1=%d)\n", ok ? "ok  " : "FAIL", what,
           cur_map, ticks, globals[1]);
    if (!ok) fails++;
}
#endif
NOINL static int until_map(int m, int max)
{
    while (max-- > 0) { tick(); if (cur_map == m && pending_map < 0) return 1; }
    return 0;
}
NOINL static void quiet(void) { int n = 2000; while (dialogue_active() && n--) tick(); }
NOINL static void tp(int tx, int ty) { player->x = tx * 8; player->y = ty * 8; }
/* walk out through an exit portal at tile (tx,ty) into map m */
NOINL static void exit_to(int tx, int ty, int m)
{
    quiet(); run(15); quiet();
    tp(tx, ty);
    LOG2("     portal (%d,%d)\n", tx, ty);
    check("portal -> next map", until_map(m, 80));
}
/* stand next to something and press fire once */
NOINL static void talk(int tx, int ty)
{
    int n = dialogues_seen;
    quiet();
    tp(tx, ty);
    tick();                               /* release: fire is edge-triggered */
    hs_fire = 1; tick(); hs_fire = 0; tick();
    if (dialogues_seen == n) LOG2("     (no reply at %d,%d)\n", tx, ty);
    quiet();
}
/* keep the player beside Twilight until she leads us into map m */
NOINL static void follow_twilight(int m, int max)
{
    while (max-- > 0) {
        Entity *t = find_ent(E_TWILIGHT);
        if (!dialogue_active() && t && player && player->enabled) {
            player->x = t->x; player->y = t->y + 20;
        }
        tick();
        if (cur_map == m && pending_map < 0) break;
    }
    LOG2("     following Twilight to map %d%s\n", m, "");
    check("Twilight leads on", cur_map == m);
}

int main(void)
{
    Entity *e;
    int n;
#ifndef TI99
    signal(SIGALRM, watchdog); alarm(30);
    setvbuf(stdout, 0, _IONBF, 0);
#endif
    game_init();
    video_init();
    hs_fire_auto = 1;
    enter_map(0);                         /* title, then the forest edge */
    check("start: map 0, player at the entrance",
          cur_map == 0 && player && player->x == 16 && player->y == 384);

    exit_to(30, 57, 1);
    exit_to(30, 1, 2);
    /* map 2: Meeting - Applebloom walks in, Twilight comes up, dialogue 1 */
    n = dialogues_seen; run(130);
    check("Twilight meeting dialogue", dialogues_seen > n);
    quiet();
    follow_twilight(2, 120);              /* she heads off the top */
    exit_to(15, 1, 3);
    follow_twilight(3, 40);
    exit_to(14, 6, 4);
    follow_twilight(4, 20);
    exit_to(47, 0, 5);
    exit_to(30, 1, 6);
    /* map 6 first visit: she leads through the passage to map 7, then 8 */
    follow_twilight(7, 600);
    follow_twilight(8, 600);
    /* map 8: Zecora, two dialogues, back to map 7 with chapter 1 */
    check("Zecora sends us back to map 7", until_map(7, 400));
    check("chapter 1 (met Zecora)", globals[1] == 1);
    follow_twilight(6, 600);
    /* map 6 second visit: Blocked path cutscene */
    n = dialogues_seen;
    follow_twilight(6, 300);
#ifndef TI99    /* camera-framing checks: host only (keeps the cart test small) */
    {   /* the log scene must be on screen when Twilight starts talking */
        int seen = 0, ok = 0, i;
        while (globals[1] < 2 && ticks < 1000000) {
            tick();
            if (dialogue_active() && !seen) {
                Entity *t = find_ent(E_TWILIGHT), *tr = 0;
                seen = 1;
                for (i = 0; i < entity_count; i++)
                    if (entity[i].type == E_TREE && entity[i].a == 1) tr = &entity[i];
                ok = t && tr &&
                     t->x / 8 >= cam_tx && t->x / 8 < cam_tx + 32 &&
                     t->y / 8 >= cam_ty && t->y / 8 < cam_ty + 24 &&
                     tr->x / 8 >= cam_tx && tr->x / 8 + 3 < cam_tx + 32 &&
                     tr->y / 8 >= cam_ty && tr->y / 8 + 1 < cam_ty + 24;
            }
        }
        check("log scene: Twilight and the tree on screen", ok);
    }
#else
    while (globals[1] < 2 && ticks < 1000000) tick();
#endif
    check("blocked path: three dialogues, chapter 2",
          globals[1] == 2 && dialogues_seen >= n + 3);
    tp(16, 30); run(2); tp(16, 21); run(3);   /* walk up into her trigger */
    check("Ruby appears (chapter 3)", globals[1] == 3 && find_ent(E_RUBY));
#ifndef TI99    /* camera-framing checks: host only (keeps the cart test small) */
    {   /* her walk must be on screen, Applebloom too */
        int i, ok = 1;
        for (i = 0; i < 50; i++) {
            Entity *r = find_ent(E_RUBY);
            tick();
            if (r->visible && (r->y / 8 < cam_ty || r->y / 8 >= cam_ty + 24 ||
                               player->y / 8 < cam_ty || player->y / 8 >= cam_ty + 24))
                ok = 0;
        }
        check("Ruby's scene on screen with Applebloom", ok);
    }
#else
    run(50);
#endif
    run(60);
    check("Ruby walked off and vanished", !find_ent(E_RUBY)->visible);
    quiet(); run(5);
    tp(4, 9);
    check("hidden path -> map 9", until_map(9, 80));
    tp(20, 15); run(3); quiet();
    check("yell 1 (chapter 4)", globals[1] == 4);
    exit_to(12, 28, 10);
    exit_to(0, 12, 11);
    tp(24, 15); run(3); quiet();
    check("yell 2 (chapter 5)", globals[1] == 5);
    e = find_ent(E_NIGHTMARE);
    check("BoneFiends sleep before chapter 6", e && !e->enabled);
    exit_to(0, 19, 13);
    exit_to(10, 0, 14);
    exit_to(0, 8, 15);
    exit_to(0, 13, 16);
    exit_to(14, 0, 17);
    tp(13, 6);
    check("enter the village -> Sunny Town", until_map(18, 80));
    /* map 18: she walks up and GreyHoof greets her */
    n = dialogues_seen; run(80); quiet();
    check("Sunny Town greeting", globals[5] == 1 && dialogues_seen > n);
    talk(9, 10);                          /* Roneo, sad */
    check("Roneo is sad (g2 0)", globals[2] == 0);
    exit_to(0, 10, 19);
    talk(5, 51);                          /* the gem */
    check("picked up the gem (g2 1)", globals[2] == 1);
    exit_to(60, 44, 18);
    e = find_ent(E_RONEO);
    talk(e->x / 8, e->y / 8 + 2);
    check("Roneo cheers up (g2 2)", globals[2] == 2);
    run(120);
    e = find_ent(E_RONEO);
    check("Roneo walks over to Starlet", e->x == 23 * 8 && e->y == 25 * 8);
    exit_to(9, 6, 21);
    talk(29, 20);                         /* the crank */
    check("picked up the crank (g3 1)", globals[3] == 1);
    exit_to(14, 27, 18);
    exit_to(0, 10, 19);
    exit_to(60, 13, 20);
    talk(54, 6);                          /* the well */
    check("found the key in the well (g4 1)", globals[4] == 1);
    talk(42, 7);                          /* the door (unlocks silently) */
    e = find_ent(E_DOOR);
    check("door unlocked", e->key == 1 && !e->solid);
    exit_to(42, 4, 22);
    {   /* the fireplace: the skeleton portrait appears during dialogue 24 */
        int i, shown = 0;
        tp(14, 11);
        for (i = 0; i < 400 && (i < 3 || dialogue_active()); i++) {
            tick();
            if (dialogue_portrait()) shown = 1;
        }
        check("the skeleton is shown in the fireplace dialogue", shown);
    }
    check("discovery (chapter 6)", globals[1] == 6);
    check("scare turned the room red", room_fg_tint == COLOR_DKRED);
    exit_to(14, 27, 20);
    tp(3, 51);
    check("back to town -> the ruins (map 24)", until_map(24, 80));
    run(2);
    check("ruins: she starts in control, on screen",
          player->ai == 0 && player->x / 8 >= cam_tx && player->x / 8 < cam_tx + 32);
    /* map 24: wake the Nightmares one by one, keeping out of reach */
    tp(35, 12); run(2);
    e = find_ent(E_NIGHTMARE);
    for (n = 0; n < entity_count; n++)
        if (entity[n].type == E_NIGHTMARE && entity[n].key == 0) e = &entity[n];
    check("GreyHoof nightmare activated", e->a == 1 && e->visible);
    tp(60, 2); n = dialogues_seen; run(300); quiet();
    check("GreyHoof nightmare speaks at its waypoint", dialogues_seen > n);
    {   /* Gladstone's nightmare: nudges the debris, then leaves its route's
         * end (which sits half inside a wall) to come after her */
        Entity *g = 0, *bx = find_ent(E_BOX);
        for (n = 0; n < entity_count; n++)
            if (entity[n].type == E_NIGHTMARE && entity[n].key == 1) g = &entity[n];
        tp(13, 23); run(2); quiet();
        n = 0;
        while (n++ < 700) {
            if (!dialogue_active()) tp(13, 21);   /* behind a wall: safe */
            tick();
        }
        check("Gladstone's nightmare nudges the debris and moves on",
              g->nwp == 0 && bx->y > 256 && g->x != 26 * 8);
    }
#ifndef TI99    /* camera-framing checks: host only (keeps the cart test small) */
    {   /* ThreeLeaf nightmare rises at the left edge, partly in view */
        Entity *t3 = 0;
        int i, seen = 0;
        for (i = 0; i < entity_count; i++)
            if (entity[i].type == E_NIGHTMARE && entity[i].key == 2) t3 = &entity[i];
        quiet(); tp(18, 15);
        for (i = 0; i < 200 && !seen; i++) {
            tick();
            if (t3->a && t3->x - cam_tx * 8 > -16 && t3->x - cam_tx * 8 < 256) seen = 1;
        }
        check("ThreeLeaf nightmare rises in view at the left edge", seen);
        run(150); quiet(); run(5);
    }
#else
    quiet(); tp(18, 15); run(200); run(150); quiet(); run(5);
#endif
#ifndef TI99
    {   /* caught in the ruins: map 20 sends her straight back to the ruins
         * and must never be shown in between */
        extern int shown_maps[16], shown_n;
        Entity *g = 0;
        int i, saw20 = 0, sx = player->x, sy = player->y;
        for (i = 0; i < entity_count; i++)
            if (entity[i].type == E_NIGHTMARE && entity[i].a && entity[i].visible) g = &entity[i];
        if (g) {
            quiet();
            shown_n = 0;
            player->x = g->x; player->y = g->y;
            for (i = 0; i < 400 && !(cur_map == 24 && shown_n > 0 && player->enabled); i++) tick();
            for (i = 0; i < shown_n; i++) if (shown_maps[i] == 20) saw20 = 1;
            check("caught in the ruins: back to the ruins, map 20 never shown",
                  cur_map == 24 && !saw20);
            player->x = sx; player->y = sy;
        }
    }
#endif
    {   /* Starlet's nightmare wakes a little after its trigger (TI head start) */
        Entity *st = 0;
        int i, early, later;
        for (i = 0; i < entity_count; i++)
            if (entity[i].type == E_NIGHTMARE && entity[i].key == 3) st = &entity[i];
        quiet(); tp(25, 38); run(2); early = st->a;
        tp(60, 2); run(60); later = st->a;
        check("Starlet's nightmare wakes after a head start", !early && later);
    }
    exit_to(62, 50, 23);
    exit_to(13, 58, 17);
    e = find_ent(E_FADE);
    check("black opening: the fade now leaves to black", e && e->key == 2);
    exit_to(8, 28, 16);
    exit_to(30, 11, 15);
    exit_to(30, 13, 14);
    exit_to(21, 28, 13);
    exit_to(30, 18, 11);
    e = find_ent(E_NIGHTMARE);
    check("BoneFiends awake in chapter 6", e && e->enabled && map_chr == 8);
    {   /* they rise one at a time as she comes near, not all at once */
        int i, woke = 0, asleep = 0;
        Entity *near = 0;
        for (i = 0; i < entity_count; i++)
            if (entity[i].type == E_NIGHTMARE) { near = &entity[i]; break; }
        tp(near->x / 8 + 5, near->y / 8); run(2);
        for (i = 0; i < entity_count; i++)
            if (entity[i].type == E_NIGHTMARE) { if (entity[i].a) woke++; else asleep++; }
        check("BoneFiends wake one at a time by proximity", woke == 1 && asleep > 0);
        tp(30, 18 - 2);                   /* back to the entrance side */
    }
    {   /* nightmare labyrinth: black walls, red floor (not moved off black) */
        int g, black = 0, red = 0;
        for (g = 0; g < 32; g++) {
            if ((room_colors[g] & 15) == COLOR_BLACK || (room_colors[g] >> 4) == COLOR_BLACK) black = 1;
            if ((room_colors[g] & 15) == COLOR_DKRED || (room_colors[g] & 15) == 8) red = 1;
        }
        check("nightmare labyrinth: black walls and a red floor", black && red);
    }
    tp(30, 18);
    check("heart of the forest -> map 26", until_map(26, 80));
    exit_to(12, 0, 25);
    exit_to(13, 4, 27);
    check("final entrance (chapter 7)", globals[1] == 7);
    tp(15, 33); run(2);
    check("the finale starts (chapter 9)", globals[1] == 9 && player->key == 6);
#ifndef TI99    /* camera-framing checks: host only (keeps the cart test small) */
    {   /* the finale: Ruby and all four nightmares in view as they appear */
        int i, ok = 1, seen = 0;
        n = dialogues_seen;
        for (i = 0; i < 300; i++) {          /* until the nightmares are up */
            tick();
            if (cur_map != 27) break;
            for (seen = 0, ok = 1, i = i; seen < entity_count; seen++) {
                Entity *q = &entity[seen];
                if ((q->type == E_NIGHTMARE || q->type == E_RUBY) && q->visible &&
                    (q->y / 8 < cam_ty || q->y / 8 + 2 > cam_ty + 24)) ok = 0;
            }
            if (!ok) break;
        }
        check("finale: Ruby and the nightmares stay in view", ok);
    }
#else
    n = dialogues_seen;
#endif
    check("finale plays out into the escape (map 28)", until_map(28, 2000));
    check("finale had its three dialogues", dialogues_seen >= n + 3);
    n = dialogues_seen;
    check("escape -> the end (map 30)", until_map(30, 1500));
    check("Ruby's dark dialogue on the way", dialogues_seen > n);
    check("the end -> credits (map 29)", until_map(29, 400));
    check("credits -> back to the title (map 0)", until_map(0, 1000));
    check("new game: chapter reset", globals[1] == 0);

    /* getting caught: BoneFiends in chapter 6 */
    globals[1] = 6; globals[0] = 3;
    load_map_direct(11); run(2);
    e = 0;
    for (n = 0; n < entity_count; n++)
        if (entity[n].type == E_NIGHTMARE) { e = &entity[n]; break; }
    tp(e->x / 8 + 4, e->y / 8);
    n = dialogues_seen;
    check("caught by a BoneFiend -> dialogue 31, back to map 17",
          until_map(17, 600) && dialogues_seen > n);

#ifdef TI99
    wt_done = 1;
    for (;;) vdpwaitvint();              /* cartsim stops on wt_done */
#else
    printf("\n%s: %d failure(s), %d ticks, %d dialogues\n",
           fails ? "FAILED" : "PASSED", fails, ticks, dialogues_seen);
    return fails != 0;
#endif
}
