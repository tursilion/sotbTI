#ifndef KSCAN_H
#define KSCAN_H
/* host-sim stub. Tests drive input directly: KSCAN_JOYX/Y are left alone by
 * joystfast(); fire comes from hs_fire, or alternates press/release on every
 * scan while hs_fire_auto is set (gets through the title and dialogues). */
extern unsigned char KSCAN_KEY,KSCAN_JOYX,KSCAN_JOYY;
extern int hs_fire, hs_fire_auto;
static inline void kscanfast(unsigned char m){
    static int tog; (void)m;
    if (hs_fire_auto) KSCAN_KEY = (tog ^= 1) ? 18 : 0xff;
    else KSCAN_KEY = hs_fire ? 18 : 0xff;
}
static inline void joystfast(unsigned char u){(void)u;}  /* leave axes for test */
#endif
