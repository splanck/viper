-- call_stress.lua -- Function call overhead benchmark (100K iterations).
-- Equivalent to examples/il/benchmarks/call_stress.il

local function add_triple(a, b, c)
    return a + b + c
end

local function mul_pair(x, y)
    return x * y
end

local function compute(n)
    local a = n
    local b = n + 1
    local c = n + 2
    local s = add_triple(a, b, c)
    return mul_pair(s, 3)
end

local sum = 0
for i = 0, 99999 do
    local r1 = compute(i)
    local r2 = add_triple(i, r1, 1)
    local r3 = mul_pair(r2, 2)
    sum = sum + r3
end

os.exit(sum & 0xFF)
