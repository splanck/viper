il 0.1.2

func @narrow_cast(%f:f64) -> i64 {
entry(%f:f64):
  %t0 = fptosi %f
  ret %t0
}
