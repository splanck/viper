il 0.1.2

extern @Viper.String.Concat(str, str) -> str
extern @Viper.String.get_Length(str) -> i64

global const str @.hello = "Hello"
global const str @.world = "World"
global const str @.space = " "
global const str @.bang = "!"

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 50000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %s1 = const_str @.hello
  %s2 = const_str @.space
  %t1 = call @Viper.String.Concat(%s1, %s2)

  %s3 = const_str @.world
  %t2 = call @Viper.String.Concat(%t1, %s3)

  %s4 = const_str @.bang
  %t3 = call @Viper.String.Concat(%t2, %s4)

  %len = call @Viper.String.get_Length(%t3)
  %new_sum = iadd.ovf %sum0, %len
  %next_i = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
