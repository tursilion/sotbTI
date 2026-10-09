/* cartsim - run the real banked sotb.bin on the harness TMS9900 core.
 *
 * Minimal TI-99/4A: 64K flat RAM, banked cart ROM at >6000 (write >6000+2n
 * selects bank n), VDP ports with a 16K VRAM, no console ROM/GROM. libti99
 * routines that need the console (vdpwaitvint, kscanfast, joystfast, charsetlc)
 * are intercepted by PC (addresses looked up from cart.map) and emulated.
 *
 * usage: cartsim sotb.bin <hook addrs from cart.map...>  (see run.sh)
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "cpu.h"

static uint8_t ram[65536];
static std::vector<uint8_t> rom;
static int bank;
static uint8_t vram[16384];
static uint16_t vaddr; static int vlatch; static uint8_t vlow;
static uint8_t vreg[8];
static uint16_t watch_addr;             /* report CPU writes here (0 = off) */
static uint16_t cur_pc;

class Sim : public TMS9900 {
public:
    uint16_t R(int r) { return rd16(getWP() + r * 2); }
    uint16_t rd16(uint16_t a) { return (rdb(a & ~1) << 8) | rdb((a & ~1) + 1); }
    uint8_t rdb(uint16_t a) {
        if (a >= 0x6000 && a < 0x8000) {
            size_t o = (size_t)bank * 0x2000 + (a - 0x6000);
            return o < rom.size() ? rom[o] : 0xff;
        }
        if (a >= 0x8000 && a < 0xa000) {
            if ((a & 0xfc00) == 0x8800) {          /* VDP read */
                if (a & 2) { vlatch = 0; return 0x80; }
                vlatch = 0; return vram[vaddr++ & 0x3fff];
            }
            if (a >= 0x8300 && a < 0x8400) return ram[a];
            return 0;
        }
        return ram[a];
    }
    void wrb(uint16_t a, uint8_t d) {
        if (a >= 0x6000 && a < 0x8000) { bank = ((a - 0x6000) >> 1) & 0x7f; return; }
        if (a >= 0x8000 && a < 0xa000) {
            if ((a & 0xfc00) == 0x8c00) {          /* VDP write */
                if (a & 2) {
                    if (!vlatch) { vlow = d; vlatch = 1; }
                    else {
                        vlatch = 0;
                        if (d & 0x80) vreg[d & 7] = vlow;
                        else vaddr = ((d & 0x3f) << 8) | vlow;
                    }
                } else { vlatch = 0; vram[vaddr++ & 0x3fff] = d; }
                return;
            }
            if (a >= 0x8300 && a < 0x8400) ram[a] = d;
            return;
        }
        if (watch_addr && (a & ~1) == watch_addr && d)
            printf("write %02x to watched %04x at pc %04x\n", d, a, cur_pc);
        ram[a] = d;
    }
    void ret() { branch(R(11)); }
private:
    uint16_t _memReadW(uint16_t a) { return rd16(a); }
    uint8_t _memReadB(uint16_t a) { return rdb(a); }
    void _memWriteW(uint16_t a, uint16_t d) { a &= ~1; wrb(a, d >> 8); wrb(a + 1, d & 0xff); }
    void _memWriteB(uint16_t a, uint8_t d) { wrb(a, d); }
    int _interruptLevel(int) { return -1; }
};

/* --walk: run the WALKTEST cart (tools/hostsim/walkthrough.c built for the
 * TMS9900). Fire comes from the cart's hs_fire / hs_fire_auto mailbox, the
 * joystick is centred, and the run ends when wt_done is set; wt_res[] holds
 * one entry per check ((map << 2) | 1 ok / 2 fail) in the same order as the host run. */
