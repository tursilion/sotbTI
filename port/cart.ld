/* Stage-2 banked-cart linker for Story of the Blanks.
 * Code + small data in the fixed 24K region (copied to >A000 RAM by crt0).
 * Bulk DATA lives in switched 8K banks paged into the 0x6000 window:
 *   bank3 = CHR patterns (gen/chr.o)
 *   bank4 = map RLE       (gen/maps.o)
 *   bank5 = dialogue text  (gen/dialogues.o)
 * elf2bankedcart maps the bankN output sections to physical banks N (user
 * banks start at 3); the C uses CART_BANK(n) = write 0x6000 + 2*n to select.
 */
OUTPUT_FORMAT("elf32-tms9900", "elf32-tms9900", "elf32-tms9900")
OUTPUT_ARCH(tms9900)
ENTRY(_start)

MEMORY {
    INIT  (xr)  : ORIGIN = 0x6000, LENGTH = 512
    FIXED (xrw) : ORIGIN = 0xA000, LENGTH = 24K
    STACK (xrw) : ORIGIN = 0x2000, LENGTH = 8K
    CART3 (XR)  : ORIGIN = 0x602e, LENGTH = 8146
    CART4 (XR)  : ORIGIN = 0x602e, LENGTH = 8146
    CART5 (XR)  : ORIGIN = 0x602e, LENGTH = 8146
    FLASH (XR)  : ORIGIN = 0,      LENGTH = 256K
}

SECTIONS {
    PROVIDE (__executable_start = 0xA000);

    . = 0x6000;
    loader : { crt0_cart.o } > INIT AT> FLASH

    /* NOTE: this gcc puts const data in .text (rodata is merged into text), so
     * the bulk-data objects must be excluded from the .text grab as well. */
    . = 0xA000;
    fixed : {
        *(EXCLUDE_FILE(*crt0_cart.o *chr.o *maps.o *dialogues.o) .text)
        *(EXCLUDE_FILE(*crt0_cart.o *chr.o *maps.o *dialogues.o) .text.*)
        *(EXCLUDE_FILE(*crt0_cart.o *chr.o *maps.o *dialogues.o) .rodata)
        *(EXCLUDE_FILE(*crt0_cart.o *chr.o *maps.o *dialogues.o) .rodata.*)
    } > FIXED AT> FLASH

    bank3 : { *chr.o(.text .text.* .rodata .rodata.*) }       > CART3 AT> FLASH
    bank4 : { *maps.o(.text .text.* .rodata .rodata.*) }      > CART4 AT> FLASH
    bank5 : { *dialogues.o(.text .text.* .rodata .rodata.*) } > CART5 AT> FLASH

    . = 0x2080;
    PROVIDE (sdata = .);
    .data : { *(.data) *(.data.*) } > STACK AT> FLASH
    PROVIDE (edata = .);

    PROVIDE (sbss = .);
    .bss : { *(.bss) *(.bss.*) *(COMMON) } > STACK AT> FLASH
    PROVIDE (ebss = .);

    _end = .; PROVIDE (end = .);
}
