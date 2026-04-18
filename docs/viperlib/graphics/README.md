---
status: active
audience: public
last-verified: 2026-04-18
---

# Graphics
> 2D graphics, scene management, 3D runtime surfaces, and asset import workflows.

**Part of [Viper Runtime Library](../README.md)**

The current 2D runtime includes clip-correct text/image rendering inside GUI surfaces, full UTF-8 text-input event delivery from desktop backends, and desktop text clipboard support across the supported platforms.

---

## Contents

| File | Contents |
|------|----------|
| [Canvas & Color](canvas.md) | Canvas drawing surface and Color utilities |
| [Fonts](fonts.md) | BitmapFont for custom BDF/PSF font rendering |
| [Images & Sprites](pixels.md) | Pixels, Sprite, SpriteSheet, Tilemap |
| [Scene Graph](scene.md) | SceneNode, Scene, SpriteBatch, TextureAtlas, Camera, SpriteAnimation |
| [3D Physics](physics3d.md) | `Physics3DWorld`, `PhysicsHit3D`, `PhysicsHitList3D`, `CollisionEvent3D`, `ContactPoint3D`, `Collider3D`, `Physics3DBody`, `Character3D`, `DistanceJoint3D`, `SpringJoint3D` |
| [Graphics 3D Guide](../../graphics3d-guide.md) | `Model3D`, `AnimController3D`, `Canvas3D`, `Scene3D`, `SceneNode3D`, `Scene3D.SyncBindings`, `Mesh3D`, `Material3D`, `FBX`, `GLTF`, and the higher-level 3D asset pipeline |

## See Also

- [Input](../input.md) - `Keyboard`, `Mouse`, and `Pad` for interactive input with Canvas
- [Audio](../audio.md) - Sound effects and music for games
- [Mathematics](../math.md) - `Vec2` and `Vec3` for 2D/3D graphics calculations
- [Collections](../collections/README.md) - `Bytes` for pixel data serialization
- [Graphics 3D Guide](../../graphics3d-guide.md) - higher-level rendering, materials, meshes, and scenes
