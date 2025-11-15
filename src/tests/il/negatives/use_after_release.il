il 0.1.2
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_release(ptr) -> void

func @main() -> i64 {
entry:
  %slot = alloca 8
  store ptr, %slot, null
  %handle = load ptr, %slot
  call @rt_arr_i32_release(%handle)
  %len = call @rt_arr_i32_len(%handle)
  ret %len
}
