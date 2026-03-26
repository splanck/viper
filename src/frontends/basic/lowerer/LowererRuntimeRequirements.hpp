//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lowerer/LowererRuntimeRequirements.hpp
//
//===----------------------------------------------------------------------===//
// =========================================================================
// File: frontends/basic/lowerer/LowererRuntimeRequirements.hpp
// Purpose: Runtime requirement declarations for the BASIC Lowerer.
//          Each require*() method ensures that the corresponding runtime
//          helper function is declared and available in the emitted module.
// Note: This file is #include'd inside the Lowerer class body.
//       It must NOT have #pragma once, include guards, or namespace blocks.
// =========================================================================

// ═══ Runtime Requirement Declarations ═══

void requireTrap();
void requireArrayI32New();
void requireArrayI32Resize();
void requireArrayI32Len();
void requireArrayI32Get();
void requireArrayI32Set();
void requireArrayI32Retain();
void requireArrayI32Release();
void requireArrayOobPanic();
// I64 array helpers (for LONG arrays)
void requireArrayI64New();
void requireArrayI64Resize();
void requireArrayI64Len();
void requireArrayI64Get();
void requireArrayI64Set();
void requireArrayI64Retain();
void requireArrayI64Release();
// F64 array helpers (for SINGLE/DOUBLE arrays)
void requireArrayF64New();
void requireArrayF64Resize();
void requireArrayF64Len();
void requireArrayF64Get();
void requireArrayF64Set();
void requireArrayF64Retain();
void requireArrayF64Release();

void requireArrayStrAlloc();
void requireArrayStrRelease();
void requireArrayStrGet();
void requireArrayStrPut();
void requireArrayStrLen();
// Object array helpers
void requireArrayObjNew();
void requireArrayObjLen();
void requireArrayObjGet();
void requireArrayObjPut();
void requireArrayObjResize();
void requireArrayObjRelease();
void requireOpenErrVstr();
void requireCloseErr();
void requireSeekChErr();
void requireWriteChErr();
void requirePrintlnChErr();
void requireLineInputChErr();
// --- begin: require declarations ---
void requireEofCh();
void requireLofCh();
void requireLocCh();
// Module-level globals helpers
void requireModvarAddrI64();
void requireModvarAddrF64();
void requireModvarAddrI1();
void requireModvarAddrPtr();
void requireModvarAddrStr();
// --- end: require declarations ---
void requireStrRetainMaybe();
void requireStrReleaseMaybe();
void requireSleepMs();
void requireTimerMs();
