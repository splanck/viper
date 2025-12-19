//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/RuntimeNames.hpp
// Purpose: Centralized runtime function and type names for ViperLang.
// Key invariants: All Viper.* names are defined here to ensure consistency.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::viperlang::runtime {

//=============================================================================
// Terminal I/O Functions
//=============================================================================

inline constexpr const char *kTerminalSay = "Viper.Terminal.Say";
inline constexpr const char *kTerminalSayInt = "Viper.Terminal.SayInt";
inline constexpr const char *kTerminalSayNum = "Viper.Terminal.SayNum";
inline constexpr const char *kTerminalPrint = "Viper.Terminal.Print";
inline constexpr const char *kTerminalPrintLine = "Viper.Terminal.PrintLine";
inline constexpr const char *kTerminalReadLine = "Viper.Terminal.ReadLine";
inline constexpr const char *kTerminalReadKey = "Viper.Terminal.ReadKey";
inline constexpr const char *kTerminalClear = "Viper.Terminal.Clear";
inline constexpr const char *kTerminalSetPosition = "Viper.Terminal.SetPosition";
inline constexpr const char *kTerminalSetColor = "Viper.Terminal.SetColor";
inline constexpr const char *kTerminalGetWidth = "Viper.Terminal.GetWidth";
inline constexpr const char *kTerminalGetHeight = "Viper.Terminal.GetHeight";
inline constexpr const char *kTerminalHideCursor = "Viper.Terminal.HideCursor";
inline constexpr const char *kTerminalShowCursor = "Viper.Terminal.ShowCursor";
inline constexpr const char *kTerminalKeyAvailable = "Viper.Terminal.KeyAvailable";

//=============================================================================
// String Functions
//=============================================================================

inline constexpr const char *kStringConcat = "Viper.String.Concat";
inline constexpr const char *kStringEquals = "Viper.String.Equals";
inline constexpr const char *kStringLength = "Viper.String.Length";
inline constexpr const char *kStringSubstring = "Viper.String.Substring";
inline constexpr const char *kStringContains = "Viper.String.Contains";
inline constexpr const char *kStringStartsWith = "Viper.String.StartsWith";
inline constexpr const char *kStringEndsWith = "Viper.String.EndsWith";
inline constexpr const char *kStringIndexOf = "Viper.String.IndexOf";
inline constexpr const char *kStringToUpper = "Viper.String.ToUpper";
inline constexpr const char *kStringToLower = "Viper.String.ToLower";
inline constexpr const char *kStringTrim = "Viper.String.Trim";
inline constexpr const char *kStringSplit = "Viper.String.Split";
inline constexpr const char *kStringFromInt = "Viper.Strings.FromInt";
inline constexpr const char *kStringFromNum = "Viper.Strings.FromDouble";
inline constexpr const char *kStringCharAt = "Viper.String.CharAt";

//=============================================================================
// Math Functions
//=============================================================================

inline constexpr const char *kMathAbs = "Viper.Math.Abs";
inline constexpr const char *kMathSqrt = "Viper.Math.Sqrt";
inline constexpr const char *kMathPow = "Viper.Math.Pow";
inline constexpr const char *kMathSin = "Viper.Math.Sin";
inline constexpr const char *kMathCos = "Viper.Math.Cos";
inline constexpr const char *kMathTan = "Viper.Math.Tan";
inline constexpr const char *kMathFloor = "Viper.Math.Floor";
inline constexpr const char *kMathCeil = "Viper.Math.Ceil";
inline constexpr const char *kMathRound = "Viper.Math.Round";
inline constexpr const char *kMathMin = "Viper.Math.Min";
inline constexpr const char *kMathMax = "Viper.Math.Max";
inline constexpr const char *kMathRandom = "Viper.Math.Random";
inline constexpr const char *kMathRandomRange = "Viper.Math.RandomRange";

//=============================================================================
// Collection Functions
//=============================================================================

inline constexpr const char *kListNew = "Viper.Collections.List.New";
inline constexpr const char *kListAdd = "Viper.Collections.List.Add";
inline constexpr const char *kListGet = "Viper.Collections.List.get_Item";
inline constexpr const char *kListSet = "Viper.Collections.List.set_Item";
inline constexpr const char *kListCount = "Viper.Collections.List.get_Count";
inline constexpr const char *kListClear = "Viper.Collections.List.Clear";
inline constexpr const char *kListRemoveAt = "Viper.Collections.List.RemoveAt";
inline constexpr const char *kListContains = "Viper.Collections.List.Contains";

inline constexpr const char *kSetNew = "Viper.Collections.Set.New";
inline constexpr const char *kMapNew = "Viper.Collections.Map.New";

//=============================================================================
// Boxing/Unboxing Functions
//=============================================================================

inline constexpr const char *kBoxI64 = "Viper.Box.I64";
inline constexpr const char *kBoxF64 = "Viper.Box.F64";
inline constexpr const char *kBoxI1 = "Viper.Box.I1";
inline constexpr const char *kBoxStr = "Viper.Box.Str";

inline constexpr const char *kUnboxI64 = "Viper.Box.ToI64";
inline constexpr const char *kUnboxF64 = "Viper.Box.ToF64";
inline constexpr const char *kUnboxI1 = "Viper.Box.ToI1";
inline constexpr const char *kUnboxStr = "Viper.Box.ToStr";

//=============================================================================
// System Functions
//=============================================================================

inline constexpr const char *kSystemSleep = "Viper.System.Sleep";
inline constexpr const char *kSystemExit = "Viper.System.Exit";
inline constexpr const char *kSystemGetTime = "Viper.System.GetTime";

//=============================================================================
// Threading Functions
//=============================================================================

inline constexpr const char *kThreadSpawn = "Viper.Thread.Spawn";
inline constexpr const char *kThreadJoin = "Viper.Thread.Join";
inline constexpr const char *kThreadSleep = "Viper.Thread.Sleep";

//=============================================================================
// Runtime Allocator
//=============================================================================

inline constexpr const char *kRtAlloc = "rt_alloc";

//=============================================================================
// Configuration Constants
//=============================================================================

/// Maximum depth for import recursion to prevent stack overflow.
inline constexpr size_t kMaxImportDepth = 50;

/// Maximum number of imported files to prevent runaway compilation.
inline constexpr size_t kMaxImportedFiles = 100;

/// Object header size for entity types (vtable pointer).
inline constexpr size_t kObjectHeaderSize = 8;

} // namespace il::frontends::viperlang::runtime
