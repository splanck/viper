-- fib_stress.lua -- Recursive fibonacci(35) benchmark.
-- Equivalent to examples/il/benchmarks/fib_stress.il

local function fib(n)
    if n <= 1 then return n end
    return fib(n - 1) + fib(n - 2)
end

local result = fib(35)
os.exit(result & 0xFF)
