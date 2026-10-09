# Paths to TMS9900 compilation tools 
# ( Set in environment to override paths )
#TMS9900_DIR?=/home/tursilion/gcc9900/bin
TMS9900_DIR?=/home/tursi/newtms9900-gcc/newgcc9900/bin
ELF2EA5_DIR?=/home/tursi/gcc9900/bin
EA5_SPLIT_DIR?=/home/tursi/gcc9900/bin
CLASSIC99_DSK1?=/mnt/d/classic99/DSK1/
LIBTI99?=/mnt/d/work/ti/libti99/

# Full paths to the executables used
GAS=$(TMS9900_DIR)/tms9900-as
LD=$(TMS9900_DIR)/tms9900-ld
CC=$(TMS9900_DIR)/tms9900-gcc

CP=/usr/bin/cp
NAME=ssa

# Path to elf2cart conversion utility
ELF2CART=/mnt/d/work/ti/Elf2BankedCart/elf2bankedcart

# Flags used during linking
#
# Locate the code (.text section) and the data (.data section)
# a linker script is necessary to define the output sections (that will replace the list in the Coleco CRT0)
# That will also let us set the start addresses. The ELF should in theory contain section names which will let
# out custom cartridge builder figure out where things go.
# https://ftp.gnu.org/old-gnu/Manuals/ld-2.9.1/html_node/ld_19.html
LDFLAGS_EA5=-M -T linker.ld

INCPATH=-I/mnt/d/work/libti99ALL -I/mnt/d/work/ti/vgmcomp2/Players/libtivgm2
LIBPATH=-L/mnt/d/work/libti99ALL/buildti -L/mnt/d/work/ti/vgmcomp2/Players/libtivgm2
LIBS=-lti99 -ltivgm2

# Note: no-function-cse prevents gcc from preferring registers for bl calls
CFLAGS=-Os -std=c99 -c -s --save-temp -DTI99 -fno-builtin -fno-function-cse
#-minline_rtl


.PHONY: all clean

# crt0 must be first!
OBJECT_LIST = crt0.o superspaceacer.o trampolines.o ssd_inc.o ssd_inc2.o bg.o boss.o bossdat.o bossdraw.o bossdraw2.o enemy.o music.o wineasy.o \
        winhard.o winhard2.o title_c.o title_p.o boss1.o boss2.o boss3.o boss4.o boss5.o outfile0.o outfile1.o \
        outfile2.o outfile3.o font.o difficulty.o selena_end_c.o selena_end_p.o titlepage.o ship1_c.o ship1_p.o \
        ship2_c.o ship2_p.o ship3_c.o ship3_p.o ship4_c.o ship4_p.o qwertianc.o qwertianp.o attract.o sfxs.o \
        ladybugc.o ladybugp.o ladybugscreenc.o ladybugscreenp.o gnat1rlec.o gnat1rlep.o gnat2rlec.o gnat2rlep.o \
        gnat3rlec.o gnat3rlep.o gnat4rlec.o gnat4rlep.o gnat5rlec.o gnat5rlep.o snowballbasec.o snowballbasep.o \
        human.o human2.o winmed.o cruiserendc.o cruiserendp.o f18sprites.o f18load.o f18load2.o f18load3.o f18load4.o f18abosses.o \
        hllogoc.o hllogop.o soundtest.o input.o highscores.o

# List of all files needed in executable
PREREQUISITES=$(OBJECT_LIST)

all: $(PREREQUISITES)
	$(LD) $(OBJECT_LIST) $(LIBS) $(LIBPATH) $(LDFLAGS_EA5) -o $(NAME).ea5.elf > ea5.map
	$(ELF2CART) $(NAME).ea5.elf $(NAME)8.bin
	$(CP) $(NAME)8.bin ../ubergrom/ssa_uber_rom8.bin
    # just to remove the ROM header byte (we'll use the GROM instead to boot)
	/mnt/d/work/PatchBin/release/PatchBin.exe ../ubergrom/ssa_uber_rom8.bin -bank 2000 0000 00
	cat ../ubergrom/ssa_uber_rom8.bin ../ubergrom/ssa_uber_rom8.bin > ../ubergrom/ssa_uber_rom512_8.bin

clean:
	-rm *.o *.map *.lst *.sym *.ihx *.rom

# Since we don't need custom segment tags anymore, we could in theory just use a generic recipe...

ssd_inc.o: ssd_inc.c
	$(CC) $(CFLAGS) $(INCPATH) -o ssd_inc.o ssd_inc.c