static int walk(int argc, char **argv)
{
    if (argc < 13) { fprintf(stderr, "usage: cartsim --walk rom start wait kscan joy cset hs_fire hs_auto wt_done wt_res wt_n\n"); return 1; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END); rom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(rom.data(), 1, rom.size(), f) != rom.size()) return 1;
    fclose(f);
    uint16_t a[11];
    for (int i = 0; i < 11; i++) a[i] = strtol(argv[3 + i], 0, 16);
    uint16_t a_start = a[0], a_wait = a[1], a_kscan = a[2], a_joy = a[3], a_cset = a[4],
             a_fire = a[5], a_auto = a[6], a_done = a[7], a_res = a[8], a_n = a[9];
    Sim cpu;
    bank = 0;
    cpu.branch(a_start);
    int tog = 0;
    long steps = 0;
    uint16_t minsp = 0xffff, minsp_pc = 0;
    watch_addr = 0;                     /* set to an address to trace writes */
    for (;;) {
        uint16_t pc = cpu.getPC();
        cur_pc = pc;
        if (pc == a_wait) {
            if (cpu.rd16(a_done)) break;
            cpu.ret(); continue;
        }
        if (pc == a_kscan) {
            if (cpu.rd16(a_auto)) ram[0x8375] = (tog ^= 1) ? 18 : 0xff;
            else ram[0x8375] = cpu.rd16(a_fire) ? 18 : 0xff;
            cpu.ret(); continue;
        }
        if (pc == a_joy) { ram[0x8376] = ram[0x8377] = 0; cpu.ret(); continue; }
        if (pc == a_cset) { cpu.ret(); continue; }
        uint16_t op = cpu.fetch();
        cpu.execute(op);
        if (cpu.getWP() == 0x8300) {             /* gcc's stack pointer is R10 */
            uint16_t sp = cpu.R(10);
            if (sp >= 0x2000 && sp < minsp) { minsp = sp; minsp_pc = pc; }
        }
        if ((++steps & 0xffff) == 0 && cpu.rd16(a_done)) break;
        if (steps > 4000000000L) { printf("step limit, pc=%04x\n", pc); return 1; }
    }
    int n = (int16_t)cpu.rd16(a_n), fails = 0;
    for (int i = 0; i < n; i++) {
        int r = cpu.rd16(a_res + 2 * i);
        printf("%s map %d\n", (r & 3) == 1 ? "ok" : "FAIL", (int16_t)r >> 2);
        if ((r & 3) != 1) fails++;
    }
    printf("TMS9900 walkthrough: %d checks, %d failure(s), %ld instructions\n", n, fails, steps);
    printf("deepest stack: SP=%04x at pc %04x\n", minsp, minsp_pc);
    return fails != 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--walk")) return walk(argc, argv);
    if (argc < 9) {
        fprintf(stderr, "usage: cartsim rom start waitvint kscanfast joystfast charsetlc player frames\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); rom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(rom.data(), 1, rom.size(), f) != rom.size()) return 1;
    fclose(f);
    uint16_t a_start = strtol(argv[2], 0, 16), a_wait = strtol(argv[3], 0, 16),
             a_kscan = strtol(argv[4], 0, 16), a_joy = strtol(argv[5], 0, 16),
             a_cset = strtol(argv[6], 0, 16), a_player = strtol(argv[7], 0, 16);
    int frames = atoi(argv[8]);
    uint16_t a_trace = argc > 9 ? strtol(argv[9], 0, 16) : 0, trace_ret = 0;
    uint16_t a_map = argc > 10 ? strtol(argv[10], 0, 16) : 0;   /* cur_map */

    Sim cpu;
    bank = 0;
    cpu.branch(a_start);
    int frame = 0, kcalls = 0;
    long steps = 0;
    for (;;) {
        uint16_t pc = cpu.getPC();
        if (pc == a_wait) {
            frame++;
            uint16_t pp = cpu.rd16(a_player);
            if (pp) {
                /* Entity is all ints: type,key,x,y,w,h,enabled,visible,solid,held,dir */
                int16_t x = cpu.rd16(pp + 4), y = cpu.rd16(pp + 6);
                printf("frame %3d bank %2d map %2d player@%04x x=%d y=%d dir=%d\n",
                       frame, bank, a_map ? (int16_t)cpu.rd16(a_map) : -1,
                       pp, x, y, (int16_t)cpu.rd16(pp + 20));
            }
            if (frame >= frames) break;
            cpu.ret(); continue;
        }
        if (pc == a_kscan) {                      /* title: release, then fire */
            kcalls++;
            ram[0x8375] = (kcalls == 3) ? 18 : 0xff;
            cpu.ret(); continue;
        }
        if (pc == a_joy) {                        /* hold RIGHT from frame 5 */
            ram[0x8377] = (frame >= 5) ? 4 : 0;
            ram[0x8376] = 0;
            cpu.ret(); continue;
        }
        if (pc == a_cset) { cpu.ret(); continue; }
        if (a_trace && pc == a_trace && frame >= 5 && frame <= 7) {
            printf("  is_blocked(%d,%d) from %04x\n", (int16_t)cpu.R(1), (int16_t)cpu.R(2), cpu.R(11));
            trace_ret = cpu.R(11);
        }
        if (trace_ret && pc == trace_ret) { printf("   -> %d\n", (int16_t)cpu.R(1)); trace_ret = 0; }
        uint16_t op = cpu.fetch();
        cpu.execute(op);
        if (++steps > 200000000L) { printf("step limit, pc=%04x\n", pc); break; }
    }
    /* dump collision for the player's rows so it can be compared to the host */
    FILE *o = fopen("ram.bin", "wb"); fwrite(ram, 1, 65536, o); fclose(o);
    o = fopen("vram.bin", "wb"); fwrite(vram, 1, 16384, o); fclose(o);
    o = fopen("vregs.bin", "wb"); fwrite(vreg, 1, 8, o); fclose(o);
    return 0;
}
