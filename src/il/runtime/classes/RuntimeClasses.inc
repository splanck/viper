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
    "Viper.Strings.FromStr", /* ctor helper; keep NULL if gating NEW later */
    RUNTIME_PROPS(RUNTIME_PROP("Length", "i64", "Viper.String.get_Length", nullptr),
                  RUNTIME_PROP("IsEmpty", "i1", "Viper.String.get_IsEmpty", nullptr)),
    RUNTIME_METHODS(RUNTIME_METHOD("Substring", "str(i64,i64)", "Viper.String.Substring"),
                    RUNTIME_METHOD("Concat", "str(str)", "Viper.String.Concat"),
                    RUNTIME_METHOD("Left", "str(i64)", "Viper.String.Left"),
                    RUNTIME_METHOD("Right", "str(i64)", "Viper.String.Right"),
                    RUNTIME_METHOD("Mid", "str(i64)", "Viper.String.Mid"),
                    RUNTIME_METHOD("MidLen", "str(i64,i64)", "Viper.String.MidLen"),
                    RUNTIME_METHOD("Trim", "str()", "Viper.String.Trim"),
                    RUNTIME_METHOD("TrimStart", "str()", "Viper.String.TrimStart"),
                    RUNTIME_METHOD("TrimEnd", "str()", "Viper.String.TrimEnd"),
                    RUNTIME_METHOD("ToUpper", "str()", "Viper.String.ToUpper"),
                    RUNTIME_METHOD("ToLower", "str()", "Viper.String.ToLower"),
                    RUNTIME_METHOD("IndexOf", "i64(str)", "Viper.String.IndexOf"),
                    RUNTIME_METHOD("IndexOfFrom", "i64(i64,str)", "Viper.String.IndexOfFrom"),
                    RUNTIME_METHOD("Chr", "str(i64)", "Viper.String.Chr"),
                    RUNTIME_METHOD("Asc", "i64()", "Viper.String.Asc")))

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
RUNTIME_CLASS("Viper.Object",
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
    RUNTIME_METHODS(RUNTIME_METHOD("Exists", "i1(str)", "Viper.IO.File.Exists"),
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
RUNTIME_CLASS("Viper.Math",
              RTCLS_Math,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(RUNTIME_METHOD("Abs", "f64(f64)", "Viper.Math.Abs"),
                              RUNTIME_METHOD("AbsInt", "i64(i64)", "Viper.Math.AbsInt"),
                              RUNTIME_METHOD("Sqrt", "f64(f64)", "Viper.Math.Sqrt"),
                              RUNTIME_METHOD("Sin", "f64(f64)", "Viper.Math.Sin"),
                              RUNTIME_METHOD("Cos", "f64(f64)", "Viper.Math.Cos"),
                              RUNTIME_METHOD("Tan", "f64(f64)", "Viper.Math.Tan"),
                              RUNTIME_METHOD("Atan", "f64(f64)", "Viper.Math.Atan"),
                              RUNTIME_METHOD("Floor", "f64(f64)", "Viper.Math.Floor"),
                              RUNTIME_METHOD("Ceil", "f64(f64)", "Viper.Math.Ceil"),
                              RUNTIME_METHOD("Pow", "f64(f64,f64)", "Viper.Math.Pow"),
                              RUNTIME_METHOD("Log", "f64(f64)", "Viper.Math.Log"),
                              RUNTIME_METHOD("Exp", "f64(f64)", "Viper.Math.Exp"),
                              RUNTIME_METHOD("Sgn", "f64(f64)", "Viper.Math.Sgn"),
                              RUNTIME_METHOD("SgnInt", "i64(i64)", "Viper.Math.SgnInt"),
                              RUNTIME_METHOD("Min", "f64(f64,f64)", "Viper.Math.Min"),
                              RUNTIME_METHOD("Max", "f64(f64,f64)", "Viper.Math.Max"),
                              RUNTIME_METHOD("MinInt", "i64(i64,i64)", "Viper.Math.MinInt"),
                              RUNTIME_METHOD("MaxInt", "i64(i64,i64)", "Viper.Math.MaxInt")))

// Canonical console I/O utility class
RUNTIME_CLASS("Viper.Console",
              RTCLS_Console,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(RUNTIME_METHOD("WriteLine", "void(str)", "Viper.Console.PrintStr"),
                              RUNTIME_METHOD("ReadLine", "str()", "Viper.Console.ReadLine")))

// Canonical type conversion utility class
RUNTIME_CLASS(
    "Viper.Convert",
    RTCLS_Convert,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(RUNTIME_METHOD("ToInt64", "i64(str)", "Viper.Convert.ToInt"),
                    RUNTIME_METHOD("ToDouble", "f64(str)", "Viper.Convert.ToDouble"),
                    RUNTIME_METHOD("ToString_Int", "str(i64)", "Viper.Strings.FromInt"),
                    RUNTIME_METHOD("ToString_Double", "str(f64)", "Viper.Strings.FromDouble")))

// Random number generation
RUNTIME_CLASS("Viper.Random",
              RTCLS_Random,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(RUNTIME_METHOD("Seed", "void(i64)", "Viper.Random.Seed"),
                              RUNTIME_METHOD("Next", "f64()", "Viper.Random.Next"),
                              RUNTIME_METHOD("NextInt", "i64(i64)", "Viper.Random.NextInt")))

// Environment and command-line arguments
RUNTIME_CLASS("Viper.Environment",
              RTCLS_Environment,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(
                  RUNTIME_METHOD("GetArgumentCount", "i64()", "Viper.Environment.GetArgumentCount"),
                  RUNTIME_METHOD("GetArgument", "str(i64)", "Viper.Environment.GetArgument"),
                  RUNTIME_METHOD("GetCommandLine", "str()", "Viper.Environment.GetCommandLine"),
                  RUNTIME_METHOD("IsNative", "i1()", "Viper.Environment.IsNative")))

// Graphics window for 2D rendering
// Notes:
// - Constructor takes width, height, and title.
// - Properties provide window dimensions and close state.
// - Drawing methods take coordinates and color (0x00RRGGBB format).
RUNTIME_CLASS(
    "Viper.Graphics.Window",
    RTCLS_GfxWindow,
    "obj",
    "Viper.Graphics.Window.New",
    RUNTIME_PROPS(RUNTIME_PROP("Width", "i64", "rt_gfx_window_width", nullptr),
                  RUNTIME_PROP("Height", "i64", "rt_gfx_window_height", nullptr),
                  RUNTIME_PROP("ShouldClose", "i64", "rt_gfx_window_should_close", nullptr)),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Update", "void()", "rt_gfx_window_update"),
        RUNTIME_METHOD("Clear", "void(i64)", "rt_gfx_window_clear"),
        RUNTIME_METHOD("DrawLine", "void(i64,i64,i64,i64,i64)", "rt_gfx_draw_line"),
        RUNTIME_METHOD("DrawRect", "void(i64,i64,i64,i64,i64)", "rt_gfx_draw_rect"),
        RUNTIME_METHOD("DrawRectOutline", "void(i64,i64,i64,i64,i64)", "rt_gfx_draw_rect_outline"),
        RUNTIME_METHOD("DrawCircle", "void(i64,i64,i64,i64)", "rt_gfx_draw_circle"),
        RUNTIME_METHOD("DrawCircleOutline", "void(i64,i64,i64,i64)", "rt_gfx_draw_circle_outline"),
        RUNTIME_METHOD("SetPixel", "void(i64,i64,i64)", "rt_gfx_set_pixel"),
        RUNTIME_METHOD("PollEvent", "i64()", "rt_gfx_poll_event"),
        RUNTIME_METHOD("KeyDown", "i64(i64)", "rt_gfx_key_down")))

// Static color utility class for graphics
RUNTIME_CLASS("Viper.Graphics.Color",
              RTCLS_GfxColor,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(RUNTIME_METHOD("RGB", "i64(i64,i64,i64)", "rt_gfx_rgb")))

// Date/time utilities
// Notes:
// - Timestamps are Unix timestamps in seconds since epoch (UTC).
// - Now() and NowMs() return current time.
// - Component extraction (Year, Month, etc.) uses local time.
// - Create() builds a timestamp from local time components.
RUNTIME_CLASS("Viper.DateTime",
              RTCLS_DateTime,
              "obj",
              "", /* static utility class; no instance ctor */
              RUNTIME_PROPS(),
              RUNTIME_METHODS(
                  RUNTIME_METHOD("Now", "i64()", "Viper.DateTime.Now"),
                  RUNTIME_METHOD("NowMs", "i64()", "Viper.DateTime.NowMs"),
                  RUNTIME_METHOD("Year", "i64(i64)", "Viper.DateTime.Year"),
                  RUNTIME_METHOD("Month", "i64(i64)", "Viper.DateTime.Month"),
                  RUNTIME_METHOD("Day", "i64(i64)", "Viper.DateTime.Day"),
                  RUNTIME_METHOD("Hour", "i64(i64)", "Viper.DateTime.Hour"),
                  RUNTIME_METHOD("Minute", "i64(i64)", "Viper.DateTime.Minute"),
                  RUNTIME_METHOD("Second", "i64(i64)", "Viper.DateTime.Second"),
                  RUNTIME_METHOD("DayOfWeek", "i64(i64)", "Viper.DateTime.DayOfWeek"),
                  RUNTIME_METHOD("Format", "str(i64,str)", "Viper.DateTime.Format"),
                  RUNTIME_METHOD("ToISO", "str(i64)", "Viper.DateTime.ToISO"),
                  RUNTIME_METHOD("Create", "i64(i64,i64,i64,i64,i64,i64)", "Viper.DateTime.Create"),
                  RUNTIME_METHOD("AddSeconds", "i64(i64,i64)", "Viper.DateTime.AddSeconds"),
                  RUNTIME_METHOD("AddDays", "i64(i64,i64)", "Viper.DateTime.AddDays"),
                  RUNTIME_METHOD("Diff", "i64(i64,i64)", "Viper.DateTime.Diff")))

// String-keyed dictionary (hash map)
// Notes:
// - Keys are strings, values are objects.
// - Keys are copied internally (dictionary owns copies).
// - Values are retained when stored, released when removed.
RUNTIME_CLASS(
    "Viper.Collections.Dictionary",
    RTCLS_Dictionary,
    "obj",
    "Viper.Collections.Dictionary.New",
    RUNTIME_PROPS(RUNTIME_PROP("Count", "i64", "Viper.Collections.Dictionary.get_Count", nullptr)),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Set", "void(str,obj)", "Viper.Collections.Dictionary.Set"),
        RUNTIME_METHOD("Get", "obj(str)", "Viper.Collections.Dictionary.Get"),
        RUNTIME_METHOD("ContainsKey", "i64(str)", "Viper.Collections.Dictionary.ContainsKey"),
        RUNTIME_METHOD("Remove", "i64(str)", "Viper.Collections.Dictionary.Remove"),
        RUNTIME_METHOD("Clear", "void()", "Viper.Collections.Dictionary.Clear")))

