.arm
.align 4

.extern __divsi3, __modsi3, __udivsi3, __umodsi3, mem_rbyte, mem_rhword, mem_rword, mem_wbyte, mem_whword, mem_wword, ins_err, ins_rev, drc_clearScreenForGolf, baseball2_scaling

.text
@ A cheap relocation table
.globl drc_relocTable
drc_relocTable:
.word       mem_vip_rbyte
.word       mem_nop
.word       mem_hw_read
.word       mem_nop
.word       mem_nop
.word       mem_wram_rbyte
.word       mem_sram_rbyte
.word       mem_rom_rbyte

.word       mem_vip_rhword
.word       mem_nop
.word       mem_hw_read
.word       mem_nop
.word       mem_nop
.word       mem_wram_rhword
.word       mem_sram_rhword
.word       mem_rom_rhword

.word       mem_vip_rword
.word       mem_nop
.word       mem_hw_read
.word       mem_nop
.word       mem_nop
.word       mem_wram_rword
.word       mem_sram_rword
.word       mem_rom_rword

.word       mem_vip_wbyte
.word       mem_vsu_write
.word       mem_hw_write
.word       mem_nop
.word       mem_nop
.word       mem_wram_wbyte
.word       mem_sram_wbyte
.word       mem_nop

.word       mem_vip_whword
.word       mem_vsu_write
.word       mem_hw_write
.word       mem_nop
.word       mem_nop
.word       mem_wram_whword
.word       mem_sram_whword
.word       mem_nop

.word       mem_vip_wword
.word       mem_vsu_write
.word       mem_hw_write
.word       mem_nop
.word       mem_nop
.word       mem_wram_wword
.word       mem_sram_wword
.word       mem_nop

.word       ins_sch0bsu
.word       ins_sch0bsd
.word       ins_sch1bsu
.word       ins_sch1bsd
.word       ins_err
.word       ins_err
.word       ins_err
.word       ins_err

.word       ins_orbsu
.word       ins_andbsu
.word       ins_xorbsu
.word       ins_movbsu
.word       ins_ornbsu
.word       ins_andnbsu
.word       ins_xornbsu
.word       ins_notbsu

.word       __aeabi_idivmod
.word       __aeabi_uidivmod
.word       ins_rev
.word       drc_clearScreenForGolf
.word       baseball2_scaling
.word       baseball2_sort
