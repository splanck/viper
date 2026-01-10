//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Runtime.cpp
/// @brief Runtime function registration for the ViperLang semantic analyzer.
///
/// This file contains the registration of all Viper.* namespace runtime
/// functions with their return types. These functions are defined in
/// src/il/runtime/runtime.def and implemented in the IL runtime library.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"

namespace il::frontends::viperlang
{

void Sema::initRuntimeFunctions()
{
    // Auto-generated from src/il/runtime/runtime.def
    // This registers all Viper.* namespace functions as extern symbols in scope.
    // Using defineExternFunction() ensures they work exactly like user-defined
    // namespaced functions, with isExtern=true for the lowerer.

    // =========================================================================
    // RUNTIME CLASSES (from runtime.def RT_CLASS_BEGIN entries)
    // =========================================================================
    // Register runtime classes as named ptr types so they can be used in
    // type annotations (e.g., "canvas: Viper.Graphics.Canvas") and for
    // method call resolution.
    typeRegistry_["Viper.Graphics.Canvas"] = types::runtimeClass("Viper.Graphics.Canvas");
    typeRegistry_["Viper.Graphics.Color"] = types::runtimeClass("Viper.Graphics.Color");
    typeRegistry_["Viper.Graphics.Pixels"] = types::runtimeClass("Viper.Graphics.Pixels");
    typeRegistry_["Viper.Sound.Audio"] = types::runtimeClass("Viper.Sound.Audio");
    typeRegistry_["Viper.Sound.Sound"] = types::runtimeClass("Viper.Sound.Sound");
    typeRegistry_["Viper.Sound.Voice"] = types::runtimeClass("Viper.Sound.Voice");
    typeRegistry_["Viper.Sound.Music"] = types::runtimeClass("Viper.Sound.Music");

    // GUI classes
    typeRegistry_["Viper.GUI.App"] = types::runtimeClass("Viper.GUI.App");
    typeRegistry_["Viper.GUI.Font"] = types::runtimeClass("Viper.GUI.Font");
    typeRegistry_["Viper.GUI.Widget"] = types::runtimeClass("Viper.GUI.Widget");
    typeRegistry_["Viper.GUI.Label"] = types::runtimeClass("Viper.GUI.Label");
    typeRegistry_["Viper.GUI.Button"] = types::runtimeClass("Viper.GUI.Button");
    typeRegistry_["Viper.GUI.TextInput"] = types::runtimeClass("Viper.GUI.TextInput");
    typeRegistry_["Viper.GUI.Checkbox"] = types::runtimeClass("Viper.GUI.Checkbox");
    typeRegistry_["Viper.GUI.ScrollView"] = types::runtimeClass("Viper.GUI.ScrollView");
    typeRegistry_["Viper.GUI.TreeView"] = types::runtimeClass("Viper.GUI.TreeView");
    typeRegistry_["Viper.GUI.TabBar"] = types::runtimeClass("Viper.GUI.TabBar");
    typeRegistry_["Viper.GUI.Tab"] = types::runtimeClass("Viper.GUI.Tab");
    typeRegistry_["Viper.GUI.SplitPane"] = types::runtimeClass("Viper.GUI.SplitPane");
    typeRegistry_["Viper.GUI.CodeEditor"] = types::runtimeClass("Viper.GUI.CodeEditor");
    typeRegistry_["Viper.GUI.Dropdown"] = types::runtimeClass("Viper.GUI.Dropdown");
    typeRegistry_["Viper.GUI.Slider"] = types::runtimeClass("Viper.GUI.Slider");
    typeRegistry_["Viper.GUI.ProgressBar"] = types::runtimeClass("Viper.GUI.ProgressBar");
    typeRegistry_["Viper.GUI.ListBox"] = types::runtimeClass("Viper.GUI.ListBox");
    typeRegistry_["Viper.GUI.RadioGroup"] = types::runtimeClass("Viper.GUI.RadioGroup");
    typeRegistry_["Viper.GUI.RadioButton"] = types::runtimeClass("Viper.GUI.RadioButton");
    typeRegistry_["Viper.GUI.Spinner"] = types::runtimeClass("Viper.GUI.Spinner");
    typeRegistry_["Viper.GUI.Image"] = types::runtimeClass("Viper.GUI.Image");
    typeRegistry_["Viper.GUI.VBox"] = types::runtimeClass("Viper.GUI.VBox");
    typeRegistry_["Viper.GUI.HBox"] = types::runtimeClass("Viper.GUI.HBox");

    // =========================================================================
    // BITS
    // =========================================================================
    defineExternFunction("Viper.Bits.And", types::integer());
    defineExternFunction("Viper.Bits.Clear", types::integer());
    defineExternFunction("Viper.Bits.Count", types::integer());
    defineExternFunction("Viper.Bits.Flip", types::integer());
    defineExternFunction("Viper.Bits.Get", types::boolean());
    defineExternFunction("Viper.Bits.LeadZ", types::integer());
    defineExternFunction("Viper.Bits.Not", types::integer());
    defineExternFunction("Viper.Bits.Or", types::integer());
    defineExternFunction("Viper.Bits.Rotl", types::integer());
    defineExternFunction("Viper.Bits.Rotr", types::integer());
    defineExternFunction("Viper.Bits.Set", types::integer());
    defineExternFunction("Viper.Bits.Shl", types::integer());
    defineExternFunction("Viper.Bits.Shr", types::integer());
    defineExternFunction("Viper.Bits.Swap", types::integer());
    defineExternFunction("Viper.Bits.Toggle", types::integer());
    defineExternFunction("Viper.Bits.TrailZ", types::integer());
    defineExternFunction("Viper.Bits.Ushr", types::integer());
    defineExternFunction("Viper.Bits.Xor", types::integer());

    // =========================================================================
    // BOX
    // =========================================================================
    defineExternFunction("Viper.Box.I64", types::ptr());
    defineExternFunction("Viper.Box.F64", types::ptr());
    defineExternFunction("Viper.Box.I1", types::ptr());
    defineExternFunction("Viper.Box.Str", types::ptr());
    defineExternFunction("Viper.Box.ToI64", types::integer());
    defineExternFunction("Viper.Box.ToF64", types::number());
    defineExternFunction("Viper.Box.ToI1", types::integer());
    defineExternFunction("Viper.Box.ToStr", types::string());
    defineExternFunction("Viper.Box.Type", types::integer());
    defineExternFunction("Viper.Box.EqI64", types::integer());
    defineExternFunction("Viper.Box.EqF64", types::integer());
    defineExternFunction("Viper.Box.EqStr", types::integer());

    // =========================================================================
    // COLLECTIONS - BYTES
    // =========================================================================
    defineExternFunction("Viper.Collections.Bytes.Clone", types::ptr());
    defineExternFunction("Viper.Collections.Bytes.Copy", types::voidType());
    defineExternFunction("Viper.Collections.Bytes.Fill", types::voidType());
    defineExternFunction("Viper.Collections.Bytes.Find", types::integer());
    defineExternFunction("Viper.Collections.Bytes.FromBase64", types::ptr());
    defineExternFunction("Viper.Collections.Bytes.FromHex", types::ptr());
    defineExternFunction("Viper.Collections.Bytes.FromStr", types::ptr());
    defineExternFunction("Viper.Collections.Bytes.Get", types::integer());
    defineExternFunction("Viper.Collections.Bytes.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Bytes.New", types::ptr());
    defineExternFunction("Viper.Collections.Bytes.Set", types::voidType());
    defineExternFunction("Viper.Collections.Bytes.Slice", types::ptr());
    defineExternFunction("Viper.Collections.Bytes.ToBase64", types::string());
    defineExternFunction("Viper.Collections.Bytes.ToHex", types::string());
    defineExternFunction("Viper.Collections.Bytes.ToStr", types::string());

    // =========================================================================
    // COLLECTIONS - BAG
    // =========================================================================
    defineExternFunction("Viper.Collections.Bag.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Bag.Common", types::ptr());
    defineExternFunction("Viper.Collections.Bag.Diff", types::ptr());
    defineExternFunction("Viper.Collections.Bag.Drop", types::boolean());
    defineExternFunction("Viper.Collections.Bag.Has", types::boolean());
    defineExternFunction("Viper.Collections.Bag.get_IsEmpty", types::boolean());
    defineExternFunction("Viper.Collections.Bag.Items", types::ptr());
    defineExternFunction("Viper.Collections.Bag.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Bag.Merge", types::ptr());
    defineExternFunction("Viper.Collections.Bag.New", types::ptr());
    defineExternFunction("Viper.Collections.Bag.Put", types::boolean());

    // =========================================================================
    // COLLECTIONS - LIST
    // =========================================================================
    defineExternFunction("Viper.Collections.List.Add", types::voidType());
    defineExternFunction("Viper.Collections.List.Clear", types::voidType());
    defineExternFunction("Viper.Collections.List.Find", types::integer());
    defineExternFunction("Viper.Collections.List.get_Count", types::integer());
    defineExternFunction("Viper.Collections.List.get_Item", types::ptr());
    defineExternFunction("Viper.Collections.List.Has", types::boolean());
    defineExternFunction("Viper.Collections.List.Insert", types::voidType());
    defineExternFunction("Viper.Collections.List.New", types::ptr());
    defineExternFunction("Viper.Collections.List.Remove", types::boolean());
    defineExternFunction("Viper.Collections.List.RemoveAt", types::voidType());
    defineExternFunction("Viper.Collections.List.set_Item", types::voidType());

    // =========================================================================
    // COLLECTIONS - MAP
    // =========================================================================
    defineExternFunction("Viper.Collections.Map.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Map.Get", types::ptr());
    defineExternFunction("Viper.Collections.Map.GetOr", types::ptr());
    defineExternFunction("Viper.Collections.Map.Has", types::boolean());
    defineExternFunction("Viper.Collections.Map.get_IsEmpty", types::boolean());
    defineExternFunction("Viper.Collections.Map.Keys", types::ptr());
    defineExternFunction("Viper.Collections.Map.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Map.New", types::ptr());
    defineExternFunction("Viper.Collections.Map.Remove", types::boolean());
    defineExternFunction("Viper.Collections.Map.Set", types::voidType());
    defineExternFunction("Viper.Collections.Map.SetIfMissing", types::boolean());
    defineExternFunction("Viper.Collections.Map.Values", types::ptr());

    // =========================================================================
    // COLLECTIONS - QUEUE, STACK, HEAP, SEQ, RING, TREEMAP
    // =========================================================================
    defineExternFunction("Viper.Collections.Queue.Add", types::voidType());
    defineExternFunction("Viper.Collections.Queue.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Queue.get_IsEmpty", types::boolean());
    defineExternFunction("Viper.Collections.Queue.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Queue.New", types::ptr());
    defineExternFunction("Viper.Collections.Queue.Peek", types::ptr());
    defineExternFunction("Viper.Collections.Queue.Take", types::ptr());
    defineExternFunction("Viper.Collections.Heap.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Heap.get_IsEmpty", types::boolean());
    defineExternFunction("Viper.Collections.Heap.get_IsMax", types::boolean());
    defineExternFunction("Viper.Collections.Heap.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Heap.New", types::ptr());
    defineExternFunction("Viper.Collections.Heap.NewMax", types::ptr());
    defineExternFunction("Viper.Collections.Heap.Peek", types::ptr());
    defineExternFunction("Viper.Collections.Heap.Pop", types::ptr());
    defineExternFunction("Viper.Collections.Heap.Push", types::voidType());
    defineExternFunction("Viper.Collections.Heap.ToSeq", types::ptr());
    defineExternFunction("Viper.Collections.Stack.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Stack.get_IsEmpty", types::boolean());
    defineExternFunction("Viper.Collections.Stack.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Stack.New", types::ptr());
    defineExternFunction("Viper.Collections.Stack.Peek", types::ptr());
    defineExternFunction("Viper.Collections.Stack.Pop", types::ptr());
    defineExternFunction("Viper.Collections.Stack.Push", types::voidType());
    defineExternFunction("Viper.Collections.Seq.Push", types::voidType()); // VL-012: was Add
    defineExternFunction("Viper.Collections.Seq.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Seq.Get", types::ptr());
    defineExternFunction("Viper.Collections.Seq.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Seq.New", types::ptr());
    defineExternFunction("Viper.Collections.Seq.Pop", types::ptr());
    defineExternFunction("Viper.Collections.Seq.Set", types::voidType());
    defineExternFunction("Viper.Collections.Ring.Clear", types::voidType());
    defineExternFunction("Viper.Collections.Ring.Get", types::ptr());
    defineExternFunction("Viper.Collections.Ring.get_Len", types::integer());
    defineExternFunction("Viper.Collections.Ring.New", types::ptr());
    defineExternFunction("Viper.Collections.Ring.Pop", types::ptr());
    defineExternFunction("Viper.Collections.Ring.Push", types::voidType());
    defineExternFunction("Viper.Collections.TreeMap.Clear", types::voidType());
    defineExternFunction("Viper.Collections.TreeMap.Get", types::ptr());
    defineExternFunction("Viper.Collections.TreeMap.Has", types::boolean());
    defineExternFunction("Viper.Collections.TreeMap.Keys", types::ptr());
    defineExternFunction("Viper.Collections.TreeMap.get_Len", types::integer());
    defineExternFunction("Viper.Collections.TreeMap.New", types::ptr());
    defineExternFunction("Viper.Collections.TreeMap.Drop", types::boolean()); // VL-013: was Remove
    defineExternFunction("Viper.Collections.TreeMap.Set", types::voidType());

    // =========================================================================
    // CRYPTO - HASH, RAND, KEYDERIVE
    // =========================================================================
    defineExternFunction("Viper.Crypto.Hash.CRC32", types::integer());
    defineExternFunction("Viper.Crypto.Hash.MD5", types::string());
    defineExternFunction("Viper.Crypto.Hash.SHA1", types::string());
    defineExternFunction("Viper.Crypto.Hash.SHA256", types::string());
    defineExternFunction("Viper.Crypto.Hash.SHA384", types::string());
    defineExternFunction("Viper.Crypto.Hash.SHA512", types::string());
    defineExternFunction("Viper.Crypto.Hash.HmacMD5", types::string());
    defineExternFunction("Viper.Crypto.Hash.HmacSHA1", types::string());
    defineExternFunction("Viper.Crypto.Hash.HmacSHA256", types::string());
    defineExternFunction("Viper.Crypto.Rand.Bytes", types::ptr());
    defineExternFunction("Viper.Crypto.Rand.Int", types::integer());
    defineExternFunction("Viper.Crypto.KeyDerive.Pbkdf2SHA256", types::ptr());

    // =========================================================================
    // DATETIME
    // =========================================================================
    defineExternFunction("Viper.DateTime.Now", types::integer());
    defineExternFunction("Viper.DateTime.UtcNow", types::integer());
    defineExternFunction("Viper.DateTime.Create", types::integer());
    defineExternFunction("Viper.DateTime.Format", types::string());
    defineExternFunction("Viper.DateTime.Parse", types::integer());
    defineExternFunction("Viper.DateTime.Year", types::integer());
    defineExternFunction("Viper.DateTime.Month", types::integer());
    defineExternFunction("Viper.DateTime.Day", types::integer());
    defineExternFunction("Viper.DateTime.Hour", types::integer());
    defineExternFunction("Viper.DateTime.Minute", types::integer());
    defineExternFunction("Viper.DateTime.Second", types::integer());
    defineExternFunction("Viper.DateTime.DayOfWeek", types::integer());
    defineExternFunction("Viper.DateTime.DayOfYear", types::integer());
    defineExternFunction("Viper.DateTime.AddDays", types::integer());
    defineExternFunction("Viper.DateTime.AddHours", types::integer());
    defineExternFunction("Viper.DateTime.AddMinutes", types::integer());
    defineExternFunction("Viper.DateTime.AddSeconds", types::integer());
    defineExternFunction("Viper.DateTime.ToUnix", types::integer());
    defineExternFunction("Viper.DateTime.FromUnix", types::integer());

    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    defineExternFunction("Viper.Diagnostics.Assert", types::voidType());
    defineExternFunction("Viper.Diagnostics.AssertEq", types::voidType());
    defineExternFunction("Viper.Diagnostics.AssertNe", types::voidType());
    defineExternFunction("Viper.Diagnostics.AssertTrue", types::voidType());
    defineExternFunction("Viper.Diagnostics.AssertFalse", types::voidType());
    defineExternFunction("Viper.Diagnostics.Trap", types::voidType());
    defineExternFunction("Viper.Diagnostics.Stopwatch.Start", types::ptr());
    defineExternFunction("Viper.Diagnostics.Stopwatch.Elapsed", types::integer());
    defineExternFunction("Viper.Diagnostics.Stopwatch.ElapsedMs", types::integer());
    defineExternFunction("Viper.Diagnostics.Stopwatch.Reset", types::voidType());

    // =========================================================================
    // ENVIRONMENT
    // =========================================================================
    defineExternFunction("Viper.Environment.GetArgument", types::string());
    defineExternFunction("Viper.Environment.GetArgumentCount", types::integer());
    defineExternFunction("Viper.Environment.GetCommandLine", types::string());
    defineExternFunction("Viper.Environment.GetVar", types::string());
    defineExternFunction("Viper.Environment.SetVar", types::voidType());
    defineExternFunction("Viper.Environment.HasVar", types::boolean());

    // =========================================================================
    // EXEC
    // =========================================================================
    defineExternFunction("Viper.Exec.Run", types::integer());
    defineExternFunction("Viper.Exec.Capture", types::string());
    defineExternFunction("Viper.Exec.Shell", types::integer());

    // =========================================================================
    // FMT (FORMATTING)
    // =========================================================================
    defineExternFunction("Viper.Fmt.Str", types::string());
    defineExternFunction("Viper.Fmt.Int", types::string());
    defineExternFunction("Viper.Fmt.Num", types::string());
    defineExternFunction("Viper.Fmt.Bool", types::string());
    defineExternFunction("Viper.Fmt.Pad", types::string());
    defineExternFunction("Viper.Fmt.PadLeft", types::string());
    defineExternFunction("Viper.Fmt.PadRight", types::string());
    defineExternFunction("Viper.Fmt.Hex", types::string());
    defineExternFunction("Viper.Fmt.Oct", types::string());
    defineExternFunction("Viper.Fmt.Bin", types::string());
    defineExternFunction("Viper.Fmt.Size", types::string());

    // =========================================================================
    // GRAPHICS - CANVAS
    // =========================================================================
    defineExternFunction("Viper.Graphics.Canvas.New", types::runtimeClass("Viper.Graphics.Canvas"));
    defineExternFunction("Viper.Graphics.Canvas.Clear", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Plot", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Line", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Box", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Frame", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Disc", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Ring", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Text", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.TextBg", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Poll", types::integer());
    defineExternFunction("Viper.Graphics.Canvas.Flip", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.KeyHeld", types::integer());
    defineExternFunction("Viper.Graphics.Canvas.GetPixel", types::integer());
    defineExternFunction("Viper.Graphics.Canvas.get_Width", types::integer());
    defineExternFunction("Viper.Graphics.Canvas.get_Height", types::integer());
    defineExternFunction("Viper.Graphics.Canvas.get_ShouldClose", types::integer());
    defineExternFunction("Viper.Graphics.Canvas.ThickLine", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.RoundBox", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.RoundFrame", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Blit", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.BlitRegion", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.BlitAlpha", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Triangle", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.TriangleFrame", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.Ellipse", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.EllipseFrame", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.FloodFill", types::voidType());
    defineExternFunction("Viper.Graphics.Canvas.SaveBmp", types::integer());
    defineExternFunction("Viper.Graphics.Color.RGB", types::integer());
    defineExternFunction("Viper.Graphics.Color.RGBA", types::integer());
    defineExternFunction("Viper.Graphics.Color.Red", types::integer());
    defineExternFunction("Viper.Graphics.Color.Green", types::integer());
    defineExternFunction("Viper.Graphics.Color.Blue", types::integer());
    defineExternFunction("Viper.Graphics.Color.Alpha", types::integer());
    defineExternFunction("Viper.Graphics.Color.Blend", types::integer());
    defineExternFunction("Viper.Graphics.Pixels.New", types::runtimeClass("Viper.Graphics.Pixels"));
    defineExternFunction("Viper.Graphics.Pixels.LoadBmp",
        types::runtimeClass("Viper.Graphics.Pixels"));
    defineExternFunction("Viper.Graphics.Pixels.Get", types::integer());
    defineExternFunction("Viper.Graphics.Pixels.Set", types::voidType());
    defineExternFunction("Viper.Graphics.Pixels.get_Width", types::integer());
    defineExternFunction("Viper.Graphics.Pixels.get_Height", types::integer());
    defineExternFunction("Viper.Graphics.Pixels.Clear", types::voidType());
    defineExternFunction("Viper.Graphics.Pixels.Clone", types::runtimeClass("Viper.Graphics.Pixels"));
    defineExternFunction("Viper.Graphics.Pixels.Copy", types::voidType());
    defineExternFunction("Viper.Graphics.Pixels.Fill", types::voidType());
    defineExternFunction("Viper.Graphics.Pixels.ToBytes", types::ptr());
    defineExternFunction("Viper.Graphics.Pixels.SaveBmp", types::integer());
    defineExternFunction("Viper.Graphics.Pixels.FlipH", types::runtimeClass("Viper.Graphics.Pixels"));
    defineExternFunction("Viper.Graphics.Pixels.FlipV", types::runtimeClass("Viper.Graphics.Pixels"));
    defineExternFunction("Viper.Graphics.Pixels.Scale", types::runtimeClass("Viper.Graphics.Pixels"));

    // =========================================================================
    // SOUND - AUDIO SYSTEM
    // =========================================================================
    defineExternFunction("Viper.Sound.Audio.Init", types::integer());
    defineExternFunction("Viper.Sound.Audio.Shutdown", types::voidType());
    defineExternFunction("Viper.Sound.Audio.SetMasterVolume", types::voidType());
    defineExternFunction("Viper.Sound.Audio.GetMasterVolume", types::integer());
    defineExternFunction("Viper.Sound.Audio.PauseAll", types::voidType());
    defineExternFunction("Viper.Sound.Audio.ResumeAll", types::voidType());
    defineExternFunction("Viper.Sound.Audio.StopAllSounds", types::voidType());

    // =========================================================================
    // SOUND - SOUND EFFECTS
    // =========================================================================
    defineExternFunction("Viper.Sound.Sound.Load", types::runtimeClass("Viper.Sound.Sound"));
    defineExternFunction("Viper.Sound.Sound.Free", types::voidType());
    defineExternFunction("Viper.Sound.Sound.Play", types::integer());
    defineExternFunction("Viper.Sound.Sound.PlayEx", types::integer());
    defineExternFunction("Viper.Sound.Sound.PlayLoop", types::integer());

    // =========================================================================
    // SOUND - VOICE CONTROL
    // =========================================================================
    defineExternFunction("Viper.Sound.Voice.Stop", types::voidType());
    defineExternFunction("Viper.Sound.Voice.SetVolume", types::voidType());
    defineExternFunction("Viper.Sound.Voice.SetPan", types::voidType());
    defineExternFunction("Viper.Sound.Voice.IsPlaying", types::integer());

    // =========================================================================
    // SOUND - MUSIC STREAMING
    // =========================================================================
    defineExternFunction("Viper.Sound.Music.Load", types::runtimeClass("Viper.Sound.Music"));
    defineExternFunction("Viper.Sound.Music.Free", types::voidType());
    defineExternFunction("Viper.Sound.Music.Play", types::voidType());
    defineExternFunction("Viper.Sound.Music.Stop", types::voidType());
    defineExternFunction("Viper.Sound.Music.Pause", types::voidType());
    defineExternFunction("Viper.Sound.Music.Resume", types::voidType());
    defineExternFunction("Viper.Sound.Music.SetVolume", types::voidType());
    defineExternFunction("Viper.Sound.Music.get_Volume", types::integer());
    defineExternFunction("Viper.Sound.Music.IsPlaying", types::integer());
    defineExternFunction("Viper.Sound.Music.Seek", types::voidType());
    defineExternFunction("Viper.Sound.Music.get_Position", types::integer());
    defineExternFunction("Viper.Sound.Music.get_Duration", types::integer());

    // =========================================================================
    // INPUT
    // =========================================================================
    defineExternFunction("Viper.Input.Keyboard.IsDown", types::boolean());
    defineExternFunction("Viper.Input.Keyboard.WasPressed", types::boolean());
    defineExternFunction("Viper.Input.Mouse.GetX", types::integer());
    defineExternFunction("Viper.Input.Mouse.GetY", types::integer());
    defineExternFunction("Viper.Input.Mouse.IsDown", types::boolean());
    defineExternFunction("Viper.Input.Pad.IsConnected", types::boolean());
    defineExternFunction("Viper.Input.Pad.GetAxis", types::number());
    defineExternFunction("Viper.Input.Pad.IsDown", types::boolean());

    // =========================================================================
    // IO - FILE (VL-017 fixes: align names and return types with runtime)
    // =========================================================================
    defineExternFunction("Viper.IO.File.Exists", types::boolean());
    defineExternFunction("Viper.IO.File.Size", types::integer());
    defineExternFunction("Viper.IO.File.Delete", types::voidType()); // VL-017: was boolean
    defineExternFunction("Viper.IO.File.Copy", types::voidType());   // VL-017: was boolean
    defineExternFunction("Viper.IO.File.Move", types::voidType());   // VL-017: was boolean
    defineExternFunction("Viper.IO.File.ReadAllText", types::string());
    defineExternFunction("Viper.IO.File.WriteAllText", types::voidType());
    defineExternFunction("Viper.IO.File.ReadAllBytes", types::ptr());
    defineExternFunction("Viper.IO.File.WriteAllBytes", types::voidType());
    defineExternFunction("Viper.IO.File.Append", types::voidType());  // VL-017: was AppendText
    defineExternFunction("Viper.IO.File.Modified", types::integer()); // VL-017: was GetModTime
    defineExternFunction("Viper.IO.File.Touch", types::voidType());

    // =========================================================================
    // IO - DIR (VL-016 fixes: align names with runtime)
    // =========================================================================
    defineExternFunction("Viper.IO.Dir.Make", types::voidType()); // VL-016: was Create
    defineExternFunction("Viper.IO.Dir.MakeAll", types::voidType());
    defineExternFunction("Viper.IO.Dir.Remove", types::voidType()); // VL-016: was Delete
    defineExternFunction("Viper.IO.Dir.RemoveAll", types::voidType());
    defineExternFunction("Viper.IO.Dir.Exists", types::boolean());
    // Bug #7 fix: Use proper List[String] type instead of raw ptr
    defineExternFunction("Viper.IO.Dir.List", types::list(types::string()));
    defineExternFunction("Viper.IO.Dir.ListSeq", types::list(types::string()));
    defineExternFunction("Viper.IO.Dir.Files", types::list(types::string()));
    defineExternFunction("Viper.IO.Dir.FilesSeq", types::list(types::string()));
    defineExternFunction("Viper.IO.Dir.Dirs", types::list(types::string()));
    defineExternFunction("Viper.IO.Dir.DirsSeq", types::list(types::string()));
    defineExternFunction("Viper.IO.Dir.Current", types::string()); // VL-016: was GetCurrent
    defineExternFunction("Viper.IO.Dir.SetCurrent", types::voidType());

    // =========================================================================
    // IO - PATH (VL-014, VL-015 fixes: align names with runtime)
    // =========================================================================
    defineExternFunction("Viper.IO.Path.Join", types::string());
    defineExternFunction("Viper.IO.Path.Dir", types::string());  // VL-014: was GetDir
    defineExternFunction("Viper.IO.Path.Name", types::string()); // VL-014: was GetName
    defineExternFunction("Viper.IO.Path.Ext", types::string());  // VL-014: was GetExt
    defineExternFunction("Viper.IO.Path.Stem", types::string()); // VL-014: was GetBase
    defineExternFunction("Viper.IO.Path.Norm", types::string()); // VL-014: was Normalize
    defineExternFunction("Viper.IO.Path.Abs", types::string());  // VL-014: was Absolute
    defineExternFunction("Viper.IO.Path.IsAbs", types::boolean());
    defineExternFunction("Viper.IO.Path.Sep", types::string());
    defineExternFunction("Viper.IO.Path.WithExt", types::string());

    // =========================================================================
    // IO - BINFILE, LINEREADER, LINEWRITER
    // =========================================================================
    defineExternFunction("Viper.IO.BinFile.Open", types::ptr());
    defineExternFunction("Viper.IO.BinFile.Close", types::voidType());
    defineExternFunction("Viper.IO.BinFile.Read", types::ptr());
    defineExternFunction("Viper.IO.BinFile.Write", types::integer());
    defineExternFunction("Viper.IO.BinFile.Seek", types::integer());
    defineExternFunction("Viper.IO.BinFile.Tell", types::integer());
    defineExternFunction("Viper.IO.BinFile.Eof", types::boolean());
    defineExternFunction("Viper.IO.LineReader.Open", types::ptr());
    defineExternFunction("Viper.IO.LineReader.ReadLine", types::string());
    defineExternFunction("Viper.IO.LineReader.Close", types::voidType());
    defineExternFunction("Viper.IO.LineReader.Eof", types::boolean());
    defineExternFunction("Viper.IO.LineWriter.Open", types::ptr());
    defineExternFunction("Viper.IO.LineWriter.WriteLine", types::voidType());
    defineExternFunction("Viper.IO.LineWriter.Close", types::voidType());

    // =========================================================================
    // IO - COMPRESS, ARCHIVE, MEMSTREAM, WATCHER
    // =========================================================================
    defineExternFunction("Viper.IO.Compress.Deflate", types::ptr());
    defineExternFunction("Viper.IO.Compress.Inflate", types::ptr());
    defineExternFunction("Viper.IO.Compress.GzipCompress", types::ptr());
    defineExternFunction("Viper.IO.Compress.GzipDecompress", types::ptr());
    defineExternFunction("Viper.IO.Archive.New", types::ptr());
    defineExternFunction("Viper.IO.Archive.Open", types::ptr());
    defineExternFunction("Viper.IO.Archive.AddFile", types::voidType());
    defineExternFunction("Viper.IO.Archive.AddBytes", types::voidType());
    defineExternFunction("Viper.IO.Archive.ExtractAll", types::voidType());
    defineExternFunction("Viper.IO.Archive.List", types::ptr());
    defineExternFunction("Viper.IO.Archive.Close", types::voidType());
    defineExternFunction("Viper.IO.MemStream.New", types::ptr());
    defineExternFunction("Viper.IO.MemStream.Read", types::ptr());
    defineExternFunction("Viper.IO.MemStream.Write", types::integer());
    defineExternFunction("Viper.IO.MemStream.Seek", types::integer());
    defineExternFunction("Viper.IO.MemStream.ToBytes", types::ptr());
    defineExternFunction("Viper.IO.Watcher.New", types::ptr());
    defineExternFunction("Viper.IO.Watcher.Next", types::ptr());
    defineExternFunction("Viper.IO.Watcher.Close", types::voidType());

    // =========================================================================
    // LOG
    // =========================================================================
    defineExternFunction("Viper.Log.Info", types::voidType());
    defineExternFunction("Viper.Log.Warn", types::voidType());
    defineExternFunction("Viper.Log.Error", types::voidType());
    defineExternFunction("Viper.Log.Debug", types::voidType());

    // =========================================================================
    // MACHINE
    // =========================================================================
    defineExternFunction("Viper.Machine.GetOS", types::string());
    defineExternFunction("Viper.Machine.GetArch", types::string());
    defineExternFunction("Viper.Machine.GetCPUCount", types::integer());
    defineExternFunction("Viper.Machine.GetMemory", types::integer());
    defineExternFunction("Viper.Machine.GetHostname", types::string());
    defineExternFunction("Viper.Machine.GetUsername", types::string());

    // =========================================================================
    // MATH
    // =========================================================================
    defineExternFunction("Viper.Math.Abs", types::number());
    defineExternFunction("Viper.Math.AbsInt", types::integer());
    defineExternFunction("Viper.Math.Acos", types::number());
    defineExternFunction("Viper.Math.Asin", types::number());
    defineExternFunction("Viper.Math.Atan", types::number());
    defineExternFunction("Viper.Math.Atan2", types::number());
    defineExternFunction("Viper.Math.Ceil", types::number());
    defineExternFunction("Viper.Math.Cos", types::number());
    defineExternFunction("Viper.Math.Cosh", types::number());
    defineExternFunction("Viper.Math.Exp", types::number());
    defineExternFunction("Viper.Math.Floor", types::number());
    defineExternFunction("Viper.Math.Log", types::number());
    defineExternFunction("Viper.Math.Log10", types::number());
    defineExternFunction("Viper.Math.Max", types::number());
    defineExternFunction("Viper.Math.MaxInt", types::integer());
    defineExternFunction("Viper.Math.Min", types::number());
    defineExternFunction("Viper.Math.MinInt", types::integer());
    defineExternFunction("Viper.Math.Pow", types::number());
    defineExternFunction("Viper.Math.Randomize", types::voidType());
    defineExternFunction("Viper.Math.Rnd", types::number());
    defineExternFunction("Viper.Math.Round", types::number());
    defineExternFunction("Viper.Math.Sign", types::integer());
    defineExternFunction("Viper.Math.Sin", types::number());
    defineExternFunction("Viper.Math.Sinh", types::number());
    defineExternFunction("Viper.Math.Sqrt", types::number());
    defineExternFunction("Viper.Math.Tan", types::number());
    defineExternFunction("Viper.Math.Tanh", types::number());
    defineExternFunction("Viper.Math.Trunc", types::number());
    defineExternFunction("Viper.Math.Clamp", types::number());
    defineExternFunction("Viper.Math.ClampInt", types::integer());
    defineExternFunction("Viper.Math.Lerp", types::number());

    // =========================================================================
    // NETWORK
    // =========================================================================
    defineExternFunction("Viper.Network.Dns.Lookup", types::string());
    defineExternFunction("Viper.Network.Dns.ReverseLookup", types::string());
    defineExternFunction("Viper.Network.Http.Get", types::string());
    defineExternFunction("Viper.Network.Http.Post", types::string());
    defineExternFunction("Viper.Network.Http.GetBytes", types::ptr());
    defineExternFunction("Viper.Network.Http.PostBytes", types::ptr());
    // TCP Client
    defineExternFunction("Viper.Network.Tcp.Connect", types::ptr());
    defineExternFunction("Viper.Network.Tcp.ConnectFor", types::ptr());
    defineExternFunction("Viper.Network.Tcp.get_Host", types::string());
    defineExternFunction("Viper.Network.Tcp.get_Port", types::integer());
    defineExternFunction("Viper.Network.Tcp.get_LocalPort", types::integer());
    defineExternFunction("Viper.Network.Tcp.get_IsOpen", types::boolean());
    defineExternFunction("Viper.Network.Tcp.get_Available", types::integer());
    defineExternFunction("Viper.Network.Tcp.Send", types::integer());
    defineExternFunction("Viper.Network.Tcp.SendStr", types::integer());
    defineExternFunction("Viper.Network.Tcp.SendAll", types::voidType());
    defineExternFunction("Viper.Network.Tcp.Recv", types::ptr());
    defineExternFunction("Viper.Network.Tcp.RecvStr", types::string());
    defineExternFunction("Viper.Network.Tcp.RecvExact", types::ptr());
    defineExternFunction("Viper.Network.Tcp.RecvLine", types::string());
    defineExternFunction("Viper.Network.Tcp.SetRecvTimeout", types::voidType());
    defineExternFunction("Viper.Network.Tcp.SetSendTimeout", types::voidType());
    defineExternFunction("Viper.Network.Tcp.Close", types::voidType());
    // TCP Server
    defineExternFunction("Viper.Network.TcpServer.Listen", types::ptr());
    defineExternFunction("Viper.Network.TcpServer.ListenAt", types::ptr());
    defineExternFunction("Viper.Network.TcpServer.get_Port", types::integer());
    defineExternFunction("Viper.Network.TcpServer.get_Address", types::string());
    defineExternFunction("Viper.Network.TcpServer.get_IsListening", types::boolean());
    defineExternFunction("Viper.Network.TcpServer.Accept", types::ptr());
    defineExternFunction("Viper.Network.TcpServer.AcceptFor", types::ptr());
    defineExternFunction("Viper.Network.TcpServer.Close", types::voidType());
    defineExternFunction("Viper.Network.Udp.Open", types::ptr());
    defineExternFunction("Viper.Network.Udp.Close", types::voidType());
    defineExternFunction("Viper.Network.Udp.Send", types::integer());
    defineExternFunction("Viper.Network.Udp.Recv", types::ptr());
    defineExternFunction("Viper.Network.Url.Encode", types::string());
    defineExternFunction("Viper.Network.Url.Decode", types::string());
    defineExternFunction("Viper.Network.Url.Parse", types::ptr());

    // =========================================================================
    // PARSE
    // =========================================================================
    defineExternFunction("Viper.Parse.Int", types::integer());
    defineExternFunction("Viper.Parse.Num", types::number());
    defineExternFunction("Viper.Parse.Bool", types::boolean());
    defineExternFunction("Viper.Parse.TryInt", types::boolean());
    defineExternFunction("Viper.Parse.TryNum", types::boolean());

    // =========================================================================
    // RANDOM
    // =========================================================================
    defineExternFunction("Viper.Random.Next", types::number());
    defineExternFunction("Viper.Random.NextInt", types::integer());
    defineExternFunction("Viper.Random.Seed", types::voidType());
    defineExternFunction("Viper.Random.Range", types::integer());
    defineExternFunction("Viper.Random.RangeF", types::number());
    defineExternFunction("Viper.Random.Bool", types::boolean());
    defineExternFunction("Viper.Random.Choice", types::ptr());
    defineExternFunction("Viper.Random.Shuffle", types::voidType());

    // =========================================================================
    // STRING
    // =========================================================================
    defineExternFunction("Viper.String.Concat", types::string());
    defineExternFunction("Viper.String.Length", types::integer());
    defineExternFunction("Viper.String.Substring", types::string());
    defineExternFunction("Viper.String.Left", types::string());
    defineExternFunction("Viper.String.Right", types::string());
    defineExternFunction("Viper.String.Mid", types::string());
    defineExternFunction("Viper.String.IndexOf", types::integer());
    defineExternFunction("Viper.String.IndexOfFrom", types::integer());
    defineExternFunction("Viper.String.Has", types::boolean()); // VL-002: was Contains
    defineExternFunction("Viper.String.StartsWith", types::boolean());
    defineExternFunction("Viper.String.EndsWith", types::boolean());
    defineExternFunction("Viper.String.ToUpper", types::string());
    defineExternFunction("Viper.String.ToLower", types::string());
    defineExternFunction("Viper.String.Trim", types::string());
    defineExternFunction("Viper.String.TrimStart", types::string()); // VL-003: was TrimLeft
    defineExternFunction("Viper.String.TrimEnd", types::string());   // VL-003: was TrimRight
    defineExternFunction("Viper.String.Replace", types::string());
    defineExternFunction("Viper.String.Split", types::ptr());
    defineExternFunction("Viper.String.Repeat", types::string());
    defineExternFunction("Viper.String.Flip", types::string()); // VL-005: was Reverse
    defineExternFunction("Viper.String.Chr", types::string());
    defineExternFunction("Viper.String.Asc", types::integer());
    defineExternFunction("Viper.String.get_IsEmpty", types::boolean()); // VL-011: was IsEmpty
    defineExternFunction("Viper.String.Cmp", types::integer());         // VL-007: was Compare
    defineExternFunction("Viper.Strings.Join", types::string()); // VL-010: was Viper.String.Join
    defineExternFunction("Viper.Strings.Equals", types::boolean());
    defineExternFunction("Viper.Strings.Compare", types::integer());

    // =========================================================================
    // TERMINAL
    // =========================================================================
    defineExternFunction("Viper.Terminal.Say", types::voidType());
    defineExternFunction("Viper.Terminal.SayInt", types::voidType());
    defineExternFunction("Viper.Terminal.SayNum", types::voidType());
    defineExternFunction("Viper.Terminal.SayBool", types::voidType());
    defineExternFunction("Viper.Terminal.Print", types::voidType());
    defineExternFunction("Viper.Terminal.PrintInt", types::voidType());
    defineExternFunction("Viper.Terminal.PrintNum", types::voidType());
    defineExternFunction("Viper.Terminal.ReadLine", types::string());
    defineExternFunction("Viper.Terminal.GetKey", types::string());
    defineExternFunction("Viper.Terminal.InKey", types::string());
    defineExternFunction("Viper.Terminal.GetKeyTimeout", types::string());
    defineExternFunction("Viper.Terminal.Clear", types::voidType());
    defineExternFunction("Viper.Terminal.Bell", types::voidType());
    defineExternFunction("Viper.Terminal.Flush", types::voidType());
    defineExternFunction("Viper.Terminal.SetColor", types::voidType());
    defineExternFunction("Viper.Terminal.SetPosition", types::voidType());
    defineExternFunction("Viper.Terminal.SetCursorVisible", types::voidType());
    defineExternFunction("Viper.Terminal.SetAltScreen", types::voidType());
    defineExternFunction("Viper.Terminal.BeginBatch", types::voidType());
    defineExternFunction("Viper.Terminal.EndBatch", types::voidType());
    defineExternFunction("Viper.Terminal.GetWidth", types::integer());
    defineExternFunction("Viper.Terminal.GetHeight", types::integer());

    // =========================================================================
    // TEXT - CODEC, CSV, GUID, STRINGBUILDER, TEMPLATE, PATTERN
    // =========================================================================
    defineExternFunction("Viper.Text.Codec.EncodeBase64", types::string());
    defineExternFunction("Viper.Text.Codec.DecodeBase64", types::ptr());
    defineExternFunction("Viper.Text.Codec.EncodeHex", types::string());
    defineExternFunction("Viper.Text.Codec.DecodeHex", types::ptr());
    defineExternFunction("Viper.Text.Codec.EncodeUrl", types::string());
    defineExternFunction("Viper.Text.Codec.DecodeUrl", types::string());
    defineExternFunction("Viper.Text.Csv.Parse", types::ptr());
    defineExternFunction("Viper.Text.Csv.ParseFile", types::ptr());
    defineExternFunction("Viper.Text.Csv.Write", types::string());
    defineExternFunction("Viper.Text.Guid.New", types::string());
    defineExternFunction("Viper.Text.Guid.Parse", types::string());
    defineExternFunction("Viper.Text.StringBuilder.New", types::ptr());
    defineExternFunction("Viper.Text.StringBuilder.Append", types::voidType());
    defineExternFunction("Viper.Text.StringBuilder.AppendLine", types::voidType());
    defineExternFunction("Viper.Text.StringBuilder.ToString", types::string());
    defineExternFunction("Viper.Text.StringBuilder.Clear", types::voidType());
    defineExternFunction("Viper.Text.StringBuilder.get_Length", types::integer());
    defineExternFunction("Viper.Text.Template.New", types::ptr());
    defineExternFunction("Viper.Text.Template.Set", types::voidType());
    defineExternFunction("Viper.Text.Template.Render", types::string());
    defineExternFunction("Viper.Text.Pattern.New", types::ptr());
    defineExternFunction("Viper.Text.Pattern.Match", types::boolean());
    defineExternFunction("Viper.Text.Pattern.FindAll", types::ptr());
    defineExternFunction("Viper.Text.Pattern.Replace", types::string());

    // =========================================================================
    // THREADS
    // =========================================================================
    defineExternFunction("Viper.Threads.Thread.New", types::ptr());
    defineExternFunction("Viper.Threads.Thread.Start", types::voidType());
    defineExternFunction("Viper.Threads.Thread.Join", types::voidType());
    defineExternFunction("Viper.Threads.Thread.IsAlive", types::boolean());
    defineExternFunction("Viper.Threads.Thread.Sleep", types::voidType());
    defineExternFunction("Viper.Threads.Thread.Yield", types::voidType());
    defineExternFunction("Viper.Threads.Thread.GetId", types::integer());
    defineExternFunction("Viper.Threads.Barrier.New", types::ptr());
    defineExternFunction("Viper.Threads.Barrier.Wait", types::voidType());
    defineExternFunction("Viper.Threads.Gate.New", types::ptr());
    defineExternFunction("Viper.Threads.Gate.Open", types::voidType());
    defineExternFunction("Viper.Threads.Gate.Close", types::voidType());
    defineExternFunction("Viper.Threads.Gate.Wait", types::voidType());
    defineExternFunction("Viper.Threads.Monitor.New", types::ptr());
    defineExternFunction("Viper.Threads.Monitor.Enter", types::voidType());
    defineExternFunction("Viper.Threads.Monitor.Exit", types::voidType());
    defineExternFunction("Viper.Threads.Monitor.Wait", types::voidType());
    defineExternFunction("Viper.Threads.Monitor.Notify", types::voidType());
    defineExternFunction("Viper.Threads.Monitor.NotifyAll", types::voidType());
    defineExternFunction("Viper.Threads.Semaphore.New", types::ptr());
    defineExternFunction("Viper.Threads.Semaphore.Wait", types::voidType());
    defineExternFunction("Viper.Threads.Semaphore.Signal", types::voidType());

    // =========================================================================
    // TIME
    // =========================================================================
    defineExternFunction("Viper.Time.Clock.Now", types::integer());
    defineExternFunction("Viper.Time.Clock.Millis", types::integer());
    defineExternFunction("Viper.Time.Clock.Micros", types::integer());
    defineExternFunction("Viper.Time.Clock.Sleep", types::voidType());
    defineExternFunction("Viper.Time.SleepMs", types::voidType());
    defineExternFunction("Viper.Countdown.New", types::ptr());
    defineExternFunction("Viper.Countdown.Tick", types::integer());
    defineExternFunction("Viper.Countdown.Reset", types::voidType());
    defineExternFunction("Viper.Countdown.Expired", types::boolean());

    // =========================================================================
    // VEC2, VEC3 (2D/3D VECTORS)
    // =========================================================================
    defineExternFunction("Viper.Vec2.New", types::ptr());
    defineExternFunction("Viper.Vec2.Add", types::ptr());
    defineExternFunction("Viper.Vec2.Sub", types::ptr());
    defineExternFunction("Viper.Vec2.Mul", types::ptr());
    defineExternFunction("Viper.Vec2.Div", types::ptr());
    defineExternFunction("Viper.Vec2.Dot", types::number());
    defineExternFunction("Viper.Vec2.Length", types::number());
    defineExternFunction("Viper.Vec2.Normalize", types::ptr());
    defineExternFunction("Viper.Vec2.Distance", types::number());
    defineExternFunction("Viper.Vec2.Lerp", types::ptr());
    defineExternFunction("Viper.Vec3.New", types::ptr());
    defineExternFunction("Viper.Vec3.Add", types::ptr());
    defineExternFunction("Viper.Vec3.Sub", types::ptr());
    defineExternFunction("Viper.Vec3.Mul", types::ptr());
    defineExternFunction("Viper.Vec3.Div", types::ptr());
    defineExternFunction("Viper.Vec3.Dot", types::number());
    defineExternFunction("Viper.Vec3.Cross", types::ptr());
    defineExternFunction("Viper.Vec3.Length", types::number());
    defineExternFunction("Viper.Vec3.Normalize", types::ptr());
    defineExternFunction("Viper.Vec3.Distance", types::number());
    defineExternFunction("Viper.Vec3.Lerp", types::ptr());

    // =========================================================================
    // CONVERT
    // =========================================================================
    defineExternFunction("Viper.Convert.IntToStr", types::string());
    defineExternFunction("Viper.Convert.NumToStr", types::string());
    defineExternFunction("Viper.Convert.BoolToStr", types::string());
    defineExternFunction("Viper.Convert.StrToInt", types::integer());
    defineExternFunction("Viper.Convert.StrToNum", types::number());
    defineExternFunction("Viper.Convert.StrToBool", types::boolean());
    defineExternFunction("Viper.Convert.NumToInt", types::integer());

    // =========================================================================
    // GUI - APP
    // =========================================================================
    defineExternFunction("Viper.GUI.App.New", types::runtimeClass("Viper.GUI.App"));
    defineExternFunction("Viper.GUI.App.Destroy", types::voidType());
    defineExternFunction("Viper.GUI.App.get_ShouldClose", types::integer());
    defineExternFunction("Viper.GUI.App.Poll", types::voidType());
    defineExternFunction("Viper.GUI.App.Render", types::voidType());
    defineExternFunction("Viper.GUI.App.get_Root", types::runtimeClass("Viper.GUI.Widget"));
    defineExternFunction("Viper.GUI.App.SetFont", types::voidType());

    // =========================================================================
    // GUI - FONT
    // =========================================================================
    defineExternFunction("Viper.GUI.Font.Load", types::runtimeClass("Viper.GUI.Font"));
    defineExternFunction("Viper.GUI.Font.Destroy", types::voidType());

    // =========================================================================
    // GUI - WIDGET (base class for all widgets)
    // =========================================================================
    defineExternFunction("Viper.GUI.Widget.Destroy", types::voidType());
    defineExternFunction("Viper.GUI.Widget.SetVisible", types::voidType());
    defineExternFunction("Viper.GUI.Widget.SetEnabled", types::voidType());
    defineExternFunction("Viper.GUI.Widget.SetSize", types::voidType());
    defineExternFunction("Viper.GUI.Widget.AddChild", types::voidType());
    defineExternFunction("Viper.GUI.Widget.IsHovered", types::integer());
    defineExternFunction("Viper.GUI.Widget.IsPressed", types::integer());
    defineExternFunction("Viper.GUI.Widget.IsFocused", types::integer());
    defineExternFunction("Viper.GUI.Widget.WasClicked", types::integer());
    defineExternFunction("Viper.GUI.Widget.SetPosition", types::voidType());

    // =========================================================================
    // GUI - LABEL
    // =========================================================================
    defineExternFunction("Viper.GUI.Label.New", types::runtimeClass("Viper.GUI.Label"));
    defineExternFunction("Viper.GUI.Label.SetText", types::voidType());
    defineExternFunction("Viper.GUI.Label.SetFont", types::voidType());
    defineExternFunction("Viper.GUI.Label.SetColor", types::voidType());

    // =========================================================================
    // GUI - BUTTON
    // =========================================================================
    defineExternFunction("Viper.GUI.Button.New", types::runtimeClass("Viper.GUI.Button"));
    defineExternFunction("Viper.GUI.Button.SetText", types::voidType());
    defineExternFunction("Viper.GUI.Button.SetFont", types::voidType());
    defineExternFunction("Viper.GUI.Button.SetStyle", types::voidType());
    defineExternFunction("Viper.GUI.Button.WasClicked", types::integer());

    // =========================================================================
    // GUI - TEXTINPUT
    // =========================================================================
    defineExternFunction("Viper.GUI.TextInput.New", types::runtimeClass("Viper.GUI.TextInput"));
    defineExternFunction("Viper.GUI.TextInput.SetText", types::voidType());
    defineExternFunction("Viper.GUI.TextInput.get_Text", types::string());
    defineExternFunction("Viper.GUI.TextInput.SetPlaceholder", types::voidType());
    defineExternFunction("Viper.GUI.TextInput.SetFont", types::voidType());
    defineExternFunction("Viper.GUI.TextInput.SetMaxLength", types::voidType());

    // =========================================================================
    // GUI - CHECKBOX
    // =========================================================================
    defineExternFunction("Viper.GUI.Checkbox.New", types::runtimeClass("Viper.GUI.Checkbox"));
    defineExternFunction("Viper.GUI.Checkbox.IsChecked", types::integer());
    defineExternFunction("Viper.GUI.Checkbox.SetChecked", types::voidType());

    // =========================================================================
    // GUI - CODEEDITOR
    // =========================================================================
    defineExternFunction("Viper.GUI.CodeEditor.New", types::runtimeClass("Viper.GUI.CodeEditor"));
    defineExternFunction("Viper.GUI.CodeEditor.SetText", types::voidType());
    defineExternFunction("Viper.GUI.CodeEditor.get_Text", types::string());
    defineExternFunction("Viper.GUI.CodeEditor.SetFont", types::voidType());
    defineExternFunction("Viper.GUI.CodeEditor.get_LineCount", types::integer());
    defineExternFunction("Viper.GUI.CodeEditor.get_CursorLine", types::integer());
    defineExternFunction("Viper.GUI.CodeEditor.get_CursorColumn", types::integer());
    defineExternFunction("Viper.GUI.CodeEditor.IsModified", types::integer());
    defineExternFunction("Viper.GUI.CodeEditor.ClearModified", types::voidType());

    // =========================================================================
    // GUI - THEME
    // =========================================================================
    defineExternFunction("Viper.GUI.Theme.SetDark", types::voidType());
    defineExternFunction("Viper.GUI.Theme.SetLight", types::voidType());

    // =========================================================================
    // GUI - VBOX / HBOX
    // =========================================================================
    defineExternFunction("Viper.GUI.VBox.New", types::runtimeClass("Viper.GUI.VBox"));
    defineExternFunction("Viper.GUI.VBox.SetSpacing", types::voidType());
    defineExternFunction("Viper.GUI.VBox.SetPadding", types::voidType());
    defineExternFunction("Viper.GUI.HBox.New", types::runtimeClass("Viper.GUI.HBox"));
    defineExternFunction("Viper.GUI.HBox.SetSpacing", types::voidType());
    defineExternFunction("Viper.GUI.HBox.SetPadding", types::voidType());
}

} // namespace il::frontends::viperlang