ssd_inc2.o: ssd_inc2.c
	$(CC) $(CFLAGS) $(INCPATH) -o ssd_inc2.o ssd_inc2.c

bg.o: bg.c game.h
	$(CC) $(CFLAGS) $(INCPATH) -o bg.o bg.c

boss.o: boss.c game.h boss.h f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o boss.o boss.c
bossdat.o: bossdat.c game.h boss.h
	$(CC) $(CFLAGS) $(INCPATH) -o bossdat.o bossdat.c

bossdraw.o: bossdraw.c boss.h
	$(CC) $(CFLAGS) $(INCPATH) -o bossdraw.o bossdraw.c
bossdraw2.o: bossdraw2.c boss.h
	$(CC) $(CFLAGS) $(INCPATH) -o bossdraw2.o bossdraw2.c

enemy.o: enemy.c game.h f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o enemy.o enemy.c

music.o: music.c game.h
	$(CC) $(CFLAGS) $(INCPATH) -o music.o music.c

human.o: human.c game.h f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o human.o human.c
human2.o: human2.c game.h
	$(CC) $(CFLAGS) $(INCPATH) -o human2.o human2.c

superspaceacer.o: superspaceacer.c game.h enemy.h trampoline.h human.h music.h attract.h boss.h f18load.h highscores.h
	$(CC) $(CFLAGS) $(INCPATH) -o superspaceacer.o superspaceacer.c

input.o: input.c
	$(CC) $(CFLAGS) $(INCPATH) -o input.o input.c

highscores.o: highscores.c
	$(CC) $(CFLAGS) $(INCPATH) -o highscores.o highscores.c

titlepage.o: titlepage.c game.h
	$(CC) $(CFLAGS) $(INCPATH) -o titlepage.o titlepage.c

trampolines.o: trampolines.c game.h trampoline.h f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o trampolines.o trampolines.c

wineasy.o: wineasy.c game.h
	$(CC) $(CFLAGS) $(INCPATH) -o wineasy.o wineasy.c

winmed.o: winmed.c game.h
	$(CC) $(CFLAGS) $(INCPATH) -o winmed.o winmed.c

winhard.o: winhard.c game.h boss.h trampoline.h music.h human.h
	$(CC) $(CFLAGS) $(INCPATH) -o winhard.o winhard.c
winhard2.o: winhard2.c game.h boss.h trampoline.h music.h human.h
	$(CC) $(CFLAGS) $(INCPATH) -o winhard2.o winhard2.c

title_c.o: title_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o title_c.o title_c.c

title_p.o: title_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o title_p.o title_p.c

hllogoc.o: harmlesslion_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o hllogoc.o harmlesslion_c.c

hllogop.o: harmlesslion_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o hllogop.o harmlesslion_p.c

boss1.o: boss1.bmp
	$(CC) $(CFLAGS) $(INCPATH) -oboss1.o boss1.c

boss2.o: boss2.bmp
	$(CC) $(CFLAGS) $(INCPATH) -oboss2.o boss2.c

boss3.o: boss3.bmp
	$(CC) $(CFLAGS) $(INCPATH) -oboss3.o boss3.c

boss4.o: boss4.bmp
	$(CC) $(CFLAGS) $(INCPATH) -oboss4.o boss4.c

boss5.o: boss5.bmp
	$(CC) $(CFLAGS) $(INCPATH) -oboss5.o boss5.c

outfile0.o: outfile0.c
	$(CC) $(CFLAGS) $(INCPATH) -o outfile0.o outfile0.c
outfile1.o: outfile1.c
	$(CC) $(CFLAGS) $(INCPATH) -o outfile1.o outfile1.c
outfile2.o: outfile2.c
	$(CC) $(CFLAGS) $(INCPATH) -o outfile2.o outfile2.c
outfile3.o: outfile3.c
	$(CC) $(CFLAGS) $(INCPATH) -o outfile3.o outfile3.c

ship1_c.o: ../title2/ship1_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship1_c.o ../title2/ship1_c.c
ship1_p.o: ../title2/ship1_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship1_p.o ../title2/ship1_p.c

ship2_c.o: ../title2/ship2_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship2_c.o ../title2/ship2_c.c
ship2_p.o: ../title2/ship2_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship2_p.o ../title2/ship2_p.c

ship3_c.o: ../title2/ship3_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship3_c.o ../title2/ship3_c.c
ship3_p.o: ../title2/ship3_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship3_p.o ../title2/ship3_p.c

