.text
.align 2
.globl main
main:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  mov x0, #0
  ldp x29, x30, [sp], #16
  ret

