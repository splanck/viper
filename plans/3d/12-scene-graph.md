# Phase 12: Scene Graph / Spatial Hierarchy (3D)

## Goal

Provide a 3D scene graph with parent-child transform propagation, following the pattern of the existing 2D `rt_scene.c`. Scene nodes hold local position/rotation/scale and automatically compute world transforms by multiplying up the hierarchy.

## Dependencies

- Phase 1 complete (Canvas3D rendering)
- Existing math types: Vec3, Quat, Mat4 (all verified in `src/runtime/graphics/`)

## Architecture

```
Scene3D
  └── root (SceneNode3D)
        ├── stadium (SceneNode3D) — mesh: stadium.obj, material: stadiumMat
        │     ├── scoreboard (SceneNode3D) — mesh: board.obj
        │     └── lights (SceneNode3D) — no mesh, just transform group
        │           ├── light1 (SceneNode3D)
        │           └── light2 (SceneNode3D)
        ├── player (SceneNode3D) — mesh: player.fbx
        │     ├── bat (SceneNode3D) — mesh: bat.obj, parented to hand bone
        │     └── glove (SceneNode3D)
        └── ball (SceneNode3D) — mesh: sphere, material: ballMat

Scene3D.Draw(canvas, camera)
  → depth-first traversal
  → compute worldMatrix = parent.worldMatrix * TRS(localPos, localRot, localScale)
  → if node has mesh+material: Canvas3D.DrawMesh(mesh, worldMatrix, material)
```

## New Files

**`src/runtime/graphics/rt_scene3d.h`** (~80 LOC)
**`src/runtime/graphics/rt_scene3d.c`** (~500 LOC)

## Data Structures

```c
typedef struct rt_scene_node3d {
    void *vptr;
    rt_string name;           // dynamic allocation — follows 2D scene graph pattern (rt_scene.c:83)
                              // FBX model names frequently exceed 64 chars

    // Local transform
    double position[3];       // local translation
    double rotation[4];       // quaternion (x, y, z, w)
    double scale_xyz[3];      // local scale (default 1, 1, 1)

    // Cached world transform
    double world_matrix[16];  // row-major Mat4 (recomputed when dirty)
    int8_t world_dirty;       // 1 = needs recomputation

    // Hierarchy
    struct rt_scene_node3d *parent;
    struct rt_scene_node3d **children;
    int32_t child_count;
    int32_t child_capacity;

    // Renderable attachment (both optional)
    void *mesh;               // Mesh3D (or NULL)
    void *material;           // Material3D (or NULL)

    // Visibility
    int8_t visible;           // 1 = visible (default), 0 = hidden (skips self + children)

    // Bounding volume (computed from mesh, used by Phase 13 frustum culling)
    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;
} rt_scene_node3d;

typedef struct {
    void *vptr;
    rt_scene_node3d *root;    // always exists (created in constructor)
    int32_t node_count;       // total nodes in tree (including root)
} rt_scene3d;
```

## Transform Propagation

When any node's local transform changes (position, rotation, or scale), mark it and all descendants as dirty.

On `Draw()` or `GetWorldMatrix()`, recompute if dirty:

```c
static void recompute_world_matrix(rt_scene_node3d *node) {
    if (!node->world_dirty) return;

    // Build local TRS matrix using existing math functions
    // 1. Scale matrix from scale_xyz
    // 2. Rotation matrix from quaternion (Quat.ToMat4)
    // 3. Translation from position
    // local = Translate * Rotate * Scale  (TRS order)

    double local[16];
    build_trs_matrix(node->position, node->rotation, node->scale_xyz, local);

    if (node->parent) {
        recompute_world_matrix(node->parent);  // ensure parent is up-to-date
        mat4_mul(node->parent->world_matrix, local, node->world_matrix);
    } else {
        memcpy(node->world_matrix, local, sizeof(double) * 16);
    }

    node->world_dirty = 0;
}
```

## Public API

### Scene3D

```c
void *rt_scene3d_new(void);
void *rt_scene3d_get_root(void *scene);
void  rt_scene3d_add(void *scene, void *node);        // add to root
void  rt_scene3d_remove(void *scene, void *node);      // detach from parent
void *rt_scene3d_find(void *scene, rt_string name);    // recursive search by name
void  rt_scene3d_draw(void *scene, void *canvas3d, void *camera);
void  rt_scene3d_clear(void *scene);                   // remove all children of root
int64_t rt_scene3d_get_node_count(void *scene);
```