// Terminal/console control utilities
// Notes:
// - Static utility class for terminal I/O operations.
// - SetColor takes foreground and background color indices.
// - SetPosition uses 1-based column, row coordinates.
// - BeginBatch/EndBatch group drawing operations for efficiency.
RUNTIME_CLASS(
    "Viper.Terminal",
    RTCLS_Terminal,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("Clear", "void()", "Viper.Terminal.Clear"),
        RUNTIME_METHOD("SetColor", "void(i64,i64)", "Viper.Terminal.SetColor"),
        RUNTIME_METHOD("SetPosition", "void(i64,i64)", "Viper.Terminal.SetPosition"),
        RUNTIME_METHOD("SetCursorVisible", "void(i64)", "Viper.Terminal.SetCursorVisible"),
        RUNTIME_METHOD("SetAltScreen", "void(i64)", "Viper.Terminal.SetAltScreen"),
        RUNTIME_METHOD("Bell", "void()", "Viper.Terminal.Bell"),
        RUNTIME_METHOD("InKey", "str()", "Viper.Terminal.InKey"),
        RUNTIME_METHOD("GetKey", "str()", "Viper.Terminal.GetKey"),
        RUNTIME_METHOD("GetKeyTimeout", "str(i64)", "Viper.Terminal.GetKeyTimeout"),
        RUNTIME_METHOD("BeginBatch", "void()", "Viper.Terminal.BeginBatch"),
        RUNTIME_METHOD("EndBatch", "void()", "Viper.Terminal.EndBatch"),
        RUNTIME_METHOD("Flush", "void()", "Viper.Terminal.Flush")))

// Time and delay utilities
// Notes:
// - Static utility class for time-related operations.
// - SleepMs pauses execution for the specified milliseconds.
// - GetTickCount returns milliseconds since an arbitrary epoch.
RUNTIME_CLASS(
    "Viper.Time",
    RTCLS_Time,
    "obj",
    "", /* static utility class; no instance ctor */
    RUNTIME_PROPS(),
    RUNTIME_METHODS(
        RUNTIME_METHOD("SleepMs", "void(i64)", "Viper.Time.SleepMs"),
        RUNTIME_METHOD("GetTickCount", "i64()", "Viper.Time.GetTickCount")))
