il 0.2.0

func @fib(i64 %n) -> i64 {
entry(%n:i64):
  %cmp = scmp_le %n, 1
  cbr %cmp, base(%n), recurse(%n)
base(%n1:i64):
  ret %n1
recurse(%n2:i64):
  %nm1 = isub.ovf %n2, 1
  %r1 = call @fib(%nm1)
  %nm2 = isub.ovf %n2, 2
  %r2 = call @fib(%nm2)
  %sum = iadd.ovf %r1, %r2
  ret %sum
}

func @main() -> i64 {
entry:
  %result = call @fib(40)
  ret %result
}
