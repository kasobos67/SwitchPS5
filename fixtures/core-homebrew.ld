/* SPDX-License-Identifier: GPL-3.0-or-later */
ENTRY(_start)
SECTIONS {
    . = 0;
    .text : { KEEP(*(.nro_header)) *(.text*) . = ALIGN(4096); }
    __text_size = SIZEOF(.text);
    __rodata_start = .;
    .rodata : { *(.rodata*) . = ALIGN(4096); }
    __rodata_size = SIZEOF(.rodata);
    __data_start = .;
    .data : { *(.data*) . = MAX(., __data_start + 4096); }
    __data_size = SIZEOF(.data);
    __image_size = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(4096); }
    __bss_size = SIZEOF(.bss);
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
