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

int main() {
    const auto &cat = il::runtime::runtimeClassCatalog();
    assert(cat.size() >= 1);

    // Find Viper.String in the catalog (order-independent)
    const il::runtime::RuntimeClass *stringCls = nullptr;
    for (const auto &cls : cat) {
        if (std::string_view(cls.qname) == std::string_view("Viper.String")) {
            stringCls = &cls;
            break;
        }
    }

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

    constexpr std::array<std::string_view, 43> graphics2DClasses = {
        "Viper.Graphics.RenderTarget2D", "Viper.Graphics.Surface2D",
        "Viper.Graphics.Texture2D",      "Viper.Graphics.GpuTexture2D",
        "Viper.Graphics.Renderer2D",     "Viper.Graphics.Material2D",
        "Viper.Graphics.Shader2D",       "Viper.Graphics.PostProcess2D",
        "Viper.Graphics.Viewport2D",     "Viper.Graphics.ScreenScaler",
        "Viper.Graphics.TileSet2D",      "Viper.Graphics.TileLayer2D",
        "Viper.Graphics.ObjectLayer2D",  "Viper.Graphics.AutoTile2D",
        "Viper.Graphics.Path2D",         "Viper.Graphics.ShapeRenderer2D",
        "Viper.Graphics.TextRenderer2D", "Viper.Graphics.SdfFont",
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
