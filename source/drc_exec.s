.arm
.align 4

.data

.extern v810_state

.text

.macro ldRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]
    @ Load the address of the reg map (block+17)
    add     r1, r0, #17

    @ Load the CPSR
    ldr     r3, [r11, #(34*4)]
    msr     cpsr_flg, r3

    @ Load regs
    ldrb    r2, [r1], #1
    ldr     r4, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    ldr     r5, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    ldr     r6, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    ldr     r7, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    ldr     r8, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    ldr     r9, [r11, r2, lsl #2]
    ldrb    r2, [r1], #1
    ldr     r10, [r11, r2, lsl #2]
.endm

.macro stRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]
    @ Load the address of the reg map (block+17)
    add     r1, r0, #17

    @ Save the CPSR
    mrs     r3, cpsr
    str     r3, [r11, #(34*4)]

    @ Store regs
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
    ldrb    r2, [r1], #1
    str     r10, [r11, r2, lsl #2]
.endm

@ void v810_executeBlock(exec_block* block);

.globl v810_executeBlock
v810_executeBlock:
    push    {r4-r11, ip, lr}
    push    {r0}

    @ The last instruction on the block should be "pop {pc}"
    ldr     lr, =postexec
    push    {lr}

    ldRegs
    ldr     pc, [r0]

postexec:
    pop     {r0}
    stRegs
    pop     {r4-r11, ip, pc}