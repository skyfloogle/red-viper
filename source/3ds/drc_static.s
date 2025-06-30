.arm
.align 4

.extern __divsi3, __modsi3, __udivsi3, __umodsi3, mem_rbyte, mem_rhword, mem_rword, mem_wbyte, mem_whword, mem_wword, ins_cmpf_s, ins_err, ins_cvt_ws, ins_cvt_sw, ins_addf_s, ins_subf_s, ins_mulf_s, ins_divf_s, ins_xb, ins_xh, ins_rev, ins_trnc_sw, ins_mpyhw, drc_clearScreenForGolf

.text
@ A cheap relocation table
.globl drc_relocTable
drc_relocTable:
    b       __aeabi_idivmod
    b       __aeabi_uidivmod
    b       mem_rbyte
    b       mem_rhword
    b       mem_rword
    b       mem_wbyte
    b       mem_whword
    b       mem_wword
    b       ins_cmpf_s
    b       ins_err
    b       ins_cvt_ws
    b       ins_cvt_sw
    b       ins_addf_s
    b       ins_subf_s
    b       ins_mulf_s
    b       ins_divf_s
    b       ins_xb
    b       ins_xh
    b       ins_rev
    b       ins_trnc_sw
    b       ins_mpyhw
    b       ins_err
    b       ins_err
    b       ins_err
    b       ins_sch0bsu
    b       ins_sch0bsd
    b       ins_sch1bsu
    b       ins_sch1bsd
    b       ins_err
    b       ins_err
    b       ins_err
    b       ins_err
    b       ins_orbsu
    b       ins_andbsu
    b       ins_xorbsu
    b       ins_movbsu
    b       ins_ornbsu
    b       ins_andnbsu
    b       ins_xornbsu
    b       ins_notbsu
    b       drc_clearScreenForGolf