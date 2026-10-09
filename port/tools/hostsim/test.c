/* Host-sim scratch driver. Edit main() to probe whatever you're debugging:
 *   tools/hostsim/run.sh tools/hostsim/test.c
 * The stub vdp.h/kscan.h define the libti99 entry points as no-ops. Input:
 * set KSCAN_JOYX/Y directly (joystfast leaves them), fire via hs_fire, or
 * hs_fire_auto to click through dialogues. For the full story test see
 * walkthrough.c. */
#include <stdio.h>
#include "game.h"
#include "entity.h"

unsigned int gImage, gPattern, gSprite, gSpritePat, gColor;
unsigned char KSCAN_KEY, KSCAN_JOYX, KSCAN_JOYY;
int hs_color_pair, hs_reg7, hs_fire, hs_fire_auto;

int main(void)
{
    int f;
    game_init();
    globals[0] = 1;                    /* arrive in Sunny Town by entrance 1 */
    load_map_direct(18);
    ingame_update();                   /* performs the load */
    printf("map %d player=(%d,%d) w=%d h=%d\n", cur_map,
           player->x, player->y, player->w, player->h);
    for (f = 0; f < 6; f++) {
        KSCAN_JOYX = 0x04; KSCAN_JOYY = 0;           /* hold RIGHT */
        ingame_update();
        printf("RIGHT t%d: player=(%d,%d)\n", f, player->x, player->y);
    }
    return 0;
}
