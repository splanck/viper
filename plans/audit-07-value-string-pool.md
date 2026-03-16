# Audit Finding #7: Value and MOperand String Pool

## Problem
IL `Value` (48 bytes) and MIR `MOperand` carry inline `std::string` even for non-string variants. 24 bytes wasted per instance.

## Implementation Plan (2-3 days)

### Phase 1: IL Value String Pool (1-2 days)

1. Create `src/il/core/StringPool.hpp`:
   ```cpp
   class StringPool {
       std::vector<std::string> strings_;
       std::unordered_map<std::string_view, uint32_t> index_;
   public:
       uint32_t intern(std::string s);
       const std::string &get(uint32_t id) const;
       static constexpr uint32_t kNone = UINT32_MAX;
   };
   ```

2. Modify `Value` struct (`Value.hpp:33-67`):
   ```cpp
   struct Value {
       Kind kind{Kind::NullPtr};
       union {
           long long i64;
           double f64;
           unsigned id;
       };
       uint32_t strIndex{StringPool::kNone};  // replaces std::string str
       bool isBool{false};
   };
   // New size: ~24 bytes (down from 48)
   ```

3. Add `StringPool *pool` parameter to `Value::constStr()` and `Value::global()` factory methods.

4. Update all Value consumers to resolve strings via pool:
   - `toString(const Value &v, const StringPool &pool)`
   - `ILWriter` / `ILReader` — serialize/deserialize pool
   - All analysis passes that inspect `v.str`

5. Store StringPool in `Module` (each module owns its string pool).

### Phase 2: MIR MOperand String Optimization (1 day)

MOperand's `std::string label` is simpler — labels are short, infrequent, and don't need interning.

Option A (minimal): Replace `std::string label` with `const char *label{nullptr}` pointing into a per-function arena. Labels are allocated once during lowering and never modified.

Option B (complete): Same StringPool pattern as Value.

Recommend Option A for MOperand — simpler, sufficient for MIR lifetime.

### Files to Modify
- `src/il/core/StringPool.hpp` (new)
- `src/il/core/Value.hpp` — replace str with strIndex
- `src/il/core/Value.cpp` — update factory methods
- `src/il/core/Module.hpp` — add StringPool member
- `src/il/io/ILWriter.cpp` — serialize pool
- `src/il/io/ILReader.cpp` — deserialize pool
- `src/il/verify/FunctionVerifier.cpp` — resolve strings via pool
- All analysis/transform passes that access `v.str`
- `src/codegen/aarch64/MachineIR.hpp` — MOperand label optimization

### Verification
1. `./scripts/build_viper.sh` — all tests pass
2. Add `static_assert(sizeof(Value) <= 24)` to Value.hpp
3. Benchmark: compile large IL module, compare RSS before/after
4. Golden tests: IL roundtrip (parse → print → parse) produces identical output
