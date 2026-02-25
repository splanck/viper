-- redundant_stress.lua -- Redundant computation and constant propagation stress test (500K iterations).
-- Equivalent to examples/il/benchmarks/redundant_stress.il

local sum = 0
for i = 0, 499999 do
    -- Constant expressions (SCCP folds these in the IL optimizer).
    local k1 = 10 + 20     -- 30
    local k2 = k1 * 3      -- 90
    local k3 = k2 - 40     -- 50

    -- Redundant subexpressions: computed identically twice.
    local a1 = i + 7
    local a2 = a1 * 3

    local b1 = i + 7
    local b2 = b1 * 3

    -- More constant folding chains.
    local c1 = 100 + 200   -- 300
    local c2 = c1 * 2      -- 600
    local c3 = c2 - 100    -- 500

    -- Third constant chain.
    local d1 = 5 + 10      -- 15
    local d2 = d1 * 5      -- 75
    local d3 = d2 - 5      -- 70

    -- Live computation using redundant pair and constants.
    local live = a2 + b2 + k3 + c3 + d3

    local raw_sum = sum + live
    sum = raw_sum & 268435455
end

os.exit(sum & 0xFF)