ship4_c.o: ../title2/ship4_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship4_c.o ../title2/ship4_c.c
ship4_p.o: ../title2/ship4_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o ship4_p.o ../title2/ship4_p.c

font.o: font.c
	$(CC) $(CFLAGS) $(INCPATH) -o font.o font.c

difficulty.o: difficulty.c trampoline.h
	$(CC) $(CFLAGS) $(INCPATH) -o difficulty.o difficulty.c
soundtest.o: soundtest.c
	$(CC) $(CFLAGS) $(INCPATH) -o soundtest.o soundtest.c

selena_end_c.o: selena_end_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o selena_end_c.o selena_end_c.c
selena_end_p.o: selena_end_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o selena_end_p.o selena_end_p.c

qwertianp.o: qwertianp.c
	$(CC) $(CFLAGS) $(INCPATH) -o qwertianp.o qwertianp.c
qwertianc.o: qwertianc.c
	$(CC) $(CFLAGS) $(INCPATH) -o qwertianc.o qwertianc.c

attract.o: attract.c attract.h
	$(CC) $(CFLAGS) $(INCPATH) -o attract.o attract.c

sfxs.o: sfxs.c
	$(CC) $(CFLAGS) $(INCPATH) -o sfxs.o sfxs.c

f18sprites.o: f18sprites8sl.c
	$(CC) $(CFLAGS) $(INCPATH) -o f18sprites.o f18sprites8sl.c
f18load.o: f18load.c f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o f18load.o f18load.c
f18load2.o: f18load2.c f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o f18load2.o f18load2.c
f18load3.o: f18load3.c f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o f18load3.o f18load3.c
f18load4.o: f18load4.c f18load.h
	$(CC) $(CFLAGS) $(INCPATH) -o f18load4.o f18load4.c
f18abosses.o: f18abosses.c
	$(CC) $(CFLAGS) $(INCPATH) -o f18abosses.o f18abosses.c

ladybugc.o : ladybugc.c
	$(CC) $(CFLAGS) $(INCPATH) -o ladybugc.o ladybugC.c
ladybugp.o : ladybugp.c
	$(CC) $(CFLAGS) $(INCPATH) -o ladybugp.o ladybugP.c

ladybugscreenc.o : ladybugscreenc.c
	$(CC) $(CFLAGS) $(INCPATH) -o ladybugscreenc.o ladybugscreenc.c
ladybugscreenp.o : ladybugscreenp.c
	$(CC) $(CFLAGS) $(INCPATH) -o ladybugscreenp.o ladybugscreenp.c

gnat1rlec.o : gnat1rlec.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat1rlec.o gnat1rlec.c
gnat1rlep.o : gnat1rlep.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat1rlep.o gnat1rlep.c
gnat2rlec.o : gnat2rlec.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat2rlec.o gnat2rlec.c
gnat2rlep.o : gnat2rlep.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat2rlep.o gnat2rlep.c
gnat3rlec.o : gnat3rlec.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat3rlec.o gnat3rlec.c
gnat3rlep.o : gnat3rlep.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat3rlep.o gnat3rlep.c
gnat4rlec.o : gnat4rlec.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat4rlec.o gnat4rlec.c
gnat4rlep.o : gnat4rlep.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat4rlep.o gnat4rlep.c
gnat5rlec.o : gnat5rlec.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat5rlec.o gnat5rlec.c
gnat5rlep.o : gnat5rlep.c
	$(CC) $(CFLAGS) $(INCPATH) -o gnat5rlep.o gnat5rlep.c

snowballbasec.o : snowballbase_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o snowballbasec.o snowballbase_c.c
snowballbasep.o : snowballbase_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o snowballbasep.o snowballbase_p.c

cruiserendc.o : cruiserend_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o cruiserendc.o cruiserend_c.c
cruiserendp.o : cruiserend_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o cruiserendp.o cruiserend_p.c

cruiserfinalc.o : cruiserfinal_c.c
	$(CC) $(CFLAGS) $(INCPATH) -o cruiserfinalc.o cruiserfinal_c.c
cruiserfinalp.o : cruiserfinal_p.c
	$(CC) $(CFLAGS) $(INCPATH) -o cruiserfinalp.o cruiserfinal_p.c

# libti99 crt0 copied into folder
crt0.o: crt0_bankcart.asm
	$(GAS) $(AFLAGS) crt0_bankcart.asm -o crt0.o
