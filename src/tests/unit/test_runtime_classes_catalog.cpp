//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_runtime_classes_catalog.cpp
// Purpose: Smoke test for runtime class catalog exposing Viper.String.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/il/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/classes/RuntimeClasses.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace {

const il::runtime::RuntimeClass *findClass(std::string_view qname) {
    for (const auto &cls : il::runtime::runtimeClassCatalog()) {
        if (std::string_view(cls.qname) == qname)
            return &cls;
    }
    return nullptr;
}

bool hasMethod(const il::runtime::RuntimeClass &cls,
               std::string_view name,
               std::string_view signature) {
    for (const auto &method : cls.methods) {
        if (std::string_view(method.name) == name &&
            std::string_view(method.signature) == signature)
            return true;
    }
    return false;
}

bool hasProperty(const il::runtime::RuntimeClass &cls,
                 std::string_view name,
                 std::string_view type) {
    for (const auto &prop : cls.properties) {
        if (std::string_view(prop.name) == name && std::string_view(prop.type) == type)
            return true;
    }
    return false;
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

/// @brief Assert that a static typed-constant class exposes exactly the requested i64 properties.
/// @details Exact property counts catch both missing constants and accidental unrelated members;
///          per-name checks also validate the public scalar type used by both language frontends.
/// @param qname Fully qualified runtime class name.
/// @param names Complete ordered-independent set of expected read-only property names.
void assertI64ConstantClass(std::string_view qname, std::initializer_list<std::string_view> names) {
    const il::runtime::RuntimeClass *cls = findClass(qname);
    assert(cls != nullptr && "typed GUI constant class missing from runtime catalog");
    assert(cls->properties.size() == names.size());
    assert(cls->methods.empty());
    for (std::string_view name : names)
        assert(hasProperty(*cls, name, "i64"));
}

} // namespace

