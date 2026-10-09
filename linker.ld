/* 
 256k banked cartridge with 32k RAM support
 Fixed bank is copied into 32k by the CRT0.
 The cartridge builder deal with how that works,
 and we will assume a maximum size of 24k (the
 entire high memory bank). All other pages here
 become 8k, so for now I'll use a PAGEA/PAGEB
 concept to split up the Coleco pages.
*/

OUTPUT_FORMAT("elf32-tms9900", "elf32-tms9900",
              "elf32-tms9900")
OUTPUT_ARCH(tms9900)
ENTRY(_start)
SEARCH_DIR("/home/tursi/gcc9900/tms9900/lib");

/* banked offset of 46 bytes to account for header space */
/* We also make the 24k block smaller by 46*3 so we can */
/* Although this script gives 24k, note we lose an extra */
/* 92 bytes for the headers in page 2 and 3 */

MEMORY
{
    INIT(xr) : ORIGIN = 0x6000, LENGTH = 512

    FIXED(xrw) : ORIGIN = 0xA000, LENGTH = 24K
    STACK(xrw) : ORIGIN = 0x2000, LENGTH = 8K
    SCRATCH(rw) : ORIGIN = 0x8320, LENGTH = 224
    CART1A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART1B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART2A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART2B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART3A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART3B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART4A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART4B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART5A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART5B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART6A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART6B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART7A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART7B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART8A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART8B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART9A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART9B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART10A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART10B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART11A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART11B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART12A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART12B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART13A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART13B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART14A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART14B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART15A(XR) : ORIGIN = 0x602e, LENGTH = 8146
    CART15B(XR) : ORIGIN = 0x602e, LENGTH = 8146
    
    FLASH(XR) : ORIGIN = 0, LENGTH = 256K
}

