/* Story of the Blanks - TI-99/4A port - entry point.
 *
 * The original runs its game logic at 25 updates per second
 * (Game.GameUpdatesPerSecond) and every script is timed in those ticks, so
 * we run one logic tick per 25/60ths of a vertical blank.
 */
#include "vdp.h"
#include "game.h"
#include "entity.h"

#define TICK_HZ  25
#define VINT_HZ  60

void main(void)
{
    int acc = 0;
    game_init();                 /* set mutable globals (don't trust .data init) */
    video_init();
    enter_map(0);                /* title screen, then the forest edge */

    for (;;) {
        vdpwaitvint();
        acc += TICK_HZ;
        if (acc < VINT_HZ) continue;
        acc -= VINT_HZ;
        ingame_update();
        if (!dialogue_active()) camera_follow();
        ingame_draw();
    }
}
