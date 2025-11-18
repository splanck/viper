.text
.align 2
.globl f
f:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  mov x0, x0
  add x0, x0, #5
  ldp x29, x30, [sp], #16
  ret

