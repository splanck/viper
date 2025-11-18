.text
.align 2
.globl id0
id0:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  ldp x29, x30, [sp], #16
  ret

