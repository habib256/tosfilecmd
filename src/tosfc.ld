/*
 * tosfc.ld -- un programme TOS : texte, donnees, BSS contigus a partir de 0.
 * tools/elf2prg.py reloge ensuite chaque long absolu (R_68K_32) grace aux
 * relocations conservees par --emit-relocs.
 */
OUTPUT_FORMAT("elf32-m68k")
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : {
        *crt0.o(.text)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(2);
    }
    .data : {
        *(.data .data.*)
        . = ALIGN(2);
    }
    .bss (NOLOAD) : {
        *(.bss .bss.* COMMON)
        . = ALIGN(2);
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
