# CODEMAP: IL Runtime

Runtime signature metadata (`src/il/runtime/`) for C ABI helpers.

## Signature Registry

| File                             | Purpose                                                  |
|----------------------------------|----------------------------------------------------------|
| `RuntimeSignatures.hpp/cpp`      | Main registry mapping helper names to IL signatures      |
| `RuntimeSignatureParser.hpp/cpp` | Parse compact ABI signature spellings                    |
| `RuntimeSignaturesData.hpp`      | Static data bundles for signature catalog                |
| `HelperEffects.hpp`              | Effect tags for runtime helpers (side-effects, aliasing) |

## Runtime Classes (`classes/`)

| File                     | Purpose                                            |
|--------------------------|----------------------------------------------------|
| `RuntimeClasses.hpp/cpp` | Runtime class metadata (StringBuilder, List, etc.) |

## Signature Categories (`signatures/`)

| File                     | Purpose                                      |
|--------------------------|----------------------------------------------|
| `Registry.hpp/cpp`       | Registration entry points for category files |
| `Signatures_Arrays.cpp`  | Array helper signatures                      |
| `Signatures_FileIO.cpp`  | File I/O helper signatures                   |
| `Signatures_Math.cpp`    | Math function signatures                     |
| `Signatures_Strings.cpp` | String operation signatures                  |
