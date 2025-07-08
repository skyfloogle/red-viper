.arm
.align 4

@ Defining v810_state offsets
.struct 0
state_regs:
.struct state_regs + (4*32)
state_statusregs:
.struct state_statusregs + (4*32)
state_pc:
.struct state_pc + 4
state_flags:
.struct state_flags + 4
state_exceptflags:
.struct state_exceptflags + 4
state_cycles:
.struct state_cycles + 4
state_cycles_until_event_partial:
.struct state_cycles_until_event_partial + 4
state_cycles_until_event_full:
.struct state_cycles_until_event_full + 4

@ Defining exec_block offsets
.struct 0
block_phys_offset:
.struct block_phys_offset + 4
block_virt_loc:
.struct block_virt_loc + 4
block_size:
.struct block_size + 4
block_cycles:
.struct block_cycles + 4
block_reg_map:
.struct block_reg_map + 4

.data

.extern v810_state, serviceInt, serviceint, tVBOpt

.text

.macro ldRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]

    @ Load the CPSR
    ldr     r3, [r11, #state_flags]
    msr     cpsr_f, r3

    @ Load the reg map
    mov     r2, #0x1F
    ldr     r3, [r1, #block_reg_map]

    @ Load cached V810 registers
    and     r4, r2, r3, lsr #0
    and     r5, r2, r3, lsr #5
    and     r6, r2, r3, lsr #10
    and     r7, r2, r3, lsr #15
    and     r8, r2, r3, lsr #20
    and     r9, r2, r3, lsr #25
    ldr     r4, [r11, r4, lsl #2]
    ldr     r5, [r11, r5, lsl #2]
    ldr     r6, [r11, r6, lsl #2]
    ldr     r7, [r11, r7, lsl #2]
    ldr     r8, [r11, r8, lsl #2]
    ldr     r9, [r11, r9, lsl #2]
.endm

.macro stRegs
    ldr     r11, =v810_state
    ldr     r11, [r11]

    @ Save the CPSR
    mrs     r3, cpsr
    str     r3, [r11, #state_flags]

    @ Load the address of the reg map
    ldr     r1, [r0, #block_reg_map]
    mov     r3, #0x1F

    @ Store cached V810 registers
    and     r2, r3, r1, lsr #0
    str     r4, [r11, r2, lsl #2]
    and     r2, r3, r1, lsr #5
    str     r5, [r11, r2, lsl #2]
    and     r2, r3, r1, lsr #10
    str     r6, [r11, r2, lsl #2]
    and     r2, r3, r1, lsr #15
    str     r7, [r11, r2, lsl #2]
    and     r2, r3, r1, lsr #20
    str     r8, [r11, r2, lsl #2]
    and     r2, r3, r1, lsr #25
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
    mov     r10, #0
    bx      r0

postexec:
    pop     {r0}
    stRegs
    @ cycles += cuef - cuep
    @ cuef = cuep = cuef - cuep - r10
    ldr     r0, [r11, #state_cycles]
    ldr     r2, [r11, #state_cycles_until_event_full]
    ldr     r3, [r11, #state_cycles_until_event_partial]
    sub     r3, r10
    sub     r2, r3
    str     r3, [r11, #state_cycles_until_event_full]
    str     r3, [r11, #state_cycles_until_event_partial]
    add     r0, r2
    str     r0, [r11, #state_cycles]
    pop     {r4-r11, ip, pc}

@ Checks for pending interrupts and exits the block if necessary
.globl drc_handleInterrupts
drc_handleInterrupts:
    push    {r4, r5, lr}

    mov     r4, r0
    mov     r5, r1

    @ Save flags and PC
    str     r4, [r11, #state_flags]
    str     r5, [r11, #state_pc]

    @ cycles += cuef - cuep - r10
    @ cuef = cuep = cuef - cuep - r10
    @ r10 = 0
    ldr     r0, [r11, #state_cycles]
    ldr     r2, [r11, #state_cycles_until_event_full]
    ldr     r3, [r11, #state_cycles_until_event_partial]
    sub     r3, r10
    sub     r2, r3
    str     r3, [r11, #state_cycles_until_event_full]
    str     r3, [r11, #state_cycles_until_event_partial]
    add     r0, r2
    str     r0, [r11, #state_cycles]
    mov     r10, #0

    bl      serviceInt
    cmp     r0, #0
    bne     exit_block

ret_to_block:
    @ Return to the block
    mov     r0, r4
    pop     {r4, r5, pc}

exit_block:
    @ Restore CPSR
    msr     cpsr_f, r4

    @ Exit the block ignoring linked return address
    pop     {r4, r5}
    pop     {r0, pc}
