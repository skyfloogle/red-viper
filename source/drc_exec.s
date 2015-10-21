.arm
.align 4

.data

.extern v810_state, serviceDisplayInt, serviceInt, serviceint, tVBOpt

.text

.macro ldRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]

    @ Load the CPSR
    ldr     r3, [r11, #(34*4)]
    msr     cpsr, r3

    @ Load the address of the reg map (block+17)
    add     r3, r1, #17

    @ Load cached V810 registers
    ldrb    r2, [r3], #1
    ldr     r4, [r11, r2, lsl #2]
    ldrb    r2, [r3], #1
    ldr     r5, [r11, r2, lsl #2]
    ldrb    r2, [r3], #1
    ldr     r6, [r11, r2, lsl #2]
    ldrb    r2, [r3], #1
    ldr     r7, [r11, r2, lsl #2]
    ldrb    r2, [r3], #1
    ldr     r8, [r11, r2, lsl #2]
    ldrb    r2, [r3], #1
    ldr     r9, [r11, r2, lsl #2]
.endm

.macro stRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]
    @ Load the address of the reg map (block+17)
    add     r1, r0, #17

    @ Save the CPSR
    mrs     r3, cpsr
    str     r3, [r11, #(34*4)]

    @ Store cached V810 registers
    ldrb    r2, [r1], #1
    str     r4, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    str     r5, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    str     r6, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    str     r7, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    str     r8, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    str     r9, [r11, r2, lsl #2]
.endm

@ void drc_executeBlock(WORD* entrypoint, exec_block* block);

.globl drc_executeBlock
drc_executeBlock:
    push    {r4-r11, ip, lr}
    push    {r1}

    @ The last instruction on the block should be "pop {pc}"
    ldr     lr, =postexec
    push    {lr}

    ldRegs
    @ r10 = -MAXCYCLES
    ldr     r10, =tVBOpt
    ldr     r10, [r10]
    neg     r10, r10
    bx      r0

postexec:
    pop     {r0}
    stRegs
    @ v810_state->cycles += MAXCYCLES + r10 excess
    ldr     r0, =tVBOpt
    ldr     r0, [r0]
    add     r10, r0
    ldr     r0, [r11, #67<<2]
    add     r0, r10
    str     r0, [r11, #67<<2]
    pop     {r4-r11, ip, pc}

@ Checks for pending interrupts and exits the block if necessary
.globl drc_handleInterrupts
drc_handleInterrupts:
    push    {r4, r5, lr}

    mov     r4, r0
    mov     r5, r1

    @ Load v810_state->cycles and add it to the total number of cycles
    @ (MAXCYCLES + r10 excess)
    ldr     r0, [r11, #67<<2]
    add     r0, r10
    ldr     r2, =tVBOpt
    ldr     r2, [r2]
    add     r0, r2
    str     r0, [r11, #67<<2]

    bl      serviceDisplayInt
    cmp     r0, #0
    bne     exit_block

    ldr     r0, [r11, #67<<2]
    mov     r1, r5
    bl      serviceInt
    cmp     r0, #0
    bne     exit_block

ret_to_block:
    @ r10 = -MAXCYCLES
    ldr     r10, =tVBOpt
    ldr     r10, [r10]
    neg     r10, r10

    @ Return to the block
    mov     r0, r4
    pop     {r4, r5, pc}

exit_block:
    @ Restore CPSR
    msr     cpsr, r4

    @ Exit the block ignoring linked return address
    pop     {r4, r5}
    pop     {r0, pc}
