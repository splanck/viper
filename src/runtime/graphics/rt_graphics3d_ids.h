//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_graphics3d_ids.h
// Purpose: Stable runtime class identifiers for Viper.Graphics3D objects.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

extern int64_t rt_obj_class_id(void *p);

#define RT_G3D_CUBEMAP3D_CLASS_ID INT64_C(-0x603001)
#define RT_G3D_RENDERTARGET3D_CLASS_ID INT64_C(-0x603002)
#define RT_G3D_CANVAS3D_CLASS_ID INT64_C(-0x603003)
#define RT_G3D_MESH3D_CLASS_ID INT64_C(-0x603004)
#define RT_G3D_CAMERA3D_CLASS_ID INT64_C(-0x603005)
#define RT_G3D_MATERIAL3D_CLASS_ID INT64_C(-0x603006)
#define RT_G3D_LIGHT3D_CLASS_ID INT64_C(-0x603007)
#define RT_G3D_SCENE3D_CLASS_ID INT64_C(-0x603008)
#define RT_G3D_SCENENODE3D_CLASS_ID INT64_C(-0x603009)
#define RT_G3D_NODEANIMATION3D_CLASS_ID INT64_C(-0x60300A)
#define RT_G3D_NODEANIMATOR3D_CLASS_ID INT64_C(-0x60300B)
#define RT_G3D_SKELETON3D_CLASS_ID INT64_C(-0x60300C)
#define RT_G3D_ANIMATION3D_CLASS_ID INT64_C(-0x60300D)
#define RT_G3D_ANIMPLAYER3D_CLASS_ID INT64_C(-0x60300E)
#define RT_G3D_ANIMBLEND3D_CLASS_ID INT64_C(-0x60300F)
#define RT_G3D_ANIMCONTROLLER3D_CLASS_ID INT64_C(-0x603010)
#define RT_G3D_FBX_ASSET_CLASS_ID INT64_C(-0x603011)
#define RT_G3D_GLTF_ASSET_CLASS_ID INT64_C(-0x603012)
#define RT_G3D_MODEL3D_CLASS_ID INT64_C(-0x603013)
#define RT_G3D_MORPHTARGET3D_CLASS_ID INT64_C(-0x603014)
#define RT_G3D_PARTICLES3D_CLASS_ID INT64_C(-0x603015)
#define RT_G3D_POSTFX3D_CLASS_ID INT64_C(-0x603016)
#define RT_G3D_RAYHIT3D_CLASS_ID INT64_C(-0x603017)
#define RT_G3D_AUDIOLISTENER3D_CLASS_ID INT64_C(-0x603018)
#define RT_G3D_AUDIOSOURCE3D_CLASS_ID INT64_C(-0x603019)
#define RT_G3D_WORLD3D_CLASS_ID INT64_C(-0x60301A)
#define RT_G3D_PHYSICSHIT3D_CLASS_ID INT64_C(-0x60301B)
#define RT_G3D_PHYSICSHITLIST3D_CLASS_ID INT64_C(-0x60301C)
#define RT_G3D_CONTACTPOINT3D_CLASS_ID INT64_C(-0x60301D)
#define RT_G3D_COLLISIONEVENT3D_CLASS_ID INT64_C(-0x60301E)
#define RT_G3D_COLLIDER3D_CLASS_ID INT64_C(-0x60301F)
#define RT_G3D_BODY3D_CLASS_ID INT64_C(-0x603020)
#define RT_G3D_CHARACTER3D_CLASS_ID INT64_C(-0x603021)
#define RT_G3D_TRIGGER3D_CLASS_ID INT64_C(-0x603022)
#define RT_G3D_DISTANCEJOINT3D_CLASS_ID INT64_C(-0x603023)
#define RT_G3D_SPRINGJOINT3D_CLASS_ID INT64_C(-0x603024)
#define RT_G3D_TRANSFORM3D_CLASS_ID INT64_C(-0x603025)
#define RT_G3D_PATH3D_CLASS_ID INT64_C(-0x603026)
#define RT_G3D_INSTANCEBATCH3D_CLASS_ID INT64_C(-0x603027)
#define RT_G3D_TERRAIN3D_CLASS_ID INT64_C(-0x603028)
#define RT_G3D_NAVMESH3D_CLASS_ID INT64_C(-0x603029)
#define RT_G3D_NAVAGENT3D_CLASS_ID INT64_C(-0x60302A)
#define RT_G3D_DECAL3D_CLASS_ID INT64_C(-0x60302B)
#define RT_G3D_SPRITE3D_CLASS_ID INT64_C(-0x60302C)
#define RT_G3D_WATER3D_CLASS_ID INT64_C(-0x60302D)
#define RT_G3D_VEGETATION3D_CLASS_ID INT64_C(-0x60302E)
#define RT_G3D_TEXTUREATLAS3D_CLASS_ID INT64_C(-0x60302F)

static inline int32_t rt_g3d_has_class(void *obj, int64_t class_id) {
#ifdef RT_G3D_INTERNAL_ASSUME_STRUCT_HANDLE
    (void)class_id;
    return obj != NULL;
#else
    return obj && rt_obj_class_id(obj) == class_id;
#endif
}

static inline void *rt_g3d_checked_or_null(void *obj, int64_t class_id) {
    return rt_g3d_has_class(obj, class_id) ? obj : NULL;
}
