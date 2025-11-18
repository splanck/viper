.text
.align 2
.globl g
g:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  mov x0, x3
  sub x0, x0, #7
  ldp x29, x30, [sp], #16
  ret

