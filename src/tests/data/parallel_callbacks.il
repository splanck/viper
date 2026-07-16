il 0.3.0
extern @Viper.Threads.Pool.Submit(ptr, ptr, ptr) -> i1
extern @Viper.Threads.Pool.New(i64) -> ptr
extern @Viper.Threads.Pool.Shutdown(ptr) -> void
extern @Viper.Threads.Parallel.DefaultPool() -> ptr
extern @Viper.Threads.Parallel.For(i64, i64, ptr) -> void
extern @Viper.Terminal.Say(str) -> void
extern @Viper.Terminal.SayInt(i64) -> void
extern @Viper.Threads.Parallel.ForEach(ptr, ptr) -> void
extern @Viper.Threads.Parallel.Map(ptr, ptr) -> ptr
extern @Viper.Threads.Parallel.Reduce(ptr, ptr, ptr) -> ptr
extern @Viper.Collections.Seq.New() -> ptr
extern @Viper.Collections.Seq.Push(ptr, ptr) -> void
extern @Viper.Collections.Seq.get_Count(ptr) -> i64
extern @Viper.Core.Box.I64(i64) -> ptr
extern @Viper.Core.Box.ToI64(ptr) -> i64
global const str @.Lw = "w"
global const str @.Lt = "task"
global const str @.Ld = "done"
global const str @.Lr = "rejected"
global const str @.Le = "fe"
func @worker(i64 %t0) -> void {
entry_0(%t1:i64):
  %s = const_str @.Lw
  call @Viper.Terminal.Say(%s)
  ret
}
func @task(ptr %t0) -> void {
entry_0(%t1:ptr):
  %s = const_str @.Lt
  call @Viper.Terminal.Say(%s)
  ret
}
func @each(ptr %t0) -> void {
entry_0(%t1:ptr):
  %s = const_str @.Le
  call @Viper.Terminal.Say(%s)
  ret
}
func @double(ptr %t0) -> ptr {
entry_0(%t1:ptr):
  %v:i64 = call @Viper.Core.Box.ToI64(%t1)
  %d:i64 = iadd.ovf %v, %v
  %b:ptr = call @Viper.Core.Box.I64(%d)
  ret %b
}
func @sum(ptr %t0, ptr %t1) -> ptr {
entry_0(%a:ptr, %b:ptr):
  %av:i64 = call @Viper.Core.Box.ToI64(%a)
  %bv:i64 = call @Viper.Core.Box.ToI64(%b)
  %s:i64 = iadd.ovf %av, %bv
  %r:ptr = call @Viper.Core.Box.I64(%s)
  ret %r
}
func @main() -> void {
entry_0:
  call @Viper.Threads.Parallel.For(0, 3, @worker)
  %pool:ptr = call @Viper.Threads.Parallel.DefaultPool()
  %ok:i1 = call @Viper.Threads.Pool.Submit(%pool, @task, null)
  %dead:ptr = call @Viper.Threads.Pool.New(1)
  call @Viper.Threads.Pool.Shutdown(%dead)
  %ok2:i1 = call @Viper.Threads.Pool.Submit(%dead, @task, null)
  cbr %ok2, was_accepted, was_rejected
was_rejected:
  %rj = const_str @.Lr
  call @Viper.Terminal.Say(%rj)
  br finish
was_accepted:
  br finish
finish:
  %seq:ptr = call @Viper.Collections.Seq.New()
  %one:ptr = call @Viper.Core.Box.I64(1)
  call @Viper.Collections.Seq.Push(%seq, %one)
  %two:ptr = call @Viper.Core.Box.I64(2)
  call @Viper.Collections.Seq.Push(%seq, %two)
  call @Viper.Threads.Parallel.ForEach(%seq, @each)
  %mapped:ptr = call @Viper.Threads.Parallel.Map(%seq, @double)
  %mcount:i64 = call @Viper.Collections.Seq.get_Count(%mapped)
  call @Viper.Terminal.SayInt(%mcount)
  %zero:ptr = call @Viper.Core.Box.I64(0)
  %total:ptr = call @Viper.Threads.Parallel.Reduce(%seq, @sum, %zero)
  %tv:i64 = call @Viper.Core.Box.ToI64(%total)
  call @Viper.Terminal.SayInt(%tv)
  %d = const_str @.Ld
  call @Viper.Terminal.Say(%d)
  ret
}
