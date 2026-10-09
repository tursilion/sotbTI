/* Generic entity system - port of blank.Entity / blank.Ingame / Pony.
 *
 * The AS3 engine keeps a Vector of polymorphic Entity objects, each with
 * Update/Draw/Interact. We use a flat array of structs dispatched by type
 * (entity.c = engine core, script.c = the per-class behaviour/story scripts).
 * Fields are int per the project rule (gcc9900 char handling is unreliable).
 * Entities never move inside entity[] (y-sort uses an index array), so
 * Entity pointers stay valid for the whole map.
 */
#ifndef ENTITY_H
#define ENTITY_H
#include "game.h"

#define E_ORB  42                /* not a map entity: spawned by the finale */

typedef struct {
    int type;                    /* E_* */
    int key;                     /* SelectedKey (scripts may change it) */
    int x, y;                    /* pixel pos, top-left, full-map coords */
    int w, h;                    /* collision box size */
    int enabled, visible, solid;
    int held;                    /* enabled state saved by freeze_all() */
    /* pony state (Pony.as) */
    int dir;                     /* 0 side, 1 down, 2 up */
    int mirror;                  /* facing left */
    int mvx, mvy;                /* MovingLeft/Right (-1/+1), MovingUp/Down */
    int walking, anim, animDelay;
    int bodyIndex, hairIndex, drawHair;
    int sprBank;                 /* CHR bank for this entity's sprites */
    int bodyCol, hairCol;        /* resolved TI colours */
    unsigned int cycle;          /* updates since spawn (Entity.cycle) */
    int state;                   /* generic: portal overlap edge, etc. */
    int ai;                      /* aiDelay / script timer */
    int a, b, c, d;              /* per-type script scratch (see script.c) */
    const int *wp;               /* waypoint list (x,y tile pairs) */
    int nwp;                     /* waypoints remaining */
} Entity;

#define MAX_ENTITIES 32          /* largest map (23) has 32; map 11 has 30 */
extern Entity entity[MAX_ENTITIES];
extern int entity_count;
extern Entity *player;           /* the Applebloom entity (0 if none) */

void game_init(void);            /* explicit runtime init of mutable globals */
void ents_clear(void);
Entity *ent_spawn(int type, int x, int y, int key);
Entity *find_ent(int type);      /* first entity of a type, or 0 */

void ingame_update(void);
void ingame_draw(void);

/* per-frame hardware-sprite allocator (entity.c) */
void sprites_begin(void);
void sprites_end(void);
void sprites_hide_all(void);      /* real 0xD0 terminator at slot 0 */

/* engine helpers shared with script.c */
unsigned int rnd(void);
int  box_hits(Entity *e, int x, int y);   /* map collision at a position */
int  ent_overlap(Entity *a, Entity *b);   /* Rectangle.intersects */
void pony_motion(Entity *e);              /* Pony.Update: facing + walk anim */
void face_toward(Entity *e, Entity *t);   /* Pony.Interact facing */
int  step_to(Entity *e, int tx, int ty, int v, int tol);  /* 1 = arrived */
void move_collide(Entity *e, int dx, int dy);  /* move, stopped by walls */
void freeze_all(Entity *except);          /* disable everyone (cutscenes) */
void thaw_all(void);
void portal_jump(int key);                /* Portal exit key: fade + load */
void load_map_direct(int m);              /* LoadMap without a leave fade */
void screen_flash(int on, int color);     /* blank display in a colour */

/* script.c */
void script_start(Entity *e);             /* the AS3 "cycle == 0" set-up */
void script_update(Entity *e);
void script_interact(Entity *o);
void applebloom_wait(int ticks);

#endif
