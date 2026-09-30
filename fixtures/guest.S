// SPDX-License-Identifier: GPL-3.0-or-later
.text
.global fixture
fixture:
    add x2, x0, x1
    str x2, [x10]
    ldr x3, [x10]
    mov x4, #10
1:  add x3, x3, #1
    subs x4, x4, #1
    b.ne 1b
    str x3, [x11]
    ldr x5, [x11]
    fmov d0, #1.5
    fmov d1, #2.0
    fadd d2, d0, d1
    dup v3.4s, w2
    add v4.4s, v3.4s, v3.4s
    str q4, [x12]
    svc #0
    b .
