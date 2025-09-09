# Getting Started: Build, Run, and Your First 3 BASIC Programs

This guide shows you how to build the project, run the interpreter, and write three tiny BASIC programs.

---

## 1) Prerequisites

- **Clang** (preferred) and **CMake**
  - macOS: Apple Clang is preinstalled.
  - Ubuntu: `sudo apt-get update && sudo apt-get install -y clang cmake`
  - If Clang is missing, codegen smoke tests are skipped automatically.

---

## 2) Build the tools

```bash
CC=clang CXX=clang++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

This builds:
ilc — compile/run driver (BASIC → IL → VM)
il-verify — IL verifier

## 3) Program #1 — Hello

Create hello.bas:
10 PRINT "HELLO"
20 END
Run:
./build/src/tools/ilc/ilc front basic hello.bas -emit-il
./build/src/tools/ilc/ilc front basic hello.bas -run
Expected output:
HELLO

## 4) Program #2 — Sum 1..10 (loop)

Create sum10.bas:
10 PRINT "SUM 1..10"
20 LET I = 1
30 LET S = 0
40 WHILE I <= 10
50 LET S = S + I
60 LET I = I + 1
70 WEND
80 PRINT S
90 END
Run:
./build/src/tools/ilc/ilc front basic sum10.bas -run
Expected output:
SUM 1..10
45

## 5) Program #3 — Branching (IF/ELSE)

Create branch.bas:
10 LET X = 2
20 IF X = 1 THEN PRINT "ONE" ELSE PRINT "NOT ONE"
30 END
Run:
./build/src/tools/ilc/ilc front basic branch.bas -run
Expected output:
NOT ONE

## 6) Tips & Troubleshooting

Verify IL: for .il files, use ./build/src/tools/il-verify/il-verify path/to/file.il.
Paths: if your build tree differs, adjust the ./build/src/tools/... paths accordingly.
Docs: BASIC reference → /docs/references/basic.md, IL spec → /docs/references/il.md.
