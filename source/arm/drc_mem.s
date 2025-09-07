.arm
.align 4

.globl mem_rbyte
mem_rbyte:
    ldr r2, =drc_mem_rbyte
    asr r3, r0, #24
    and r3, r3, #7
    ldr r2, [r2, r3, lsl #2]
    bx r2
drc_mem_rbyte:
.word       mem_vip_rbyte
.word       mem_nop
.word       mem_hw_read
.word       mem_nop
.word       mem_nop
.word       mem_wram_rbyte
.word       mem_sram_rbyte
.word       mem_rom_rbyte

.globl mem_rhword
mem_rhword:
    bic r0, r0, #1
    ldr r2, =drc_mem_rhword
    asr r3, r0, #24
    and r3, r3, #7
    ldr r2, [r2, r3, lsl #2]
    bx r2
drc_mem_rhword:
.word       mem_vip_rhword
.word       mem_nop
.word       mem_hw_read
.word       mem_nop
.word       mem_nop
.word       mem_wram_rhword
.word       mem_sram_rhword
.word       mem_rom_rhword

.globl mem_rword
mem_rword:
    bic r0, r0, #3
    ldr r2, =drc_mem_rword
    asr r3, r0, #24
    and r3, r3, #7
    ldr r2, [r2, r3, lsl #2]
    bx r2
drc_mem_rword:
.word       mem_vip_rword
.word       mem_nop
.word       mem_hw_read
.word       mem_nop
.word       mem_nop
.word       mem_wram_rword
.word       mem_sram_rword
.word       mem_rom_rword

.globl mem_wbyte
mem_wbyte:
    ldr r2, =drc_mem_wbyte
    asr r3, r0, #24
    and r3, r3, #7
    ldr r2, [r2, r3, lsl #2]
    bx r2
drc_mem_wbyte:
.word       mem_vip_wbyte
.word       mem_vsu_write
.word       mem_hw_write
.word       mem_nop
.word       mem_nop
.word       mem_wram_wbyte
.word       mem_sram_wbyte
.word       mem_nop

.globl mem_whword
mem_whword:
    bic r0, r0, #1
    ldr r2, =drc_mem_whword
    asr r3, r0, #24
    and r3, r3, #7
    ldr r2, [r2, r3, lsl #2]
    bx r2
drc_mem_whword:
.word       mem_vip_whword
.word       mem_vsu_write
.word       mem_hw_write
.word       mem_nop
.word       mem_nop
.word       mem_wram_whword
.word       mem_sram_whword
.word       mem_nop

.globl mem_wword
mem_wword:
    bic r0, r0, #3
    ldr r2, =drc_mem_wword
    asr r3, r0, #24
    and r3, r3, #7
    ldr r2, [r2, r3, lsl #2]
    bx r2
drc_mem_wword:
.word       mem_vip_wword
.word       mem_vsu_write
.word       mem_hw_write
.word       mem_nop
.word       mem_nop
.word       mem_wram_wword
.word       mem_sram_wword
.word       mem_nop
