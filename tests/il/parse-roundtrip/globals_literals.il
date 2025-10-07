il 0.1.2

global const str @.msg = "hello"
global const i32 @.int = 123
global const f64 @.flt = 3.5
global const ptr @.null = null
global const ptr @.sym = @.msg

func @main() -> i32 {
entry:
  ret 0
}
