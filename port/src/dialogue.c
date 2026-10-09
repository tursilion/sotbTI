/* Dialogue system - port of blank.entities.Dialogue.
 *
 * TI adaptation: the AS3 version drew a 32x6 tile box over the game with 16-tile
 * sprite portraits. On the TMS9918 that blows the sprite budget and Graphics-I
 * per-group colour would bleed, so we show a clean full-screen text page using
 * the console font: load the font, reveal the conversation letter-by-letter
 * exactly as the original (same markers), then restore the game screen.
 *
 * Message markers (identical to the AS3):
 *   '<' first speaker   '>' second speaker   '0'-'6' face expr (consumed)
 *   '/' newline         '-' pause (1 tick, prints nothing)   '*' scare
 * Empty message -> "PRESS FIRE"; fire advances; past the last message -> end.
 */
#include "vdp.h"
#include "kscan.h"
#include "game.h"
#include "entity.h"

#define TXT_X   2
#define TXT_Y   4          /* first text row */
#define NAME_Y  2
#define MAXROW  16         /* text rows available */
#define REVEAL_DELAY 0     /* extra ticks per character: the original shows
                              one per 25 Hz tick (MessageDelay 1) */

static unsigned char active;
static const DialogueDef *dd;
static unsigned char mi;           /* current message index */
static const char *mp;             /* cursor into current message */
static unsigned char row, col;
static unsigned char first_sel;
static unsigned char waiting;      /* message fully revealed */
static unsigned char delay;
static unsigned char last_fire;
static int portrait;               /* the fireplace skeleton is on screen */
int dialogue_portrait(void) { return portrait; }   /* tests */

/* Dialogue 24: the original's portrait for the unnamed first speaker is the
 * skeleton in the fireplace (face 6) - the one picture the story needs.
 * Shown as 8x8 characters (codes 128-191, colour groups 16-23), bones red
 * on black as in the scare palette. */
#define PORT_ROW 10
#define PORT_COL 12
static void draw_portrait(void)
{
    int r, c;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            vdpscreenchar((PORT_ROW + r) * 32 + PORT_COL + c, (unsigned char)(128 + r * 8 + c));
}

static int dlg_bank;               /* this conversation's data bank */

static void show_portrait(void)
{
    int g;
    CART_BANK(skel_phys);              /* the picture lives in its own bank */
    vdpmemcpy(gPattern + 128 * 8, skel_chr, 512);
    CART_BANK(dlg_bank);               /* back to the conversation's text */
    for (g = 16; g < 24; g++)
        vdpchar(gColor + g, (COLOR_MEDRED << 4) | COLOR_BLACK);
    portrait = 1;
    draw_portrait();
}

int dialogue_active(void) { return active; }

static void put(unsigned char r, unsigned char c, unsigned char ch)
{
    vdpscreenchar(r * 32 + c, ch);
}

static void put_str(unsigned char r, unsigned char c, const char *s)
{
    while (*s) put(r, c++, (unsigned char)*s++);
}

static void fill_rows(unsigned char r0, unsigned char r1)
{
    vdpmemset(gImage + r0 * 32, ' ', (r1 - r0 + 1) * 32);
}

static void show_name(void)
{
    const char *n = first_sel ? dd->first : dd->second;
    fill_rows(NAME_Y, NAME_Y);
    if (n && *n) put_str(NAME_Y, TXT_X, n);
}

static void begin_message(void)
{
    mp = dd->msgs[mi];
    row = 0; col = 0;
    waiting = 0; delay = 0;
}

void dialogue_start(int id)
{
    if (id < 0 || id >= DIALOGUE_COUNT) return;
    dlg_bank = dlg_phys[id];
    CART_BANK(dlg_bank);               /* page in this dialogue's bank */
    if (dialogues[id].nmsgs == 0) return;
    dd = &dialogues[id];
    mi = 0;
    first_sel = 1;
    active = 1;
    portrait = 0;

    video_blank();                     /* hide the switch to the text page */
    sprites_hide_all();                /* real terminator: blanks every sprite */
    charsetlc();                       /* load TI font (true lowercase) */
    { unsigned char g; for (g = 0; g < 32; g++)
          vdpchar(gColor + g, (COLOR_WHITE << 4) | COLOR_DKBLUE); }
    VDP_SET_REGISTER(VDP_REG_COL, COLOR_DKBLUE);
    vdpmemset(gImage, ' ', 32 * 24);   /* clear screen */

    begin_message();
    show_name();
    video_unblank();
    last_fire = 1;                     /* ignore the fire that opened the box */
    /* SFX: Dialogue (audio.Play(8)) */
}

static void dialogue_end(void)
{
    active = 0;
    fire_locked = 1;                   /* ignore fire until the player moves,
                                          so closing the box can't reopen it */
    video_blank();                     /* hide the switch back to the game */
    video_load_chr(map_chr);           /* restore scene CHR */
    set_room_colors();
    draw_room();
    ingame_draw();                     /* sprites back before it's shown */
    video_unblank();
}

/* consume one character of the current message */
static void step_char(void)
{
    char ch = *mp;
    if (ch == 0) { waiting = 1; return; }
    mp++;
    if (ch >= '0' && ch <= '6') {
        /* face expression - no portrait in this version */
    } else if (ch == '<') {
        first_sel = 1; row = 0; col = 0; fill_rows(TXT_Y, 23); show_name();
        if (portrait) draw_portrait();
    } else if (ch == '>') {
        first_sel = 0; row = 0; col = 0; fill_rows(TXT_Y, 23); show_name();
        if (portrait) draw_portrait();
    } else if (ch == '/') {
        row++; col = 0;
    } else if (ch == '*') {
        /* scare (dialogue 24): the original turns the room's palette
         * sinister and starts the Trapped music. On the TI the room's
         * light detail goes red once the conversation closes. */
        room_fg_tint = COLOR_DKRED;
        show_portrait();                 /* ...and there she is */
        /* MUSIC: Trapped (loop) */
        row++; col = 0;
    } else if (ch == '-') {
        /* pause: consume a tick, print nothing */
    } else {
        if (row < MAXROW && TXT_X + col < 31) {
            put(TXT_Y + row, TXT_X + col, (unsigned char)ch);
            col++;
        }
    }
    if (*mp == 0) waiting = 1;
}

void dialogue_update(void)
{
    unsigned char fire, edge;
    kscanfast(1);
    fire = (KSCAN_KEY == 18);
    edge = fire && !last_fire;
    last_fire = fire;

    if (waiting) {
        put_str(23, 22, "PRESS FIRE");
        if (edge) {
            /* SFX: Next (audio.Play(5)) */
            mi++;
            if (mi >= dd->nmsgs) { dialogue_end(); return; }
            fill_rows(TXT_Y, 23);
            if (portrait) draw_portrait();
            begin_message();
        }
        return;
    }

    /* a fresh press (not a hold) fast-forwards the reveal to the end */
    if (edge) { while (!waiting) step_char(); return; }

    if (delay) { delay--; return; }
    delay = REVEAL_DELAY;
    step_char();
}