### SceneNode3D

```c
// Construction
void *rt_scene_node3d_new(void);

// Transform
void  rt_scene_node3d_set_position(void *node, double x, double y, double z);
void *rt_scene_node3d_get_position(void *node);        // returns Vec3
void  rt_scene_node3d_set_rotation(void *node, void *quat);
void *rt_scene_node3d_get_rotation(void *node);        // returns Quat
void  rt_scene_node3d_set_scale(void *node, double x, double y, double z);
void *rt_scene_node3d_get_scale(void *node);           // returns Vec3
void *rt_scene_node3d_get_world_matrix(void *node);    // returns Mat4

// Hierarchy
void  rt_scene_node3d_add_child(void *node, void *child);
void  rt_scene_node3d_remove_child(void *node, void *child);
int64_t rt_scene_node3d_child_count(void *node);
void *rt_scene_node3d_get_child(void *node, int64_t index);
void *rt_scene_node3d_get_parent(void *node);
void *rt_scene_node3d_find(void *node, rt_string name);  // recursive in subtree

// Renderable
void  rt_scene_node3d_set_mesh(void *node, void *mesh);
void  rt_scene_node3d_set_material(void *node, void *material);

// Visibility
void  rt_scene_node3d_set_visible(void *node, int8_t visible);
int8_t rt_scene_node3d_get_visible(void *node);

// Identity
void      rt_scene_node3d_set_name(void *node, rt_string name);
rt_string rt_scene_node3d_get_name(void *node);
```

## Draw Traversal

```c
static void draw_node(rt_scene_node3d *node, void *canvas3d, void *camera) {
    if (!node->visible) return;

    recompute_world_matrix(node);

    // Draw this node if it has a mesh and material
    if (node->mesh && node->material) {
        // Convert double[16] world_matrix to Mat4 runtime object
        void *mat4 = mat4_from_doubles(node->world_matrix);
        rt_canvas3d_draw_mesh(canvas3d, node->mesh, mat4, node->material);
    }

    // Recurse into children
    for (int32_t i = 0; i < node->child_count; i++) {
        draw_node(node->children[i], canvas3d, camera);
    }
}

void rt_scene3d_draw(void *scene, void *canvas3d, void *camera) {
    rt_scene3d *s = (rt_scene3d *)scene;
    rt_canvas3d_begin(canvas3d, camera);
    draw_node(s->root, canvas3d, camera);
    rt_canvas3d_end(canvas3d);
}
```

## GC Finalizer

```c
static void rt_scene_node3d_finalize(void *obj) {
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    // Free children array (children themselves are GC-managed)
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
    // mesh, material, and name (rt_string) are GC-managed, do NOT free
}
```

## runtime.def Entries

