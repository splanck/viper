.text
.align 2
.globl add2
add2:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  add x0, x0, x1
  ldp x29, x30, [sp], #16
  ret