SECTIONS {
    PROVIDE (__executable_start = 0xA000); 

    PROVIDE (stext = .);
    
    . = 0x6000;
    
    loader :
    {
        crt0.o
    } > INIT AT> FLASH

    . = 0xA000;
    fixed :
    {
        superspaceacer.o(.text .rodata)
        trampolines.o(.text .rodata)
        music.o(.text .rodata)
        input.o(.text .rodata)
        *libti99.a:*(.text .rodata)
        *libtivgm2.a:*(.text .rodata)
    } > FIXED AT> FLASH
    
    PROVIDE (etext = .);
    
    bank1a :
    {
        /* not used - this is part of fixed above */
        PROVIDE(ebank1a = .);
    } > CART1A AT> FLASH
    
    bank1b :
    {
        snowballbasep.o(.text .rodata)
        snowballbasec.o(.text .rodata)
        PROVIDE(ebank1b = .);
    } > CART1B AT> FLASH
        
    bank2a :
    {
        human.o(.text .rodata)
        PROVIDE(ebank2a = .);
    } > CART2A AT> FLASH

    bank2b :
    {
        human2.o(.text .rodata)
        cruiserendc.o(.text .rodata)
        PROVIDE(ebank2b = .);
    } > CART2B AT> FLASH

    bank3a :
    {
        outfile1.o(.text .rodata)
        PROVIDE(ebank3a = .);
    } > CART3A AT> FLASH

    bank3b :
    {
        selena_end_c.o(.text .rodata)
        selena_end_p.o(.text .rodata)
        soundtest.o(.text .rodata)
        PROVIDE(ebank3b = .);
    } > CART3B AT> FLASH

    bank4a :
    {
        difficulty.o(.text .rodata)
        sfxs.o(.text .rodata)
        PROVIDE(ebank4a = .);
    } > CART4A AT> FLASH

    bank4b :
    {
        outfile0.o(.text .rodata)
        PROVIDE(ebank4b = .);
    } > CART4B AT> FLASH

    bank5a :
    {
        boss1.o(.text .rodata)
        boss2.o(.text .rodata)
        boss3.o(.text .rodata)
        boss4.o(.text .rodata)
        boss5.o(.text .rodata)
        gnat5rlec.o(.text .rodata)
        gnat5rlep.o(.text .rodata)
        PROVIDE(ebank5a = .);
    } > CART5A AT> FLASH

    bank5b :
    {
        ssd_inc.o(.text .rodata)
        PROVIDE(ebank5b = .);
    } > CART5B AT> FLASH

    bank6a :
    {
        bg.o(.text .rodata)
        outfile2.o(.text .rodata)
        PROVIDE(ebank6a = .);
    } > CART6A AT> FLASH

    bank6b :
    {
        outfile3.o(.text .rodata)
        font.o(.text .rodata)
        PROVIDE(ebank6b = .);
    } > CART6B AT> FLASH

    bank7a :
    {
        boss.o(.text .rodata)
        PROVIDE(ebank7a = .);
    } > CART7A AT> FLASH

    bank7b :
    {
        bossdraw.o(.text .rodata)
        f18load.o(.text .rodata)
        bossdraw2.o(.text .rodata)
        PROVIDE(ebank7b = .);
    } > CART7B AT> FLASH

    bank8a :
    {
        wineasy.o(.text .rodata)
        winmed.o(.text .rodata)
        ssd_inc2.o(.text .rodata)
        bossdat.o(.text .rodata)
        highscores.o(.text .rodata)
        PROVIDE(ebank8a = .);
    } > CART8A AT> FLASH

    bank8b :
    {	
        winhard.o(.text .rodata)
        PROVIDE(ebank8b = .);
    } > CART8B AT> FLASH

    bank9a :
    {
        titlepage.o(.text .rodata)
        title_c.o(.text .rodata)
        title_p.o(.text .rodata)
        PROVIDE(ebank9a = .);
    } > CART9A AT> FLASH

    bank9b :
    {
        ship1_c.o(.text .rodata)
        ship1_p.o(.text .rodata)
        f18load3.o(.text .rodata)
        PROVIDE(ebank9b = .);
    } > CART9B AT> FLASH

    bank10a :
    {
        ship2_c.o(.text .rodata)
        ship2_p.o(.text .rodata)
        ship3_c.o(.text .rodata)
        PROVIDE(ebank10a = .);
    } > CART10A AT> FLASH

    bank10b :
    {
        ship3_p.o(.text .rodata)
        ship4_c.o(.text .rodata)
        ship4_p.o(.text .rodata)
        PROVIDE(ebank10b = .);
    } > CART10B AT> FLASH

    bank11a :
    {
        ladybugc.o(.text .rodata)
        PROVIDE(ebank11a = .);
    } > CART11A AT> FLASH

    bank11b :
    {
        ladybugp.o(.text .rodata)
        ladybugscreenc.o(.text .rodata)
        ladybugscreenp.o(.text .rodata)
        PROVIDE(ebank11b = .);
    } > CART11B AT> FLASH

    bank12a :
    {
        qwertianc.o(.text .rodata)
        qwertianp.o(.text .rodata)
        attract.o(.text .rodata)
        cruiserendp.o(.text .rodata)
        PROVIDE(ebank12a = .);
    } > CART12A AT> FLASH

    bank12b :
    {
        gnat1rlec.o(.text .rodata)
        gnat1rlep.o(.text .rodata)
        hllogop.o(.text .rodata)
        PROVIDE(ebank12b = .);
    } > CART12B AT> FLASH

    bank13a :
    {
        gnat3rlec.o(.text .rodata)
        gnat3rlep.o(.text .rodata)
        gnat4rlec.o(.text .rodata)
        gnat4rlep.o(.text .rodata)
        PROVIDE(ebank13a = .);
    } > CART13A AT> FLASH

    bank13b :
    {
        gnat2rlep.o(.text .rodata)
        gnat2rlec.o(.text .rodata)
        f18load4.o(.text .rodata)
        PROVIDE(ebank13b = .);
    } > CART13B AT> FLASH

    bank14a :
    {
        f18sprites.o(.text .rodata)
        PROVIDE(ebank14a = .);
    } > CART14A AT> FLASH

    bank14b :
    {
        enemy.o(.text .rodata)
        winhard2.o(.text .rodata)
        PROVIDE(ebank14b = .);
    } > CART14B AT> FLASH

    bank15a :
    {
        f18abosses.o(.text .rodata)
        PROVIDE(ebank15a = .);
    } > CART15A AT> FLASH

    bank15b :
    {
        f18load2.o(.text .rodata)
        hllogoc.o(.text .rodata)
        PROVIDE(ebank15b = .);
    } > CART15B AT> FLASH

    . = 0x2080;
    PROVIDE(sdata = .);
    .data : { *(.data) } > STACK AT> FLASH
    PROVIDE(edata = .);
    
    PROVIDE(sbss = .);
    .bss :  { *(.bss) libti99.a:*(.bss)  } > STACK AT> FLASH

    PROVIDE(ebss = .);

    songDatVars : { *(.songDatVars) } > STACK AT> FLASH
    
    _end = .; PROVIDE (end = .);
}
