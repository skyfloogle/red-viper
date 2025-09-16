.arm
.align 4

.extern __divsi3, __modsi3, __udivsi3, __umodsi3, mem_rbyte, mem_rhword, mem_rword, mem_wbyte, mem_whword, mem_wword, ins_err, ins_rev, drc_clearScreenForGolf

.text
@ A cheap relocation table
.globl drc_relocTable
drc_relocTable:
.word       __aeabi_idivmod
.word       __aeabi_uidivmod
.word       mem_rbyte
.word       mem_rhword
.word       mem_rword
.word       mem_wbyte
.word       mem_whword
.word       mem_wword
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
.word       ins_rev
.word       drc_clearScreenForGolf
