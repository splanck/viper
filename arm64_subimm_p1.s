.text
.align 2
.globl g
g:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  mov x0, x1
  sub x0, x0, #3
  ldp x29, x30, [sp], #16
  ret

