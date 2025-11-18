.text
.align 2
.globl f
f:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  mov x9, x0
  mov x0, x2
  mov x1, x9
  add x0, x0, x1
  ldp x29, x30, [sp], #16
  ret

