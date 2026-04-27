il 0.2.0

extern @rt_modvar_addr_ptr(str) -> ptr
global const str @.slot = "slot"

func @main() -> void {
entry:
  %stack = alloca 8
  %name = const_str @.slot
  %global_slot = call @rt_modvar_addr_ptr(%name)
  store ptr, %global_slot, %stack
  ret
}
