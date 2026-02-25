-- udiv_stress.lua -- Unsigned division stress test (500K iterations, i from 1 to 500000).
-- Equivalent to examples/il/benchmarks/udiv_stress.il

local sum = 0
for i = 1, 500000 do
    local d1 = i // 2
    local d2 = i // 4
    local d3 = i // 8
    local d4 = i // 16
    local d5 = i // 32
    local d6 = i // 64
    local d7 = i // 128
    local d8 = i // 256

    local s7 = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8

    local raw_sum = sum + s7
    sum = raw_sum & 268435455
end

os.exit(sum & 0xFF)
