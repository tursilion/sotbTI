# Bank cart header for Story of the Blanks (TI-99/4A)
# Adapted from Tursi's crt0_bankcart.asm, converted to the gas dialect used by
# this tms9900-as (leading-whitespace directives, # comments, one value per
# 'data', .ascii strings, no aorg - the linker places 'loader' at >6000).
# The 46-byte header layout to the bank-switch instruction is preserved.

 def _start

# ---- cartridge header (lives at >6000) ----
 data 0xaa01            # header byte + version
 data 0x0100            # [data start in RAM]  - filled by elf2bankedcart
 data 0x0000            # [data address in ROM]- filled by elf2bankedcart
 data _prog             # program list pointer
 data 0x0000            # [data size]          - filled by elf2bankedcart
 data 0x0000            # [bss start]           - filled by elf2bankedcart
 data 0x0000            # [bss length]          - filled by elf2bankedcart
 data 0x0000            # [fixed program start] - filled by elf2bankedcart

_prog:
 data 0x0000            # next program
 data _start            # entry point
 byte 20                # length of following name (keep = 20)
 .ascii "STORY OF THE BLANKS "
 .even

_start:
 clr @0x6000           # force bank switch to first page - ends at 46 bytes!

 limi 0
 lwpi 0x8300

# require 32K RAM at >a000
 clr @0xa000
 mov @0xa000,r0
 jne failboot
 seto @0xa000
 mov @0xa000,r0
 inc r0
 jne failboot

# copy initialized data (addresses from the cart header at >6002)
 li   r0,0x6002
 mov  *r0+, r1
 mov  *r0+, r2
 inct r0
 mov  *r0+, r3
 jeq  data_copy_end
data_copy_top:
 mov  *r2+, *r1+
 dect r3
 jgt  data_copy_top
data_copy_end:

# clear bss
 mov  *r0+, r1
 mov  *r0, r2
 jeq  bss_clear_end
bss_clear_top:
 clr  *r1+
 dect r2
 jgt  bss_clear_top
bss_clear_end:

# copy the fixed program from ROM pages into >a000 RAM via scratchpad
 li r0,0x8320
 li r1,cpcode
cpsclp:
 mov *r1+,*r0+
 ci r1,cpend
 jne cpsclp

 li r0,0xa000
 mov @0x600E,r1
 bl @0x8320

 li sp,0x3ff6

 li r0,1
 soc r0,@0x83d6

 li r1,0x0100
 mov r1,@0x83fc
 clr @0x83c4

 bl @vdpinit
 bl @main
 blwp @0x0000

cpcode:
 li r2,0x6002
 li r3,3
cplp:
 mov *r1+,*r0+
 ci r1,0x8000
 jne cplp
 li r1,0x602E
 clr *r2+
 dec r3
 jne cplp
 clr @0x6000
 b *r11
cpend:

failboot:
 li r0,0x0040
 movb r0,@0x8c02
 swpb r0
 movb r0,@0x8c02
 li r0,0x2000
 li r1,320
scrnclr:
 movb r0,@0x8c00
 dec r1
 jne scrnclr
 li r1,warn32k
txtprint:
 movb *r1+,@0x8c00
 jne txtprint
 li r1,448-12
scrnclr2:
 movb r0,@0x8c00
 dec r1
 jne scrnclr2
 lwpi 0x83e0
waitky:
 li r0,0x0500
 movb r0,@0x8374
 bl @0x000e
 clr r0
 movb @0x8375,r0
 ci r0,0xff00
 jeq waitky
 blwp @0x0000

warn32k:
 .ascii "32K REQUIRED"
 byte 0
 .even                  # keep what follows (.data image, fixed code) word-aligned:
                        # the crt0 copies with word moves, which ignore bit 0
