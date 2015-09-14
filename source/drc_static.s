.arm
.align 4

.extern __divsi3, __modsi3, __udivsi3, __umodsi3, mem_rbyte, mem_rhword, mem_rword, mem_wbyte, mem_whword, mem_wword

.text

@ A cheap relocation table
.globl drc_relocTable
drc_relocTable:
    b       __divsi3
    b       __modsi3
    b       __udivsi3
    b       __umodsi3
    b       mem_rbyte
    b       mem_rhword
    b       mem_rword
    b       mem_wbyte
    b       mem_whword
    b       mem_wword
