---
status: active
audience: public
last-verified: 2026-04-20
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
| [Images & Sprites](pixels.md) | Pixels, Sprite, SpriteAnimator, SpriteSheet, Tilemap |
| [2D Rendering and Effects](rendering2d.md) | RenderTarget2D, Texture2D, Renderer2D, Material2D, Shader2D, PostProcess2D, Sampler2D, BlendState2D, SpriteRenderer2D, RenderPass2D, RenderGraph2D, Palette2D, Gradient2D |
| [2D Tilemaps and Layers](tilemaps2d.md) | TileSet2D, TileLayer2D, ObjectLayer2D, AutoTile2D, TilemapRenderer2D, TileChunkCache2D, TexturePackerAtlas, AsepriteImporter, TiledMapLoader |
| [2D Shapes, Text, and UI](shapes2d.md) | Path2D, ShapeRenderer2D, TextRenderer2D, TextLayout2D, SpriteFont, SdfFont, NineSlice2D, DebugDraw2D |
| [2D Animation, Collision, and Camera](game2d.md) | Viewport2D, ScreenScaler, Transform2D, AnimationClip2D, AnimatedSprite2D, CollisionMask2D, Hitbox2D, CameraRig2D, ParticleSystem2D, Emitter2D, Lighting2D |
| [Scene Graph](scene.md) | SceneNode, Scene, SpriteBatch, TextureAtlas, Camera, SpriteAnimation |
| [Game3D](game3d.md) | `World3D`, `Entity3D`, `LayerMask`, `Input3D`, native callback-loop boundaries, and code-first 3D game workflow helpers |
| [3D Physics](physics3d.md) | `Physics3DWorld`, `PhysicsHit3D`, `PhysicsHitList3D`, `CollisionEvent3D`, `ContactPoint3D`, `Collider3D`, `Physics3DBody`, `Character3D`, `DistanceJoint3D`, `SpringJoint3D` |
| [3D Rendering, Animation, and Environment](rendering3d.md) | `Camera3D`, `RenderTarget3D`, `CubeMap3D`, `Light3D`, `PostFX3D`, `RayHit3D`, `Skeleton3D`, `Animation3D`, `AnimPlayer3D`, `AnimBlend3D`, `AnimController3D`, `MorphTarget3D`, `Particles3D`, `Decal3D`, `Sound3D`, `SoundListener3D`, `SoundSource3D`, `NavMesh3D`, `NavAgent3D`, `Terrain3D`, `Water3D`, `Vegetation3D`, `Transform3D`, `Trigger3D`, `Path3D`, `InstanceBatch3D`, `Sprite3D`, `TextureAtlas3D` |
| [Graphics 3D Guide](../../graphics3d-guide.md) | `Model3D`, `AnimController3D`, `Canvas3D`, `Scene3D`, `SceneNode3D`, `Scene3D.SyncBindings`, `Mesh3D`, `Material3D`, `FBX`, `GLTF`, and the higher-level 3D asset pipeline |

## See Also

- [Input](../input.md) - `Keyboard`, `Mouse`, and `Pad` for interactive input with Canvas
- [Audio](../audio.md) - Sound effects and music for games
- [Mathematics](../math.md) - `Vec2` and `Vec3` for 2D/3D graphics calculations
- [Collections](../collections/README.md) - `Bytes` for pixel data serialization
- [Graphics 3D Guide](../../graphics3d-guide.md) - higher-level rendering, materials, meshes, and scenes
