il 0.3.0
extern @Viper.Threads.Pool.Submit(ptr, ptr, ptr) -> i1
extern @Viper.Threads.Parallel.DefaultPool() -> ptr
extern @Viper.Threads.Parallel.For(i64, i64, ptr) -> void
extern @Viper.Terminal.Say(str) -> void
global const str @.Lw = "w"
global const str @.Lt = "task"
global const str @.Ld = "done"
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
func @main() -> void {
entry_0:
  call @Viper.Threads.Parallel.For(0, 3, @worker)
  %pool:ptr = call @Viper.Threads.Parallel.DefaultPool()
  %ok:i1 = call @Viper.Threads.Pool.Submit(%pool, @task, null)
  %d = const_str @.Ld
  call @Viper.Terminal.Say(%d)
  ret
}
