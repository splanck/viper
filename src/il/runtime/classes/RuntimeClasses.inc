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

RUNTIME_CLASS(
  "Viper.String",
  RTCLS_String,
  "opaque*",
  "Viper.Strings.FromStr",   /* ctor helper; keep NULL if gating NEW later */
  RUNTIME_PROPS(
    RUNTIME_PROP("Length",  "i64", "Viper.String.get_Length", nullptr),
    RUNTIME_PROP("IsEmpty", "i1",  "Viper.String.get_IsEmpty", nullptr)
  ),
  RUNTIME_METHODS(
    RUNTIME_METHOD("Substring", "str(i64,i64)", "Viper.String.Substring"),
    RUNTIME_METHOD("Concat",    "str(str)",     "Viper.String.ConcatSelf")
  )
)

// Experimental: Viper.Text.StringBuilder — data + shims
// Notes:
// - This class is seeded to demonstrate catalog-driven OOP integration.
// - Members are wired to canonical Viper.Text.* externs; handlers are TODO
//   stubs in the runtime that currently return sensible defaults.
RUNTIME_CLASS(
  "Viper.Text.StringBuilder",
  RTCLS_StringBuilder,
  "opaque*",
  "Viper.Text.StringBuilder.New",  /* ctor; bridges to rt_ns_stringbuilder_new */
  RUNTIME_PROPS(
    RUNTIME_PROP("Length",   "i64", "Viper.Text.StringBuilder.get_Length",   nullptr),
    RUNTIME_PROP("Capacity", "i64", "Viper.Text.StringBuilder.get_Capacity", nullptr)
  ),
  RUNTIME_METHODS(
    /* Returns self to enable chaining */
    RUNTIME_METHOD("Append",   "obj(str)",  "Viper.Text.StringBuilder.Append"),
    RUNTIME_METHOD("ToString", "str()",     "Viper.Text.StringBuilder.ToString"),
    RUNTIME_METHOD("Clear",    "void()",    "Viper.Text.StringBuilder.Clear")
  )
)

// RC-B1: Add system-qualified aliases that map object members to procedural
// Viper.Strings.* externs. These entries coexist with the canonical
// Viper.String and Viper.Text.StringBuilder to preserve existing behaviour.
RUNTIME_CLASS(
  "Viper.System.String",
  RTCLS_String,
  "str",             /* runtime string handle (opaque) */
  "",                /* ctor unused: strings created via literals/ops */
  RUNTIME_PROPS(
    RUNTIME_PROP("Length",  "i64", "Viper.Strings.Len", nullptr)
    /* Note: IsEmpty helper not present under Viper.Strings; omit for now. */
  ),
  RUNTIME_METHODS(
    RUNTIME_METHOD("Substring", "str(i64,i64)", "Viper.Strings.Mid"),
    RUNTIME_METHOD("Concat",    "str(str)",     "Viper.Strings.Concat")
  )
)

RUNTIME_CLASS(
  "Viper.System.Text.StringBuilder",
  RTCLS_StringBuilder,
  "obj",            /* opaque builder handle */
  "Viper.System.Text.StringBuilder.New", /* canonical ctor helper present in registry */
  RUNTIME_PROPS(
    RUNTIME_PROP("Length", "i64", "Viper.Strings.Builder.Length", nullptr)
  ),
  RUNTIME_METHODS(
    /* Procedural builder methods are exposed under Viper.Text.* in this repo. */
    RUNTIME_METHOD("Append",   "obj(str)",  "Viper.Text.StringBuilder.Append"),
    RUNTIME_METHOD("ToString", "str()",     "Viper.Text.StringBuilder.ToString"),
    RUNTIME_METHOD("Clear",    "void()",    "Viper.Text.StringBuilder.Clear")
  )
)
