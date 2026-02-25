-- inline_stress.lua -- Inlining stress test (500K iterations).
-- Equivalent to examples/il/benchmarks/inline_stress.il

local function double(x)
    return x + x
end

local function square(x)
    return x * x
end

local function add3(a, b, c)
    return a + b + c
end

local function inc(x)
    return x + 1
end

local function combine(x)
    return add3(double(x), square(x), inc(x))
end

local sum = 0
for i = 0, 499999 do
    local r = combine(i)
    local raw_sum = sum + r
    sum = raw_sum & 268435455
end

os.exit(sum & 0xFF)
