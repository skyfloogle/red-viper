.arm
.align 4

.data

.extern v810_state, serviceDisplayInt, serviceInt

.text

.macro ldRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]

    @ Load the CPSR
    ldr     r3, [r11, #(34*4)]
    msr     cpsr_flg, r3

    @ Load the address of the reg map (block+17)
    add     r3, r1, #17

    @ Load regs
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
    ldrb    r2, [r3], #1
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

@ void v810_executeBlock(WORD* entrypoint, exec_block* block);

.globl v810_executeBlock
v810_executeBlock:
    push    {r4-r11, ip, lr}
    push    {r1}

    @ The last instruction on the block should be "pop {pc}"
    ldr     lr, =postexec
    push    {lr}

    ldRegs
    bx      r0

postexec:
    pop     {r0}
    stRegs
    pop     {r4-r11, ip, pc}

@ Checks for pending interrupts and exits the block if necessary
.globl drc_handleInterrupts
drc_handleInterrupts:
    push    {r4, lr}

    @ Move the return PC to r4
    mov     r4, r0

    @ Backup CPSR
    mrs     r1, cpsr
    push    {r1}

    @ Load v810_state->cycles
    ldr     r0, [r11, #67<<2]
    bl      serviceDisplayInt
    cmp     r0, #0
    beq     ret_to_block

exit_block:
    @ Save the new PC
    str     r4, [r11, #33<<2]

    @ Store -1 in v810_state->cycles to indicate a frame interrupt
    mov     r0, #-1
    str     r0, [r11, #67<<2]
    @ Restore CPSR
    pop     {r1}
    msr     cpsr, r1

    @ Exit the block ignoring linked return address
    pop     {r4}
    pop     {r0, pc}

ret_to_block:
    bl      serviceInt

    @ Restore CPSR and return to the block
    pop     {r1}
    msr     cpsr, r1
    pop     {r4, pc}
