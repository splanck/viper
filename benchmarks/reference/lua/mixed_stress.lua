-- mixed_stress.lua -- Mixed workload benchmark (100K iterations).
-- Equivalent to examples/il/benchmarks/mixed_stress.il

local function helper(x)
    return x * 3 + 7
end

local sum = 0
for i = 0, 99999 do
    local t1 = i + 1
    local t2 = t1 * 2
    local t3 = t2 - i
    local tmp
    if i % 4 == 0 then
        tmp = helper(t3) * 2
    else
        tmp = (t3 + 100) * 3
    end
    if i % 7 == 0 then
        tmp = tmp + helper(tmp)
    end
    sum = sum + tmp
end

os.exit(sum & 0xFF)
