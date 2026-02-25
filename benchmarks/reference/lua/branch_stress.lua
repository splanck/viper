-- branch_stress.lua -- Branch-heavy loop benchmark (200K iterations).
-- Equivalent to examples/il/benchmarks/branch_stress.il

local count = 0
for i = 0, 199999 do
    if i % 2 == 0 then count = count + 1 end
    if i % 3 == 0 then count = count + 2 end
    if i % 5 == 0 then count = count + 3 end
    if i % 7 == 0 then count = count + 5 end
end

os.exit(count & 0xFF)
