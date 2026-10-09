/* host-sim stub of libti99 vdp.h - just enough for the game logic */
#ifndef VDP_H
#define VDP_H
extern unsigned int gImage,gPattern,gSprite,gSpritePat,gColor;
static inline void vdpmemcpy(int a,const unsigned char*s,int n){(void)a;(void)s;(void)n;}
static inline void vdpmemset(int a,unsigned char c,int n){(void)a;(void)c;(void)n;}
extern int hs_color_pair, hs_reg7;   /* last colour-table byte / border, for tests */
static inline void vdpchar(int a,unsigned char c){(void)a;hs_color_pair=c;}
static inline void vdpscreenchar(int o,unsigned char c){(void)o;(void)c;}
static inline void sprite(unsigned char n,unsigned char ch,unsigned char co,unsigned char r,unsigned char c){(void)n;(void)ch;(void)co;(void)r;(void)c;}
static inline void delsprite(unsigned char n){(void)n;}
static inline void set_graphics(unsigned char m){(void)m;}
static inline void charset(void){}
static inline void charsetlc(void){}
static inline void writestring(unsigned char r,unsigned char c,char*s){(void)r;(void)c;(void)s;}
static inline unsigned char vdpwaitvint(void){return 0;}
#define VDP_SET_REGISTER(r,v) do{if((r)==7)hs_reg7=(v);}while(0)
#define VDP_SET_ADDRESS_WRITE(x) do{(void)(x);}while(0)
#define VDPWD(x) do{(void)(x);}while(0)
#define VDP_REG_MODE1 1
#define VDP_REG_SDT 6
#define VDP_REG_COL 7
#define VDP_MODE1_16K 0x80
#define VDP_MODE1_UNBLANK 0x40
#define VDP_MODE1_INT 0x20
#define VDP_MODE1_SPRMODE16x16 0x02
#define VDP_SPR_16x16 0x02
#define COLOR_WHITE 15
#define COLOR_BLACK 1
#define COLOR_DKRED 6
#define COLOR_MEDRED 8
#define COLOR_LTRED 9
#define COLOR_DKGREEN 12
#define COLOR_DKBLUE 4
#endif