```c
// Scene3D
RT_FUNC(Scene3DNew,          rt_scene3d_new,          "Viper.Graphics3D.Scene3D.New",          "obj()")
RT_FUNC(Scene3DGetRoot,      rt_scene3d_get_root,     "Viper.Graphics3D.Scene3D.get_Root",     "obj(obj)")
RT_FUNC(Scene3DAdd,          rt_scene3d_add,          "Viper.Graphics3D.Scene3D.Add",          "void(obj,obj)")
RT_FUNC(Scene3DRemove,       rt_scene3d_remove,       "Viper.Graphics3D.Scene3D.Remove",       "void(obj,obj)")
RT_FUNC(Scene3DFind,         rt_scene3d_find,         "Viper.Graphics3D.Scene3D.Find",         "obj(obj,str)")
RT_FUNC(Scene3DDraw,         rt_scene3d_draw,         "Viper.Graphics3D.Scene3D.Draw",         "void(obj,obj,obj)")
RT_FUNC(Scene3DClear,        rt_scene3d_clear,        "Viper.Graphics3D.Scene3D.Clear",        "void(obj)")
RT_FUNC(Scene3DNodeCount,    rt_scene3d_get_node_count,"Viper.Graphics3D.Scene3D.get_NodeCount","i64(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.Scene3D", Scene3D, "obj", Scene3DNew)
    RT_PROP("Root", "obj", Scene3DGetRoot, none)
    RT_PROP("NodeCount", "i64", Scene3DNodeCount, none)
    RT_METHOD("Add", "void(obj)", Scene3DAdd)
    RT_METHOD("Remove", "void(obj)", Scene3DRemove)
    RT_METHOD("Find", "obj(str)", Scene3DFind)
    RT_METHOD("Draw", "void(obj,obj)", Scene3DDraw)
    RT_METHOD("Clear", "void()", Scene3DClear)
RT_CLASS_END()

// SceneNode3D
RT_FUNC(SceneNode3DNew,           rt_scene_node3d_new,           "Viper.Graphics3D.SceneNode3D.New",           "obj()")
RT_FUNC(SceneNode3DSetPosition,   rt_scene_node3d_set_position,  "Viper.Graphics3D.SceneNode3D.SetPosition",   "void(obj,f64,f64,f64)")
RT_FUNC(SceneNode3DGetPosition,   rt_scene_node3d_get_position,  "Viper.Graphics3D.SceneNode3D.get_Position",  "obj(obj)")
RT_FUNC(SceneNode3DSetRotation,   rt_scene_node3d_set_rotation,  "Viper.Graphics3D.SceneNode3D.set_Rotation",  "void(obj,obj)")
RT_FUNC(SceneNode3DGetRotation,   rt_scene_node3d_get_rotation,  "Viper.Graphics3D.SceneNode3D.get_Rotation",  "obj(obj)")
RT_FUNC(SceneNode3DSetScale,      rt_scene_node3d_set_scale,     "Viper.Graphics3D.SceneNode3D.SetScale",      "void(obj,f64,f64,f64)")
RT_FUNC(SceneNode3DGetScale,      rt_scene_node3d_get_scale,     "Viper.Graphics3D.SceneNode3D.get_Scale",     "obj(obj)")
RT_FUNC(SceneNode3DGetWorldMatrix,rt_scene_node3d_get_world_matrix,"Viper.Graphics3D.SceneNode3D.get_WorldMatrix","obj(obj)")
RT_FUNC(SceneNode3DSetMesh,       rt_scene_node3d_set_mesh,      "Viper.Graphics3D.SceneNode3D.set_Mesh",      "void(obj,obj)")
RT_FUNC(SceneNode3DSetMaterial,   rt_scene_node3d_set_material,  "Viper.Graphics3D.SceneNode3D.set_Material",  "void(obj,obj)")
RT_FUNC(SceneNode3DAddChild,      rt_scene_node3d_add_child,     "Viper.Graphics3D.SceneNode3D.AddChild",      "void(obj,obj)")
RT_FUNC(SceneNode3DRemoveChild,   rt_scene_node3d_remove_child,  "Viper.Graphics3D.SceneNode3D.RemoveChild",   "void(obj,obj)")
RT_FUNC(SceneNode3DChildCount,    rt_scene_node3d_child_count,    "Viper.Graphics3D.SceneNode3D.get_ChildCount","i64(obj)")
RT_FUNC(SceneNode3DGetChild,      rt_scene_node3d_get_child,     "Viper.Graphics3D.SceneNode3D.GetChild",      "obj(obj,i64)")
RT_FUNC(SceneNode3DGetParent,     rt_scene_node3d_get_parent,    "Viper.Graphics3D.SceneNode3D.get_Parent",    "obj(obj)")
RT_FUNC(SceneNode3DFind,          rt_scene_node3d_find,          "Viper.Graphics3D.SceneNode3D.Find",          "obj(obj,str)")
RT_FUNC(SceneNode3DSetVisible,    rt_scene_node3d_set_visible,   "Viper.Graphics3D.SceneNode3D.set_Visible",   "void(obj,i1)")
RT_FUNC(SceneNode3DGetVisible,    rt_scene_node3d_get_visible,   "Viper.Graphics3D.SceneNode3D.get_Visible",   "i1(obj)")
RT_FUNC(SceneNode3DSetName,       rt_scene_node3d_set_name,      "Viper.Graphics3D.SceneNode3D.set_Name",      "void(obj,str)")
RT_FUNC(SceneNode3DGetName,       rt_scene_node3d_get_name,      "Viper.Graphics3D.SceneNode3D.get_Name",      "str(obj)")
RT_FUNC(SceneNode3DGetAABBMin,   rt_scene_node3d_get_aabb_min,  "Viper.Graphics3D.SceneNode3D.get_AABBMin",   "obj(obj)")
RT_FUNC(SceneNode3DGetAABBMax,   rt_scene_node3d_get_aabb_max,  "Viper.Graphics3D.SceneNode3D.get_AABBMax",   "obj(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.SceneNode3D", SceneNode3D, "obj", SceneNode3DNew)
    RT_PROP("Position", "obj", SceneNode3DGetPosition, none)
    RT_PROP("Rotation", "obj", SceneNode3DGetRotation, SceneNode3DSetRotation)
    RT_PROP("Scale", "obj", SceneNode3DGetScale, none)
    RT_PROP("WorldMatrix", "obj", SceneNode3DGetWorldMatrix, none)
    RT_PROP("ChildCount", "i64", SceneNode3DChildCount, none)
    RT_PROP("Parent", "obj", SceneNode3DGetParent, none)
    RT_PROP("Visible", "i1", SceneNode3DGetVisible, SceneNode3DSetVisible)
    RT_PROP("Name", "str", SceneNode3DGetName, SceneNode3DSetName)
    RT_PROP("AABBMin", "obj", SceneNode3DGetAABBMin, none)
    RT_PROP("AABBMax", "obj", SceneNode3DGetAABBMax, none)
    RT_METHOD("SetPosition", "void(f64,f64,f64)", SceneNode3DSetPosition)
    RT_METHOD("SetScale", "void(f64,f64,f64)", SceneNode3DSetScale)
    RT_METHOD("SetMesh", "void(obj)", SceneNode3DSetMesh)
    RT_METHOD("SetMaterial", "void(obj)", SceneNode3DSetMaterial)
    RT_METHOD("AddChild", "void(obj)", SceneNode3DAddChild)
    RT_METHOD("RemoveChild", "void(obj)", SceneNode3DRemoveChild)
    RT_METHOD("GetChild", "obj(i64)", SceneNode3DGetChild)
    RT_METHOD("Find", "obj(str)", SceneNode3DFind)
RT_CLASS_END()
```

