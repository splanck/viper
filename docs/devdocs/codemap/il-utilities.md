# CODEMAP: IL Utilities

Shared IL helper functions (`src/il/utils/`).

Last updated: 2026-02-17

## Overview

- **Total source files**: 4 (.hpp/.cpp)

## Utilities

| File             | Purpose                                                 |
|------------------|---------------------------------------------------------|
| `UseDefInfo.cpp` | Use-def chain tracking implementation                   |
| `UseDefInfo.hpp` | Use-def chain tracking for efficient SSA value replacement |
| `Utils.cpp`      | IR queries implementation                                                                          |
| `Utils.hpp`      | IR queries: block membership, terminator classification, value replacement, next temp ID, block find |
