//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/classes/RuntimeClasses.inc
// Purpose: Declarative single source of truth for runtime classes. This file
//          is macro-expanded by RuntimeClasses.cpp to populate descriptor
//          vectors used by frontends and other IL consumers.
// Notes:   Keep entries canonical as Viper.*. Receiver is arg0. Avoid rt_*
//          naming in this catalog; bridge helpers should map at integration.
//          This is intentionally macro-based for fast iteration; can be
//          replaced by a YAML-driven generator later without changing users.
// Links:   docs/il-guide.md#reference, docs/codemap.md

RUNTIME_CLASS("Viper.String",
              RTCLS_String,
              "opaque*",
              "Viper.Strings.FromStr", /* ctor helper; keep NULL if gating NEW later */
              RUNTIME_PROPS(RUNTIME_PROP("Length", "i64", "Viper.String.get_Length", nullptr),
                            RUNTIME_PROP("IsEmpty", "i1", "Viper.String.get_IsEmpty", nullptr)),
              RUNTIME_METHODS(RUNTIME_METHOD("Substring", "str(i64,i64)", "Viper.String.Substring"),
                              RUNTIME_METHOD("Concat", "str(str)", "Viper.String.Concat")))

// Viper.Text.StringBuilder — catalog + runtime shims
// Notes:
// - This class demonstrates catalog-driven OOP integration.
// - Members are wired to canonical Viper.Text.* externs with handlers
//   implemented in the runtime (see rt_text_sb_* in rt_string_builder.c).
RUNTIME_CLASS(
    "Viper.Text.StringBuilder",
    RTCLS_StringBuilder,
    "opaque*",
    "Viper.Text.StringBuilder.New", /* ctor; bridges to rt_ns_stringbuilder_new */
    RUNTIME_PROPS(
        RUNTIME_PROP("Length", "i64", "Viper.Text.StringBuilder.get_Length", nullptr),
        RUNTIME_PROP("Capacity", "i64", "Viper.Text.StringBuilder.get_Capacity", nullptr)),
    RUNTIME_METHODS(
        /* Returns self to enable chaining */
        RUNTIME_METHOD("Append", "obj(str)", "Viper.Text.StringBuilder.Append"),
        RUNTIME_METHOD("ToString", "str()", "Viper.Text.StringBuilder.ToString"),
        RUNTIME_METHOD("Clear", "void()", "Viper.Text.StringBuilder.Clear")))

// Viper.System.String removed - use Viper.String instead

// Viper.System.Text.StringBuilder removed - use Viper.Text.StringBuilder instead

// Canonical base object class for all Viper types
RUNTIME_CLASS(
    "Viper.Object",
    RTCLS_Object,
    "obj",
    "", /* no ctor; base class only */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Equals", "i1(obj)", "Viper.Object.Equals"),
        RUNTIME_METHOD("GetHashCode", "i64()", "Viper.Object.GetHashCode"),
        RUNTIME_METHOD("ToString", "str()", "Viper.Object.ToString"),
        RUNTIME_METHOD("ReferenceEquals", "i1(obj,obj)", "Viper.Object.ReferenceEquals")))

// Viper.System.Object removed - use Viper.Object instead

// Canonical file utility class
RUNTIME_CLASS(
    "Viper.IO.File",
    RTCLS_File,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Exists", "i1(str)", "Viper.IO.File.Exists"),
        RUNTIME_METHOD("ReadAllText", "str(str)", "Viper.IO.File.ReadAllText"),
        RUNTIME_METHOD("WriteAllText", "void(str,str)", "Viper.IO.File.WriteAllText"),
        RUNTIME_METHOD("Delete", "void(str)", "Viper.IO.File.Delete")))

// Viper.System.IO.File removed - use Viper.IO.File instead

// Canonical non-generic list under Viper.Collections
RUNTIME_CLASS(
    "Viper.Collections.List",
    RTCLS_List,
    "obj",
    "Viper.Collections.List.New",
    RUNTIME_PROPS(RUNTIME_PROP("Count", "i64", "Viper.Collections.List.get_Count", nullptr)),
    RUNTIME_METHODS(RUNTIME_METHOD("Add", "void(obj)", "Viper.Collections.List.Add"),
                    RUNTIME_METHOD("Clear", "void()", "Viper.Collections.List.Clear"),
                    RUNTIME_METHOD("RemoveAt", "void(i64)", "Viper.Collections.List.RemoveAt"),
                    RUNTIME_METHOD("get_Item", "obj(i64)", "Viper.Collections.List.get_Item"),
                    RUNTIME_METHOD("set_Item", "void(i64,obj)", "Viper.Collections.List.set_Item")))

// Viper.System.Collections.List removed - use Viper.Collections.List instead

// Canonical math utility class with common mathematical functions
RUNTIME_CLASS(
    "Viper.Math",
    RTCLS_Math,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Abs", "f64(f64)", "Viper.Math.Abs"),
        RUNTIME_METHOD("Sqrt", "f64(f64)", "Viper.Math.Sqrt"),
        RUNTIME_METHOD("Sin", "f64(f64)", "Viper.Math.Sin"),
        RUNTIME_METHOD("Cos", "f64(f64)", "Viper.Math.Cos"),
        RUNTIME_METHOD("Tan", "f64(f64)", "Viper.Math.Tan"),
        RUNTIME_METHOD("Floor", "f64(f64)", "Viper.Math.Floor"),
        RUNTIME_METHOD("Ceil", "f64(f64)", "Viper.Math.Ceil"),
        RUNTIME_METHOD("Pow", "f64(f64,f64)", "Viper.Math.Pow"),
        RUNTIME_METHOD("Log", "f64(f64)", "Viper.Math.Log"),
        RUNTIME_METHOD("Exp", "f64(f64)", "Viper.Math.Exp")))

// Canonical console I/O utility class
RUNTIME_CLASS(
    "Viper.Console",
    RTCLS_Console,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("WriteLine", "void(str)", "Viper.Console.PrintStr"),
        RUNTIME_METHOD("ReadLine", "str()", "Viper.Console.ReadLine")))

// Canonical type conversion utility class
RUNTIME_CLASS(
    "Viper.Convert",
    RTCLS_Convert,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("ToInt64", "i64(str)", "Viper.Convert.ToInt"),
        RUNTIME_METHOD("ToDouble", "f64(str)", "Viper.Convert.ToDouble"),
        RUNTIME_METHOD("ToString_Int", "str(i64)", "Viper.Strings.FromInt"),
        RUNTIME_METHOD("ToString_Double", "str(f64)", "Viper.Strings.FromDouble")))