## Stubs

SceneNode3D.New and Scene3D.New trap. All methods are no-ops. Getters return 0/NULL.

## Usage Example (Zia)

```rust
var scene = Scene3D.New()

var player = SceneNode3D.New()
player.Name = "player"
player.SetPosition(0.0, 0.0, 0.0)
player.Mesh = playerMesh
player.Material = playerMat

var bat = SceneNode3D.New()
bat.Name = "bat"
bat.SetPosition(0.5, 1.0, 0.0)  // offset from player
bat.Mesh = batMesh
bat.Material = batMat

player.AddChild(bat)  // bat moves with player
scene.Add(player)

// In game loop:
player.SetPosition(x, y, z)
player.Rotation = Quat.FromEuler(0.0, angle, 0.0)
scene.Draw(canvas, cam)
```

## Tests (20)

| Test | Description |
|------|-------------|
| Create scene/node | New() returns non-null |
| Add child | AddChild increases ChildCount |
| Remove child | RemoveChild decreases ChildCount |
| Parent reference | Child's Parent property points to parent |
| Root exists | Scene.Root is non-null at creation |
| Transform propagation | Translate parent (5,0,0), child at (1,0,0) → child world pos = (6,0,0) |
| Rotation propagation | Rotate parent 90° Y, child at (1,0,0) → child world pos ≈ (0,0,-1) |
| Scale propagation | Parent scale (2,2,2), child at (1,1,1) → child world pos = (2,2,2) |
| Deep hierarchy (5 levels) | Transforms compose correctly through 5 levels |
| Dirty flag | Change parent → children marked dirty |
| Dirty clears after recompute | GetWorldMatrix clears dirty flag |
| Find by name | Scene.Find("bat") returns bat node |
| Find not found | Scene.Find("nonexistent") returns null |
| Reparenting | Move child from one parent to another |
| Draw traversal | Scene.Draw calls DrawMesh for each visible node with mesh+material |
| Invisible skip | Visible=false → node and all children not drawn |
| Mesh-only node | Node with mesh but no material → not drawn |
| Empty node | Node with no mesh → drawn as transform-only group (children still drawn) |
| Clear | Scene.Clear removes all children from root |
| Node count | NodeCount reflects total tree size |
