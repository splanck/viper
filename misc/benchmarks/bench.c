#include <stdio.h>

long fib(long n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    long result = fib(45);
    printf("fib(45) = %ld\n", result);
    return 0;
}
