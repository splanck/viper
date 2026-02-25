-- arith_stress.lua -- Arithmetic-heavy loop benchmark (500K iterations).
-- Equivalent to examples/il/benchmarks/arith_stress.il

local sum = 0
for i = 0, 499999 do
    local t1 = i + 1
    local t2 = t1 * 2
    local t3 = i + 3
    local t4 = t2 + t3
    local t5 = t4 * 5
    local t6 = t5 - i
    local t7 = t6 + 7
    local t8 = t7 * 3
    local t9 = t8 - 11
    sum = sum + t9
end

os.exit(sum & 0xFF)
