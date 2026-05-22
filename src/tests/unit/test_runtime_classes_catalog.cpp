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
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/runtime/classes/RuntimeClasses.hpp"

#include <array>
#include <cassert>
#include <cstddef>
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
        if (std::string_view(method.name) == name && std::string_view(method.signature) == signature)
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

} // namespace

int main() {
    const auto &cat = il::runtime::runtimeClassCatalog();
    assert(cat.size() >= 1);

    // Find Viper.String in the catalog (order-independent)
    const il::runtime::RuntimeClass *stringCls = findClass("Viper.String");

    assert(stringCls != nullptr && "Viper.String not found in catalog");
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

    const il::runtime::RuntimeClass *valueTypeCls = findClass("Viper.Core.ValueType");
    assert(valueTypeCls != nullptr && "Viper.Core.ValueType not found in catalog");
    assert(hasMethod(*valueTypeCls, "AddField", "void(i64,i64,i1)"));

    const il::runtime::RuntimeClass *systemClipboardCls =
        findClass("Viper.System.Clipboard");
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
    assert(hasMethod(*bitmapFontCls, "LoadBDF", "obj<Viper.Graphics.BitmapFont>(str)"));
    assert(hasMethod(*bitmapFontCls, "LoadPSF", "obj<Viper.Graphics.BitmapFont>(str)"));

    const il::runtime::RuntimeClass *spriteFontCls = findClass("Viper.Graphics.SpriteFont");
    assert(spriteFontCls != nullptr && "Viper.Graphics.SpriteFont not found in catalog");
    assert(hasMethod(*spriteFontCls, "LoadBDF", "obj<Viper.Graphics.SpriteFont>(str)"));
    assert(hasMethod(*spriteFontCls, "LoadPSF", "obj<Viper.Graphics.SpriteFont>(str)"));

    const il::runtime::RuntimeClass *guiAppCls = findClass("Viper.GUI.App");
    assert(guiAppCls != nullptr && "Viper.GUI.App not found in catalog");
    assert(hasMethod(*guiAppCls, "WasFileDropped", "i64()"));
    assert(hasMethod(*guiAppCls, "GetDroppedFileCount", "i64()"));
    assert(hasMethod(*guiAppCls, "GetDroppedFile", "str(i64)"));

    const il::runtime::RuntimeClass *guiWidgetCls = findClass("Viper.GUI.Widget");
    assert(guiWidgetCls != nullptr && "Viper.GUI.Widget not found in catalog");
    assert(hasMethod(*guiWidgetCls, "SetTooltip", "void(str)"));
    assert(hasMethod(*guiWidgetCls, "SetDraggable", "void(i64)"));
    assert(hasMethod(*guiWidgetCls, "SetDragData", "void(str,str)"));
    assert(hasMethod(*guiWidgetCls, "WasDropped", "i64()"));
    assert(hasMethod(*guiWidgetCls, "GetDropData", "str()"));

    const il::runtime::RuntimeClass *guiCodeEditorCls = findClass("Viper.GUI.CodeEditor");
    assert(guiCodeEditorCls != nullptr && "Viper.GUI.CodeEditor not found in catalog");
    assert(hasProperty(*guiCodeEditorCls, "Revision", "i64"));
    assert(hasMethod(*guiCodeEditorCls, "GetGutterClickSlot", "i64()"));

    const il::runtime::RuntimeClass *guiTreeViewCls = findClass("Viper.GUI.TreeView");
    assert(guiTreeViewCls != nullptr && "Viper.GUI.TreeView not found in catalog");
    assert(hasMethod(*guiTreeViewCls, "GetNodeAt", "obj(i64,i64)"));

    const il::runtime::RuntimeClass *guiMessageBoxCls = findClass("Viper.GUI.MessageBox");
    assert(guiMessageBoxCls != nullptr && "Viper.GUI.MessageBox not found in catalog");
    assert(hasMethod(*guiMessageBoxCls, "New", "obj(str,str,i64)"));

    const il::runtime::RuntimeClass *guiFileDialogCls = findClass("Viper.GUI.FileDialog");
    assert(guiFileDialogCls != nullptr && "Viper.GUI.FileDialog not found in catalog");
    assert(hasMethod(*guiFileDialogCls, "New", "obj(i64)"));

    const il::runtime::RuntimeClass *guiToolbarCls = findClass("Viper.GUI.Toolbar");
    assert(guiToolbarCls != nullptr && "Viper.GUI.Toolbar not found in catalog");
    assert(hasMethod(*guiToolbarCls, "NewVertical", "obj(obj)"));

    constexpr std::array<std::string_view, 44> graphics2DClasses = {
        "Viper.Graphics.RenderTarget2D", "Viper.Graphics.Surface2D",
        "Viper.Graphics.Texture2D",      "Viper.Graphics.GpuTexture2D",
        "Viper.Graphics.Renderer2D",     "Viper.Graphics.Material2D",
        "Viper.Graphics.Shader2D",       "Viper.Graphics.PostProcess2D",
        "Viper.Graphics.Viewport2D",     "Viper.Graphics.ScreenScaler",
        "Viper.Graphics.TileSet2D",      "Viper.Graphics.TileLayer2D",
        "Viper.Graphics.ObjectLayer2D",  "Viper.Graphics.AutoTile2D",
        "Viper.Graphics.Path2D",         "Viper.Graphics.ShapeRenderer2D",
        "Viper.Graphics.TextRenderer2D", "Viper.Graphics.BitmapFont",
        "Viper.Graphics.SdfFont",
        "Viper.Graphics.NineSlice2D",    "Viper.Graphics.ParticleSystem2D",
        "Viper.Graphics.Emitter2D",      "Viper.Graphics.DebugDraw2D",
        "Viper.Graphics.Transform2D",    "Viper.Graphics.Sampler2D",
        "Viper.Graphics.BlendState2D",   "Viper.Graphics.SpriteRenderer2D",
        "Viper.Graphics.TilemapRenderer2D",
        "Viper.Graphics.TileChunkCache2D",
        "Viper.Graphics.AnimationClip2D",
        "Viper.Graphics.AnimatedSprite2D",
        "Viper.Graphics.TextLayout2D",   "Viper.Graphics.SpriteFont",
        "Viper.Graphics.RenderPass2D",   "Viper.Graphics.RenderGraph2D",
        "Viper.Graphics.CollisionMask2D",
        "Viper.Graphics.Hitbox2D",       "Viper.Graphics.Palette2D",
        "Viper.Graphics.Gradient2D",     "Viper.Graphics.CameraRig2D",
        "Viper.Graphics.TexturePackerAtlas",
        "Viper.Graphics.AsepriteImporter",
        "Viper.Graphics.TiledMapLoader", "Viper.Graphics.Lighting2D",
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
