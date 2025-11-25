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
                              RUNTIME_METHOD("Concat", "str(str)", "Viper.String.ConcatSelf")))

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

// RC-B1: Add system-qualified aliases that map object members to procedural
// Viper.Strings.* externs. These entries coexist with the canonical
// Viper.String and Viper.Text.StringBuilder to preserve existing behaviour.
RUNTIME_CLASS(
    "Viper.System.String",
    RTCLS_String,
    "str", /* runtime string handle (opaque) */
    "",    /* ctor unused: strings created via literals/ops */
    RUNTIME_PROPS(RUNTIME_PROP("Length", "i64", "Viper.Strings.Len", nullptr),
                  RUNTIME_PROP("IsEmpty", "i1", "Viper.String.get_IsEmpty", nullptr)
                  /* Note: IsEmpty helper not present under Viper.Strings; omit for now. */
                  ),
    RUNTIME_METHODS(RUNTIME_METHOD("Substring", "str(i64,i64)", "Viper.Strings.Mid"),
                    RUNTIME_METHOD("Concat", "str(str)", "Viper.Strings.Concat")))

RUNTIME_CLASS("Viper.System.Text.StringBuilder",
              RTCLS_StringBuilder,
              "obj",                                 /* opaque builder handle */
              "Viper.System.Text.StringBuilder.New", /* canonical ctor helper present in registry */
              RUNTIME_PROPS(RUNTIME_PROP("Length", "i64", "Viper.Strings.Builder.Length", nullptr),
                            RUNTIME_PROP("Capacity",
                                         "i64",
                                         "Viper.Text.StringBuilder.get_Capacity",
                                         nullptr)),
              RUNTIME_METHODS(
                  /* Procedural builder methods are exposed under Viper.Text.* in this repo. */
                  RUNTIME_METHOD("Append", "obj(str)", "Viper.Text.StringBuilder.Append"),
                  RUNTIME_METHOD("ToString", "str()", "Viper.Text.StringBuilder.ToString"),
                  RUNTIME_METHOD("Clear", "void()", "Viper.Text.StringBuilder.Clear")))

// Minimal seed entries for additional System types. Methods/properties will be
// defined in future changes; the goal here is to make the types visible to the
// compiler and runtime catalogs.
RUNTIME_CLASS(
    "Viper.System.Object",
    RTCLS_Object,
    "obj",
    "", /* no ctor; base class only */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Equals", "i1(obj)", "Viper.System.Object.Equals"),
        RUNTIME_METHOD("GetHashCode", "i64()", "Viper.System.Object.GetHashCode"),
        RUNTIME_METHOD("ToString", "str()", "Viper.System.Object.ToString"),
        /* Model ReferenceEquals as a class-level method entry for binding */
        RUNTIME_METHOD("ReferenceEquals", "i1(obj,obj)", "Viper.System.Object.ReferenceEquals")))

RUNTIME_CLASS("Viper.System.IO.File",
              RTCLS_File,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(
                  /* Static utility methods mapped to Viper.IO.File.* */
                  RUNTIME_METHOD("Exists", "i1(str)", "Viper.IO.File.Exists"),
                  RUNTIME_METHOD("ReadAllText", "str(str)", "Viper.IO.File.ReadAllText"),
                  RUNTIME_METHOD("WriteAllText", "void(str,str)", "Viper.IO.File.WriteAllText"),
                  RUNTIME_METHOD("Delete", "void(str)", "Viper.IO.File.Delete")))

// Canonical non-generic list under Viper.Collections
RUNTIME_CLASS(
    "Viper.Collections.List",
    RTCLS_List,
    "obj",
    "Viper.Collections.List.New",
    RUNTIME_PROPS(RUNTIME_PROP("Count", "i64", "Viper.Collections.List.get_Count", nullptr)),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Add", "void(obj)", "Viper.Collections.List.Add"),
        RUNTIME_METHOD("Clear", "void()", "Viper.Collections.List.Clear"),
        RUNTIME_METHOD("RemoveAt", "void(i64)", "Viper.Collections.List.RemoveAt"),
        RUNTIME_METHOD("get_Item", "obj(i64)", "Viper.Collections.List.get_Item"),
        RUNTIME_METHOD("set_Item", "void(i64,obj)", "Viper.Collections.List.set_Item")))

// Legacy/compat alias under Viper.System.Collections; maps to the same externs
RUNTIME_CLASS(
    "Viper.System.Collections.List",
    RTCLS_List,
    "obj",
    "Viper.System.Collections.List.New",
    RUNTIME_PROPS(RUNTIME_PROP("Count", "i64", "Viper.System.Collections.List.get_Count", nullptr)),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Add", "void(obj)", "Viper.System.Collections.List.Add"),
        RUNTIME_METHOD("Clear", "void()", "Viper.System.Collections.List.Clear"),
        RUNTIME_METHOD("RemoveAt", "void(i64)", "Viper.System.Collections.List.RemoveAt"),
        RUNTIME_METHOD("get_Item", "obj(i64)", "Viper.System.Collections.List.get_Item"),
        RUNTIME_METHOD("set_Item", "void(i64,obj)", "Viper.System.Collections.List.set_Item")))
