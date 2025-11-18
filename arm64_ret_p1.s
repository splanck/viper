.text
.align 2
.globl id1
id1:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  mov x0, x1
  ldp x29, x30, [sp], #16
  ret

