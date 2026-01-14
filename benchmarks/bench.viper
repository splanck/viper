module Benchmark;

// Fibonacci benchmark - compute fib(35)
func fib(n: Integer) -> Integer {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

func start() {
    var result = fib(45);
    print("fib(45) = " + result.toString());
}
