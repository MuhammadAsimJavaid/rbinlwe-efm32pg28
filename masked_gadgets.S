/*
 * First-order Boolean-masked nonlinear gadgets for Arm Cortex-M33.
 *
 * AAPCS arguments for masked_and_bit:
 *   r0 = a0, r1 = a1, r2 = b0, r3 = b1
 *   [entry sp]     = random_bit
 *   [entry sp, #4] = out0
 *   [entry sp, #8] = out1
 *
 * All four ISW cross-products are formed in distinct registers. In
 * particular, this routine never computes a0 ^ a1 or b0 ^ b1.
 */

    .syntax unified
    .cpu cortex-m33
    .thumb

    .section .text.masked_and_bit, "ax", %progbits
    .align 2
    .global masked_and_bit
    .type masked_and_bit, %function
    .thumb_func
masked_and_bit:
    push    {r4-r7, lr}

    and.w   r4, r0, r2          /* t00 = a0 & b0 */
    and.w   r5, r0, r3          /* t01 = a0 & b1 */
    and.w   r6, r1, r2          /* t10 = a1 & b0 */
    and.w   r7, r1, r3          /* t11 = a1 & b1 */

    ldrb.w  r12, [sp, #20]
    and.w   r12, r12, #1

    eor.w   r4, r4, r12         /* c0 = t00 ^ r */
    eor.w   r5, r5, r6
    eor.w   r5, r5, r7
    eor.w   r5, r5, r12         /* c1 = t01 ^ t10 ^ t11 ^ r */

    ldr     r0, [sp, #24]
    ldr     r1, [sp, #28]
    strb    r4, [r0]
    strb    r5, [r1]

    /* Do not leave cross-products or output shares in caller-visible temps. */
    movs    r4, #0
    movs    r5, #0
    movs    r6, #0
    movs    r7, #0
    movs    r2, #0
    movs    r3, #0
    mov.w   r12, #0

    pop     {r4-r7, pc}
    .size masked_and_bit, .-masked_and_bit

/* Refresh arithmetic polynomial shares in place:
 *   share0' = share0 + delta mod q
 *   share1' = share1 - delta mod q
 * r0=share0, r1=share1, r2=random, r3=length, [entry sp]=q-1. */
    .section .text.masked_refresh_poly, "ax", %progbits
    .align 2
    .global masked_refresh_poly
    .type masked_refresh_poly, %function
    .thumb_func
masked_refresh_poly:
    push    {r4-r7, lr}
    ldr     r7, [sp, #20]
1:
    ldrb    r4, [r2], #1
    ldrb    r5, [r0]
    ldrb    r6, [r1]
    ands    r4, r7
    adds    r5, r5, r4
    subs    r6, r6, r4
    ands    r5, r7
    ands    r6, r7
    strb    r5, [r0], #1
    strb    r6, [r1], #1
    subs    r3, r3, #1
    bne     1b
    movs    r4, #0
    movs    r5, #0
    movs    r6, #0
    movs    r7, #0
    pop     {r4-r7, pc}
    .size masked_refresh_poly, .-masked_refresh_poly

/*
 * Fault-hardened CCA2 message selection.
 *
 * AAPCS arguments:
 *   r0 = output, r1 = authenticated candidate, r2 = rejection message
 *   r3 = independently computed forward validity mask
 *   [entry sp]     = independently computed reverse validity mask
 *   [entry sp, #4] = message length
 *
 * The two masks deliberately remain separate through two distinct AND
 * instructions. A single skipped AND or corruption of one mask cannot release
 * candidate data for an invalid ciphertext while the other mask remains zero.
 */
    .section .text.pke_select_msg_hardened, "ax", %progbits
    .align 2
    .global pke_select_msg_hardened
    .type pke_select_msg_hardened, %function
    .thumb_func
pke_select_msg_hardened:
    push    {r4-r7, lr}
    ldr     r4, [sp, #20]       /* reverse mask */
    ldr     r5, [sp, #24]       /* message length */
    uxtb    r3, r3              /* forward mask */
    uxtb    r4, r4              /* reverse mask */

1:
    ldrb    r6, [r1], #1        /* candidate byte */
    ldrb    r7, [r2], #1        /* rejection byte */
    eors    r6, r7              /* candidate ^ rejection */
    ands    r6, r3              /* independent forward authorization */
    ands    r6, r4              /* independent reverse authorization */
    eors    r6, r7
    strb    r6, [r0], #1
    subs    r5, r5, #1
    bne     1b

    movs    r3, #0
    movs    r4, #0
    movs    r5, #0
    movs    r6, #0
    movs    r7, #0
    pop     {r4-r7, pc}
    .size pke_select_msg_hardened, .-pke_select_msg_hardened

    .section .note.GNU-stack, "", %progbits
