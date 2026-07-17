il 0.3.0
extern @Zanna.Threads.Pool.Submit(ptr, ptr, ptr) -> i1
extern @Zanna.Threads.Pool.New(i64) -> ptr
extern @Zanna.Threads.Pool.Shutdown(ptr) -> void
extern @Zanna.Threads.Parallel.DefaultPool() -> ptr
extern @Zanna.Threads.Parallel.For(i64, i64, ptr) -> void
extern @Zanna.Terminal.Say(str) -> void
extern @Zanna.Terminal.SayInt(i64) -> void
extern @Zanna.Threads.Parallel.ForEach(ptr, ptr) -> void
extern @Zanna.Threads.Parallel.Map(ptr, ptr) -> ptr
extern @Zanna.Threads.Parallel.Reduce(ptr, ptr, ptr) -> ptr
extern @Zanna.Collections.Seq.New() -> ptr
extern @Zanna.Collections.Seq.Push(ptr, ptr) -> void
extern @Zanna.Collections.Seq.get_Count(ptr) -> i64
extern @Zanna.Core.Box.I64(i64) -> ptr
extern @Zanna.Core.Box.ToI64(ptr) -> i64
global const str @.Lw = "w"
global const str @.Lt = "task"
global const str @.Ld = "done"
global const str @.Lr = "rejected"
global const str @.Le = "fe"
func @worker(i64 %t0) -> void {
entry_0(%t1:i64):
  %s = const_str @.Lw
  call @Zanna.Terminal.Say(%s)
  ret
}
func @task(ptr %t0) -> void {
entry_0(%t1:ptr):
  %s = const_str @.Lt
  call @Zanna.Terminal.Say(%s)
  ret
}
func @each(ptr %t0) -> void {
entry_0(%t1:ptr):
  %s = const_str @.Le
  call @Zanna.Terminal.Say(%s)
  ret
}
func @double(ptr %t0) -> ptr {
entry_0(%t1:ptr):
  %v:i64 = call @Zanna.Core.Box.ToI64(%t1)
  %d:i64 = iadd.ovf %v, %v
  %b:ptr = call @Zanna.Core.Box.I64(%d)
  ret %b
}
func @sum(ptr %t0, ptr %t1) -> ptr {
entry_0(%a:ptr, %b:ptr):
  %av:i64 = call @Zanna.Core.Box.ToI64(%a)
  %bv:i64 = call @Zanna.Core.Box.ToI64(%b)
  %s:i64 = iadd.ovf %av, %bv
  %r:ptr = call @Zanna.Core.Box.I64(%s)
  ret %r
}
func @main() -> void {
entry_0:
  call @Zanna.Threads.Parallel.For(0, 3, @worker)
  %pool:ptr = call @Zanna.Threads.Parallel.DefaultPool()
  %ok:i1 = call @Zanna.Threads.Pool.Submit(%pool, @task, null)
  %dead:ptr = call @Zanna.Threads.Pool.New(1)
  call @Zanna.Threads.Pool.Shutdown(%dead)
  %ok2:i1 = call @Zanna.Threads.Pool.Submit(%dead, @task, null)
  cbr %ok2, was_accepted, was_rejected
was_rejected:
  %rj = const_str @.Lr
  call @Zanna.Terminal.Say(%rj)
  br finish
was_accepted:
  br finish
finish:
  %seq:ptr = call @Zanna.Collections.Seq.New()
  %one:ptr = call @Zanna.Core.Box.I64(1)
  call @Zanna.Collections.Seq.Push(%seq, %one)
  %two:ptr = call @Zanna.Core.Box.I64(2)
  call @Zanna.Collections.Seq.Push(%seq, %two)
  call @Zanna.Threads.Parallel.ForEach(%seq, @each)
  %mapped:ptr = call @Zanna.Threads.Parallel.Map(%seq, @double)
  %mcount:i64 = call @Zanna.Collections.Seq.get_Count(%mapped)
  call @Zanna.Terminal.SayInt(%mcount)
  %zero:ptr = call @Zanna.Core.Box.I64(0)
  %total:ptr = call @Zanna.Threads.Parallel.Reduce(%seq, @sum, %zero)
  %tv:i64 = call @Zanna.Core.Box.ToI64(%total)
  call @Zanna.Terminal.SayInt(%tv)
  %d = const_str @.Ld
  call @Zanna.Terminal.Say(%d)
  ret
}
