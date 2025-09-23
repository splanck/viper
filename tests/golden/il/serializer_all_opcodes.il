il 0.1.2
extern @do_work(i64, i64) -> i64
global const str @.Lstr = "ops"
func @all_ops() -> i64 {
entry:
  %t0 = add 1, 2
  %t1 = sub %t0, 3
  %t2 = mul %t0, %t1
  %t3 = sdiv %t2, 5
  %t4 = udiv 10, 2
  %t5 = srem 7, 3
  %t6 = urem 9, 4
  %t7 = and 240, 15
  %t8 = or %t7, 1
  %t9 = xor %t8, 3
  %t10 = shl %t9, 1
  %t11 = lshr %t10, 2
  %t12 = ashr -8, 1
  %t13 = fadd 1, 2.5
  %t14 = fsub %t13, 1.25
  %t15 = fmul %t14, 4
  %t16 = fdiv %t15, 2
  %t17 = icmp_eq 1, 1
  %t18 = icmp_ne 1, 0
  %t19 = scmp_lt -1, 0
  %t20 = scmp_le 0, 0
  %t21 = scmp_gt 2, 1
  %t22 = scmp_ge 2, 2
  %t23 = ucmp_lt 1, 2
  %t24 = ucmp_le 2, 2
  %t25 = ucmp_gt 3, 2
  %t26 = ucmp_ge 3, 3
  %t27 = fcmp_eq 1, 1
  %t28 = fcmp_ne 1, 2
  %t29 = fcmp_lt 1, 2
  %t30 = fcmp_le 2, 2
  %t31 = fcmp_gt 3, 2
  %t32 = fcmp_ge 3, 3
  %t33 = sitofp 42
  %t34 = fptosi 5.5
  %t35 = zext1 %t18
  %t36 = trunc1 255
  %t37 = alloca 8
  %t38 = gep %t37, 1
  store i64, %t37, 64
  %t39 = load i64, %t37
  %t40 = addr_of @.Lstr
  %t41 = const_str @.Lstr
  %t42 = const_null
  %t43 = call @do_work(%t40, 5)
  cbr %t18, compute(%t35, %t39), abort(%t42)
compute(%wide:i64, %loaded:i64):
  br join(%t43, %t35)
abort:
  trap
join(%lhs:i64, %rhs:i64):
  ret %t43
}
