il 0.3.0

func @main() -> ptr {
entry:
  %stack = alloca 8
  %slot = alloca 8
  store ptr, %slot, %stack
  %loaded = load ptr, %slot
  ret %loaded
}
