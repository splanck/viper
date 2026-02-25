-- string_stress.lua -- String manipulation benchmark (50K iterations).
-- Equivalent to examples/il/benchmarks/string_stress.il

local sum = 0
for i = 0, 49999 do
    local result = "Hello" .. " " .. "World" .. "!"
    sum = sum + #result
end

os.exit(sum & 0xFF)
