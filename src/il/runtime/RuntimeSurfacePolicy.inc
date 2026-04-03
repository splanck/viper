//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Runtime surface policy for frontend-visible Viper.* APIs.
//
// This file is consumed by audit tests. Keep it focused on deliberate language
// surface guarantees and intentionally-internal headers that should not be
// inferred as frontend API.
//
// Available macros:
//   RUNTIME_SURFACE_INTERNAL_HEADER(path)
//   RUNTIME_SURFACE_EXPECT_FUNCTION(canonical, symbol)
//   RUNTIME_SURFACE_EXPECT_METHOD(class_name, method_name, signature)
//   RUNTIME_SURFACE_EXPECT_PROPERTY(class_name, property_name, type_name)
//
//===----------------------------------------------------------------------===//

RUNTIME_SURFACE_INTERNAL_HEADER("src/runtime/network/rt_crypto.h")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.New", "rt_messagebox_new")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.NewInfo", "rt_messagebox_new_info")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.NewWarning", "rt_messagebox_new_warning")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.NewError", "rt_messagebox_new_error")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.NewQuestion", "rt_messagebox_new_question")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.AddButton", "rt_messagebox_add_button")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.SetDefaultButton",
                                "rt_messagebox_set_default_button")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.Show", "rt_messagebox_show")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.MessageBox.Destroy", "rt_messagebox_destroy")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.New", "rt_filedialog_new")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.NewOpen", "rt_filedialog_new_open")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.NewSave", "rt_filedialog_new_save")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.NewFolder", "rt_filedialog_new_folder")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.SetTitle", "rt_filedialog_set_title")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.SetPath", "rt_filedialog_set_path")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.SetFilter", "rt_filedialog_set_filter")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.AddFilter", "rt_filedialog_add_filter")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.SetDefaultName",
                                "rt_filedialog_set_default_name")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.SetMultiple", "rt_filedialog_set_multiple")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.Show", "rt_filedialog_show")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.GetPath", "rt_filedialog_get_path")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.get_PathCount",
                                "rt_filedialog_get_path_count")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.GetPathAt", "rt_filedialog_get_path_at")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.GUI.FileDialog.Destroy", "rt_filedialog_destroy")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Option.Map", "rt_option_map_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Option.AndThen", "rt_option_and_then_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Option.OrElse", "rt_option_or_else_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Option.Filter", "rt_option_filter_wrapper")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Result.Map", "rt_result_map_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Result.MapErr", "rt_result_map_err_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Result.AndThen", "rt_result_and_then_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Result.OrElse", "rt_result_or_else_wrapper")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Lazy.New", "rt_lazy_new_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Lazy.Map", "rt_lazy_map_wrapper")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Lazy.FlatMap", "rt_lazy_flat_map_wrapper")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.Sprite.DrawTransformed",
                                "rt_sprite_draw_transformed")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.New",
                                "rt_sprite_animator_new")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.Destroy",
                                "rt_sprite_animator_destroy")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.AddClip",
                                "rt_sprite_animator_add_clip_str")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.Play",
                                "rt_sprite_animator_play_str")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.Stop",
                                "rt_sprite_animator_stop")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.Update",
                                "rt_sprite_animator_update")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.get_IsPlaying",
                                "rt_sprite_animator_is_playing")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Graphics.SpriteAnimator.get_Current",
                                "rt_sprite_animator_get_current_str")

RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Bag.FromSeq", "rt_seq_to_bag")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Set.FromSeq", "rt_seq_to_set")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.List.FromSeq", "rt_seq_to_list")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.List.FromSet", "rt_set_to_list")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.List.FromDeque", "rt_deque_to_list")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Queue.FromSeq", "rt_seq_to_queue")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Seq.FromList", "rt_list_to_seq")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Seq.FromSet", "rt_set_to_seq")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Seq.FromDeque", "rt_deque_to_seq")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Seq.FromBag", "rt_bag_to_seq")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Stack.FromSeq", "rt_seq_to_stack")
RUNTIME_SURFACE_EXPECT_FUNCTION("Viper.Collections.Deque.FromSeq", "rt_seq_to_deque")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.MessageBox", "New", "obj(str,str,i64)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.MessageBox", "NewInfo", "obj(str,str)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.MessageBox", "AddButton", "void(str,i64)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.MessageBox", "Show", "i64()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.MessageBox", "Destroy", "void()")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.FileDialog", "New", "obj(i64)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.FileDialog", "NewOpen", "obj()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.FileDialog", "OpenMultiple", "str(str,str,str)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.FileDialog", "GetPath", "str()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.FileDialog", "GetPathAt", "str(i64)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.GUI.FileDialog", "Destroy", "void()")
RUNTIME_SURFACE_EXPECT_PROPERTY("Viper.GUI.FileDialog", "PathCount", "i64")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.Option", "Map", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Option", "AndThen", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Option", "OrElse", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Option", "Filter", "obj(ptr)")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.Result", "Map", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Result", "MapErr", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Result", "AndThen", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Result", "OrElse", "obj(ptr)")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.Lazy", "New", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Lazy", "Map", "obj(ptr)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Lazy", "FlatMap", "obj(ptr)")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.Graphics.Sprite", "DrawTransformed",
                              "void(obj,i64,i64,i64,i64,i64,i64,i64)")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.Graphics.SpriteAnimator", "New", "obj()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Graphics.SpriteAnimator", "AddClip",
                              "i1(str,i64,i64,i64,i64)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Graphics.SpriteAnimator", "Play", "i1(str)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Graphics.SpriteAnimator", "Update", "void(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Graphics.SpriteAnimator", "Destroy", "void()")
RUNTIME_SURFACE_EXPECT_PROPERTY("Viper.Graphics.SpriteAnimator", "IsPlaying", "i1")
RUNTIME_SURFACE_EXPECT_PROPERTY("Viper.Graphics.SpriteAnimator", "Current", "str")

RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.List", "New", "obj()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Bag", "FromSeq", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Map", "New", "obj()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Set", "FromSeq", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Set", "New", "obj()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.List", "FromSeq", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.List", "FromSet", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.List", "FromDeque", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Queue", "FromSeq", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Seq", "FromList", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Seq", "FromSet", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Seq", "FromDeque", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Seq", "FromBag", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Stack", "FromSeq", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Collections.Deque", "FromSeq", "obj(obj)")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Time.Stopwatch", "New", "obj()")
RUNTIME_SURFACE_EXPECT_METHOD("Viper.Time.Stopwatch", "StartNew", "obj()")
