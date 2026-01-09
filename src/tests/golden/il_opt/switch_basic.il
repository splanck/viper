il 0.2.0

func @switch_default() -> i64 {
entry:
  switch.i32 5, ^fallback, 1 -> ^first, 3 -> ^last
first:
  ret 11
last:
  ret 33
fallback:
  ret 42
}

func @switch_first() -> i64 {
entry:
  switch.i32 1, ^fallback, 1 -> ^first, 3 -> ^last
first:
  ret 111
last:
  ret 333
fallback:
  ret 999
}

func @switch_last() -> i64 {
entry:
  switch.i32 3, ^fallback, 1 -> ^first, 3 -> ^last
first:
  ret 5
last:
  ret 55
fallback:
  ret 0
}

func @switch_duplicate() -> i64 {
entry:
  switch.i32 7, ^fallback, 7 -> ^dup_a, 7 -> ^dup_b
dup_a:
  ret 10
dup_b:
  ret 20
fallback:
  ret 30
}

func @switch_no_cases() -> i64 {
entry:
  switch.i32 12, ^fallback
fallback:
  ret 123
}
