// SPDX-License-Identifier: GPL-3.0-or-later
// Self-contained, unencrypted NRO; no SDK, firmware or game assets.
.section .nro_header,"ax"
.global _start
_start:
    b entry
    .word 0
    .ascii "HOMEBREW"
    .ascii "NRO0"
    .word 0
    .word __image_size
    .word 0
    .word 0, __text_size
    .word __rodata_start, __rodata_size
    .word __data_start, __data_size
    .word __bss_size
    .space 0x80 - (. - _start)
entry:
    bl guest_main
    svc #0x7
    b .
