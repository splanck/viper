# CODEMAP: IL Runtime

Runtime signature metadata (`src/il/runtime/`) for C ABI helpers.

Last updated: 2026-01-15

## Overview

- **Total source files**: 19 (.hpp/.cpp)
- **Subdirectories**: classes/, signatures/

## Signature Registry

| File                            | Purpose                                                  |
|---------------------------------|----------------------------------------------------------|
| `RuntimeSignatures.cpp`         | Main registry implementation                             |
| `RuntimeSignatures.hpp`         | Main registry mapping helper names to IL signatures      |
| `RuntimeSignatureParser.cpp`    | Parse compact ABI signature spellings impl               |
| `RuntimeSignatureParser.hpp`    | Parse compact ABI signature spellings                    |
| `RuntimeSignaturesData.hpp`     | Static data bundles for signature catalog                |
| `RuntimeSignatures_Handlers.cpp`| Runtime signature handler implementations                |
| `RuntimeSignatures_Handlers.hpp`| Runtime signature handler declarations                   |
| `HelperEffects.hpp`             | Effect tags for runtime helpers (side-effects, aliasing) |
| `RuntimeClassNames.hpp`         | Canonical runtime class name constants                   |
| `RuntimeNameMap.hpp`            | Name to runtime function mapping                         |

## Runtime Classes (`classes/`)

| File                        | Purpose                                            |
|-----------------------------|----------------------------------------------------|
| `classes/RuntimeClasses.cpp`| Runtime class metadata implementation              |
| `classes/RuntimeClasses.hpp`| Runtime class metadata (StringBuilder, List, etc.) |

## Signature Categories (`signatures/`)

| File                            | Purpose                                      |
|---------------------------------|----------------------------------------------|
| `signatures/Registry.cpp`       | Registration entry points implementation     |
| `signatures/Registry.hpp`       | Registration entry points for category files |
| `signatures/Signatures_Arrays.cpp` | Array helper signatures                   |
| `signatures/Signatures_FileIO.cpp` | File I/O helper signatures                |
| `signatures/Signatures_Math.cpp`   | Math function signatures                  |
| `signatures/Signatures_Strings.cpp`| String operation signatures               |
| `signatures/Signatures_OOP.cpp`    | OOP runtime helper signatures             |
