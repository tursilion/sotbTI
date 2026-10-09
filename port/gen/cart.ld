OUTPUT_FORMAT("elf32-tms9900","elf32-tms9900","elf32-tms9900")
OUTPUT_ARCH(tms9900)
ENTRY(_start)
MEMORY {
  INIT  (xr)  : ORIGIN = 0x6000, LENGTH = 512
  FIXED (xrw) : ORIGIN = 0xA000, LENGTH = 0x5e00
  STACK (xrw) : ORIGIN = 0x2000, LENGTH = 8K
  CART3   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART4   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART5   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART6   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART7   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART8   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART9   (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART10  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART11  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART12  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART13  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART14  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART15  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  CART16  (XR) : ORIGIN = 0x602e, LENGTH = 8146
  FLASH (XR)  : ORIGIN = 0, LENGTH = 16M
}
SECTIONS {
  PROVIDE (__executable_start = 0xA000);
  . = 0x6000;
  loader : { crt0_cart.o } > INIT AT> FLASH
  . = 0xA000;
  fixed : {
    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .text)
    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .text.*)
    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .rodata)
    *(EXCLUDE_FILE(*crt0_cart.o *bk[0-9][0-9].o) .rodata.*)
  } > FIXED AT> FLASH
  bank3 : { *bk03.o(.text .text.* .rodata .rodata.*) } > CART3 AT> FLASH
  bank4 : { *bk04.o(.text .text.* .rodata .rodata.*) } > CART4 AT> FLASH
  bank5 : { *bk05.o(.text .text.* .rodata .rodata.*) } > CART5 AT> FLASH
  bank6 : { *bk06.o(.text .text.* .rodata .rodata.*) } > CART6 AT> FLASH
  bank7 : { *bk07.o(.text .text.* .rodata .rodata.*) } > CART7 AT> FLASH
  bank8 : { *bk08.o(.text .text.* .rodata .rodata.*) } > CART8 AT> FLASH
  bank9 : { *bk09.o(.text .text.* .rodata .rodata.*) } > CART9 AT> FLASH
  bank10 : { *bk10.o(.text .text.* .rodata .rodata.*) } > CART10 AT> FLASH
  bank11 : { *bk11.o(.text .text.* .rodata .rodata.*) } > CART11 AT> FLASH
  bank12 : { *bk12.o(.text .text.* .rodata .rodata.*) } > CART12 AT> FLASH
  bank13 : { *bk13.o(.text .text.* .rodata .rodata.*) } > CART13 AT> FLASH
  bank14 : { *bk14.o(.text .text.* .rodata .rodata.*) } > CART14 AT> FLASH
  bank15 : { *bk15.o(.text .text.* .rodata .rodata.*) } > CART15 AT> FLASH
  bank16 : { *bk16.o(.text .text.* .rodata .rodata.*) } > CART16 AT> FLASH
  . = 0x2080;
  .data : { *(.data) *(.data.*) } > STACK AT> FLASH
  .bss : { *(.bss) *(.bss.*) *(COMMON) } > STACK AT> FLASH
  _end = .;
}
