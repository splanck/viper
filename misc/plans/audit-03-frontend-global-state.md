# Audit Finding #3: Frontend Global Mutable State

## Problem
Three static/singleton instances break concurrent compilation:
1. `static int lambdaCounter` — `Lowerer_Expr_Lambda.cpp:40`
2. `static TypeRegistry reg` — `TypeRegistry.cpp:111`
3. `static RuntimePropertyIndex idx` — `RuntimePropertyIndex.cpp:62`

## Implementation Plan (2-3 hours)

### Fix A: Lambda Counter → Instance Field
1. Add `int lambdaCounter_{0};` to the Lowerer class declaration in `Lowerer.hpp`
2. Change `Lowerer_Expr_Lambda.cpp:40` from `static int lambdaCounter = 0;` to `lambdaCounter_++`
3. Each Lowerer instance gets its own counter — no cross-compilation leakage

### Fix B: TypeRegistry → Per-Context
1. Remove the `static TypeRegistry reg;` singleton in `TypeRegistry.cpp:111`
2. Add `TypeRegistry typeRegistry_;` as a member of the BASIC compiler's top-level context (likely `SemanticAnalyzer` or `Compiler` class)
3. Change `runtimeTypeRegistry()` to take a reference parameter: `TypeRegistry &runtimeTypeRegistry(SemanticAnalyzer &sa)`
4. Update all call sites (grep for `runtimeTypeRegistry()`) to pass context

### Fix C: RuntimePropertyIndex → Per-Context
1. Same pattern as Fix B
2. Remove `static RuntimePropertyIndex idx;` in `RuntimePropertyIndex.cpp:62`
3. Add as member of the BASIC compiler context
4. Update `runtimePropertyIndex()` to take reference parameter
5. Update all call sites

### Files to Modify
- `src/frontends/zia/Lowerer.hpp` — add lambdaCounter_ field
- `src/frontends/zia/Lowerer_Expr_Lambda.cpp:40` — use instance field
- `src/frontends/basic/sem/TypeRegistry.cpp:111` — remove singleton
- `src/frontends/basic/sem/TypeRegistry.hpp` — change function signature
- `src/frontends/basic/sem/RuntimePropertyIndex.cpp:62` — remove singleton
- `src/frontends/basic/sem/RuntimePropertyIndex.hpp` — change function signature
- All call sites for `runtimeTypeRegistry()` and `runtimePropertyIndex()`

### Verification
1. `./scripts/build_viper.sh` — all tests pass
2. Write test: compile two Zia files in same process, verify unique lambda names
3. Grep for `static.*Registry\|static.*Index\|static int.*counter` in frontends to catch any others