int main() {
    const auto &cat = il::runtime::runtimeClassCatalog();
    assert(cat.size() >= 1);

    for (const auto &cls : cat) {
        assert(cls.summary != nullptr && *cls.summary != '\0' &&
               "runtime classes must have authored summaries");
        assert(cls.details != nullptr && *cls.details != '\0' &&
               "runtime classes must have authored details");
        for (const auto &method : cls.methods) {
            const std::string_view name(method.name);
            assert(!startsWith(name, "get_") &&
                   "accessors must be runtime properties, not methods");
            assert(!startsWith(name, "set_") &&
                   "accessors must be runtime properties, not methods");
        }
    }

    // Find Viper.String in the catalog (order-independent)
    const il::runtime::RuntimeClass *stringCls = findClass("Viper.String");

    assert(stringCls != nullptr && "Viper.String not found in catalog");
    assert(std::string_view(stringCls->summary) ==
           "Provides immutable runtime string values and common text operations.");
    assert(std::string_view(stringCls->details).find("`Viper.String`") != std::string_view::npos);
    assert(stringCls->properties.size() >= 2);

    // Find Length and IsEmpty properties (order-independent)
    bool hasLength = false, hasIsEmpty = false;
    for (const auto &prop : stringCls->properties) {
        if (std::string_view(prop.name) == std::string_view("Length"))
            hasLength = true;
        if (std::string_view(prop.name) == std::string_view("IsEmpty"))
            hasIsEmpty = true;
    }
    assert(hasLength && "Viper.String should have Length property");
    assert(hasIsEmpty && "Viper.String should have IsEmpty property");

    const il::runtime::RuntimeClass *weakRefCls = findClass("Viper.Memory.WeakRef");
    assert(weakRefCls != nullptr && "Viper.Memory.WeakRef not found in catalog");
    assert(hasMethod(*weakRefCls, "New", "obj<Viper.Memory.WeakRef>(obj)"));
    assert(hasMethod(*weakRefCls, "Get", "obj(obj)"));
    assert(hasMethod(*weakRefCls, "Free", "void(obj)"));
    assert(hasMethod(*weakRefCls, "Reset", "void(obj,obj)"));

    // Viper.Core.ValueType was removed from the public catalog; value-type
    // registration lives only under Viper.Runtime.Unsafe.
    assert(findClass("Viper.Core.ValueType") == nullptr &&
           "Viper.Core.ValueType must not be published");

    const il::runtime::RuntimeClass *systemClipboardCls = findClass("Viper.System.Clipboard");
    assert(systemClipboardCls != nullptr && "Viper.System.Clipboard not found in catalog");
    assert(hasMethod(*systemClipboardCls, "Get", "str()"));
    assert(hasMethod(*systemClipboardCls, "Set", "void(str)"));
    assert(hasMethod(*systemClipboardCls, "HasText", "i1()"));

    const il::runtime::RuntimeClass *cameraCls = findClass("Viper.Graphics.Camera");
    assert(cameraCls != nullptr && "Viper.Graphics.Camera not found in catalog");
    assert(hasMethod(*cameraCls, "SmoothFollow", "void(i64,i64,i64)"));
    assert(hasMethod(*cameraCls, "SetDeadzone", "void(i64,i64)"));

    const il::runtime::RuntimeClass *bitmapFontCls = findClass("Viper.Graphics.BitmapFont");
    assert(bitmapFontCls != nullptr && "Viper.Graphics.BitmapFont not found in catalog");
    assert(hasMethod(*bitmapFontCls, "LoadBdf", "obj<Viper.Graphics.BitmapFont>(str)"));
    assert(hasMethod(*bitmapFontCls, "LoadPsf", "obj<Viper.Graphics.BitmapFont>(str)"));

    const il::runtime::RuntimeClass *spriteFontCls = findClass("Viper.Graphics.BitmapFont");
    assert(spriteFontCls != nullptr && "Viper.Graphics.BitmapFont not found in catalog");
    assert(hasMethod(*spriteFontCls, "LoadBdf", "obj<Viper.Graphics.BitmapFont>(str)"));
    assert(hasMethod(*spriteFontCls, "LoadPsf", "obj<Viper.Graphics.BitmapFont>(str)"));

    const il::runtime::RuntimeClass *guiAppCls = findClass("Viper.GUI.App");
    assert(guiAppCls != nullptr && "Viper.GUI.App not found in catalog");
    assert(hasMethod(*guiAppCls, "TryNew", "obj<Viper.Result>(str,i64,i64)"));
    assert(hasMethod(*guiAppCls, "WasFileDropped", "i1()"));
    assert(hasMethod(*guiAppCls, "GetDroppedFileCount", "i64()"));
    assert(hasMethod(*guiAppCls, "GetDroppedFile", "str(i64)"));

    const il::runtime::RuntimeClass *guiSystemCls = findClass("Viper.GUI.System");
    assert(guiSystemCls != nullptr && "Viper.GUI.System not found in catalog");
    assert(hasMethod(*guiSystemCls, "IsAvailable", "i1()"));
    assert(hasMethod(*guiSystemCls, "GetUnavailableReason", "str()"));

    const il::runtime::RuntimeClass *guiWidgetCls = findClass("Viper.GUI.Widget");
    assert(guiWidgetCls != nullptr && "Viper.GUI.Widget not found in catalog");
    assert(hasMethod(*guiWidgetCls, "SetTooltip", "void(str)"));
    assert(hasMethod(*guiWidgetCls, "SetDraggable", "void(i1)"));
    assert(hasMethod(*guiWidgetCls, "SetDragData", "void(str,str)"));
    assert(hasMethod(*guiWidgetCls, "WasDropped", "i1()"));
    assert(hasMethod(*guiWidgetCls, "GetDropData", "str()"));

    const il::runtime::RuntimeClass *guiCodeEditorCls = findClass("Viper.GUI.CodeEditor");
    assert(guiCodeEditorCls != nullptr && "Viper.GUI.CodeEditor not found in catalog");
    assert(hasProperty(*guiCodeEditorCls, "Revision", "i64"));
    assert(hasMethod(*guiCodeEditorCls, "GetGutterClickSlot", "i64()"));
    assert(hasMethod(*guiCodeEditorCls, "GetShowLineNumbers", "i64()"));
    assert(hasMethod(*guiCodeEditorCls, "GetPerfStats", "obj<Viper.Collections.Map>()"));

    const il::runtime::RuntimeClass *guiTestHarnessCls = findClass("Viper.GUI.TestHarness");
    assert(guiTestHarnessCls != nullptr && "Viper.GUI.TestHarness not found in catalog");
    assert(hasMethod(*guiTestHarnessCls, "AssertNonBlank", "i1(obj)"));
    assert(hasMethod(*guiTestHarnessCls, "BindApp", "i1(obj)"));
    assert(hasMethod(*guiTestHarnessCls, "UnbindApp", "void()"));
    assert(hasMethod(*guiTestHarnessCls, "DispatchPending", "i64()"));
    assert(hasMethod(*guiTestHarnessCls, "RenderFrame", "i1(f64)"));
    assert(hasMethod(
        *guiTestHarnessCls, "CapturePixels", "obj<Viper.Graphics.Pixels>(i64,i64,i64,i64)"));
    assert(hasMethod(*guiTestHarnessCls, "CaptureHash", "str(i64,i64,i64,i64)"));
    assert(hasMethod(
        *guiTestHarnessCls, "CompareRegion", "obj<Viper.Collections.Map>(obj,i64,i64,i64)"));
    assert(
        hasMethod(*guiTestHarnessCls, "GetAccessibilitySnapshot", "obj<Viper.Collections.Map>()"));

    assertI64ConstantClass("Viper.GUI.Align", {"Start", "Center", "End", "Stretch"});
    assertI64ConstantClass(
        "Viper.GUI.Justify",
        {"Start", "Center", "End", "SpaceBetween", "SpaceAround", "SpaceEvenly"});
    assertI64ConstantClass("Viper.GUI.FlexDirection",
                           {"Row", "Column", "RowReverse", "ColumnReverse"});
    assertI64ConstantClass("Viper.GUI.FlexWrap", {"NoWrap", "Wrap", "WrapReverse"});
    assertI64ConstantClass("Viper.GUI.Dock", {"Left", "Top", "Right", "Bottom", "Fill"});
    assertI64ConstantClass("Viper.GUI.ThemeMode", {"Dark", "Light", "System", "Custom"});
    assertI64ConstantClass(
        "Viper.GUI.AccessibleRole",
        {"None",        "Application", "Window",    "Group",    "Label",    "Button",   "CheckBox",
         "RadioButton", "TextBox",     "SearchBox", "ComboBox", "List",     "ListItem", "Tree",
         "TreeItem",    "TabList",     "Tab",       "Table",    "Row",      "Cell",     "Slider",
         "ProgressBar", "Dialog",      "Alert",     "Menu",     "MenuItem", "ToolBar",  "StatusBar",
         "Image",       "Video",       "Link"});
    assertI64ConstantClass("Viper.GUI.LiveRegionMode", {"Off", "Polite", "Assertive"});
    assertI64ConstantClass(
        "Viper.GUI.DialogButtonRole",
        {"Normal", "Default", "Cancel", "Destructive", "Accept", "Reject", "Help"});
    assertI64ConstantClass("Viper.GUI.DialogStatus",
                           {"Idle", "Open", "Accepted", "Cancelled", "Failed"});
    assertI64ConstantClass("Viper.GUI.ImageFilter", {"Nearest", "Bilinear"});
    assertI64ConstantClass("Viper.GUI.SortDirection", {"None", "Ascending", "Descending"});

    const il::runtime::RuntimeClass *guiToastCls = findClass("Viper.GUI.Toast");
    assert(guiToastCls != nullptr && "Viper.GUI.Toast not found in catalog");
    assert(hasMethod(*guiToastCls, "New", "obj(str,i64,i64)"));

    const il::runtime::RuntimeClass *guiFontCls = findClass("Viper.GUI.Font");
    assert(guiFontCls != nullptr && "Viper.GUI.Font not found in catalog");
    assert(hasMethod(*guiFontCls, "Load", "obj(str)"));

    const il::runtime::RuntimeClass *guiTreeViewCls = findClass("Viper.GUI.TreeView");
    assert(guiTreeViewCls != nullptr && "Viper.GUI.TreeView not found in catalog");
    assert(hasMethod(*guiTreeViewCls, "GetNodeAt", "obj(i64,i64)"));
    assert(hasMethod(*guiTreeViewCls, "SetVirtualModel", "i1(obj)"));
    assert(hasMethod(*guiTreeViewCls, "ClearVirtualModel", "void()"));

    const il::runtime::RuntimeClass *guiListBoxCls = findClass("Viper.GUI.ListBox");
    assert(guiListBoxCls != nullptr && "Viper.GUI.ListBox not found in catalog");
    assert(hasMethod(*guiListBoxCls, "SetVirtualModel", "i1(obj)"));
    assert(hasMethod(*guiListBoxCls, "GetVisibleFirst", "i64()"));
    assert(hasMethod(*guiListBoxCls, "GetVisibleCount", "i64()"));

    const il::runtime::RuntimeClass *guiVirtualListCls = findClass("Viper.GUI.VirtualList");
    assert(guiVirtualListCls != nullptr && "Viper.GUI.VirtualList not found in catalog");
    assert(hasMethod(*guiVirtualListCls, "SetRowText", "void(i64,str)"));
    assert(hasMethod(*guiVirtualListCls, "Bind", "i1(obj)"));

    const il::runtime::RuntimeClass *guiVirtualTreeCls = findClass("Viper.GUI.VirtualTree");
    assert(guiVirtualTreeCls != nullptr && "Viper.GUI.VirtualTree not found in catalog");
    assert(
        hasMethod(*guiVirtualTreeCls, "VisibleRowsRange", "obj<Viper.Collections.Seq>(i64,i64)"));
    assert(hasMethod(*guiVirtualTreeCls, "MoveNode", "i1(str,str)"));
    assert(hasMethod(*guiVirtualTreeCls, "Bind", "i1(obj)"));

    const il::runtime::RuntimeClass *guiColorSwatchCls = findClass("Viper.GUI.ColorSwatch");
    assert(guiColorSwatchCls != nullptr && "Viper.GUI.ColorSwatch not found in catalog");
    assert(hasMethod(*guiColorSwatchCls, "SetColor", "void(i64)"));
    assert(hasMethod(*guiColorSwatchCls, "WasChanged", "i1()"));

    const il::runtime::RuntimeClass *guiColorPaletteCls = findClass("Viper.GUI.ColorPalette");
    assert(guiColorPaletteCls != nullptr && "Viper.GUI.ColorPalette not found in catalog");
    assert(hasMethod(*guiColorPaletteCls, "RemoveColor", "i1(i64)"));
    assert(hasMethod(*guiColorPaletteCls, "GetColorAt", "i64(i64)"));

    const il::runtime::RuntimeClass *guiColorPickerCls = findClass("Viper.GUI.ColorPicker");
    assert(guiColorPickerCls != nullptr && "Viper.GUI.ColorPicker not found in catalog");
    assert(hasMethod(*guiColorPickerCls, "SetAlphaEnabled", "void(i1)"));
    assert(hasMethod(*guiColorPickerCls, "GetAlpha", "i64()"));

    const il::runtime::RuntimeClass *guiGridCls = findClass("Viper.GUI.Grid");
    assert(guiGridCls != nullptr && "Viper.GUI.Grid not found in catalog");
    assert(hasMethod(*guiGridCls, "SetVirtualCell", "void(i64,i64,str)"));
    assert(hasMethod(*guiGridCls, "SelectCell", "i1(i64,i64)"));
    assert(hasMethod(*guiGridCls, "CommitEdit", "i1(str)"));
    assert(hasMethod(*guiGridCls, "WasColumnResized", "i1()"));

    const il::runtime::RuntimeClass *guiMessageBoxCls = findClass("Viper.GUI.MessageBox");
    assert(guiMessageBoxCls != nullptr && "Viper.GUI.MessageBox not found in catalog");
    assert(hasMethod(*guiMessageBoxCls, "New", "obj(str,str,i64)"));
    assert(hasMethod(*guiMessageBoxCls, "PromptOption", "obj<Viper.Option>(str,str)"));
    assert(hasMethod(*guiMessageBoxCls, "AddButtonWithRole", "void(str,i64,i64)"));
    assert(hasMethod(*guiMessageBoxCls, "SetCancelButton", "i1(i64)"));
    assert(hasMethod(*guiMessageBoxCls, "ShowAsync", "i1()"));
    assert(hasMethod(*guiMessageBoxCls, "GetStatus", "i64()"));

    const il::runtime::RuntimeClass *guiFileDialogCls = findClass("Viper.GUI.FileDialog");
    assert(guiFileDialogCls != nullptr && "Viper.GUI.FileDialog not found in catalog");
    assert(hasMethod(*guiFileDialogCls, "New", "obj(i64)"));
    assert(hasMethod(*guiFileDialogCls, "OpenOption", "obj<Viper.Option>(str,str,str)"));
    assert(
        hasMethod(*guiFileDialogCls, "OpenMultipleSeq", "obj<Viper.Collections.Seq>(str,str,str)"));
    assert(hasMethod(*guiFileDialogCls, "SetShowHidden", "void(i1)"));
    assert(hasMethod(*guiFileDialogCls, "ShowAsync", "i1()"));
    assert(hasMethod(*guiFileDialogCls, "GetPaths", "obj<Viper.Collections.Seq>()"));

    const il::runtime::RuntimeClass *guiToolbarCls = findClass("Viper.GUI.Toolbar");
    assert(guiToolbarCls != nullptr && "Viper.GUI.Toolbar not found in catalog");
    assert(hasMethod(*guiToolbarCls, "NewVertical", "obj(obj)"));

    constexpr std::array<std::string_view, 43> graphics2DClasses = {
        "Viper.Graphics.RenderTarget2D",
        "Viper.Graphics.RenderTarget2D",
        "Viper.Graphics.Texture2D",
        "Viper.Graphics.GpuTexture2D",
        "Viper.Graphics.Renderer2D",
        "Viper.Graphics.Material2D",
        "Viper.Graphics.Shader2D",
        "Viper.Graphics.PostProcess2D",
        "Viper.Graphics.Viewport2D",
        "Viper.Graphics.Viewport2D",
        "Viper.Graphics.TileSet2D",
        "Viper.Graphics.TileLayer2D",
        "Viper.Graphics.ObjectLayer2D",
        "Viper.Graphics.AutoTile2D",
        "Viper.Graphics.Path2D",
        "Viper.Graphics.ShapeRenderer2D",
        "Viper.Graphics.TextRenderer2D",
        "Viper.Graphics.BitmapFont",
        "Viper.Graphics.SdfFont",
        "Viper.Graphics.NineSlice2D",
        "Viper.Game.ParticleEmitter",
        "Viper.Game.ParticleEmitter",
        "Viper.Graphics.DebugDraw2D",
        "Viper.Graphics.Transform2D",
        "Viper.Graphics.Sampler2D",
        "Viper.Graphics.BlendState2D",
        "Viper.Graphics.SpriteRenderer2D",
        "Viper.Graphics2D.TilemapRenderer2D",
        "Viper.Graphics.TileChunkCache2D",
        "Viper.Graphics.AnimationClip2D",
        "Viper.Graphics.AnimatedSprite2D",
        "Viper.Graphics.TextLayout2D",
        "Viper.Graphics.BitmapFont",
        "Viper.Graphics.RenderPass2D",
        "Viper.Graphics.RenderGraph2D",
        "Viper.Graphics.CollisionMask2D",
        "Viper.Graphics.Hitbox2D",
        "Viper.Graphics.Palette2D",
        "Viper.Graphics.Gradient2D",
        "Viper.Graphics.CameraRig2D",
        "Viper.Graphics.TexturePackerAtlas",
        "Viper.Graphics.AsepriteImporter",
        "Viper.Graphics.TiledMapLoader",
    };

    for (std::string_view qname : graphics2DClasses) {
        bool found = false;
        for (const auto &cls : cat) {
            if (std::string_view(cls.qname) == qname) {
                found = true;
                break;
            }
        }
        assert(found && "Graphics2D runtime class missing from catalog");
    }

    return 0;
}
