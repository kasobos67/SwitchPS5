// SPDX-License-Identifier: GPL-3.0-or-later
.text
.global multicore_fixture
multicore_fixture:
    mov x9, #4096
    ldaxr x0, [x10]
    svc #1                 // All four cores hold the initial reservation.
2:  add x0, x0, #1
    stlxr w1, x0, [x10]
    add x7, x7, x1          // Count failed exclusive stores.
    cbz w1, 3f
1:  ldaxr x0, [x10]
    b 2b
3:  subs x9, x9, #1
    b.ne 1b
    mrs x8, tpidr_el0
    svc #0
    b .
