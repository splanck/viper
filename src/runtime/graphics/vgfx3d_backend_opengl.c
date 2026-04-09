//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_opengl.c
// Purpose: OpenGL 3.3 Core GPU backend for Viper.Graphics3D (Linux).
//
//===----------------------------------------------------------------------===//

#if defined(__linux__) && defined(VIPER_ENABLE_GRAPHICS)

#include "vgfx.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <X11/Xlib.h>

#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLboolean;
typedef void GLvoid;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef unsigned int GLbitfield;

#define GL_TRUE 1
#define GL_FALSE 0
#define GL_NONE 0
#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CCW 0x0901
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_R32F 0x822E
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_REPEAT 0x2901
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_INVALID_INDEX 0xFFFFFFFFu

typedef void (*PFNGLCLEARPROC)(GLbitfield);
typedef void (*PFNGLCLEARCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLCLEARDEPTHPROC)(double);
typedef void (*PFNGLENABLEPROC)(GLenum);
typedef void (*PFNGLDISABLEPROC)(GLenum);
typedef void (*PFNGLDEPTHFUNCPROC)(GLenum);
typedef void (*PFNGLCULLFACEPROC)(GLenum);
typedef void (*PFNGLFRONTFACEPROC)(GLenum);
typedef void (*PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFNGLPOLYGONMODEPROC)(GLenum, GLenum);
typedef void (*PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const void *);
typedef void (*PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
typedef void (*PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum, GLsizei, GLenum, const void *, GLsizei);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (*PFNGLDELETESHADERPROC)(GLuint);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef GLuint (*PFNGLGETUNIFORMBLOCKINDEXPROC)(GLuint, const GLchar *);
typedef void (*PFNGLUNIFORMBLOCKBINDINGPROC)(GLuint, GLuint, GLuint);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (*PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void (*PFNGLUNIFORM1FVPROC)(GLint, GLsizei, const GLfloat *);
typedef void (*PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void (*PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (*PFNGLGENBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (*PFNGLBINDBUFFERBASEPROC)(GLenum, GLuint, GLuint);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*PFNGLBUFFERSUBDATAPROC)(GLenum, GLintptr, GLsizeiptr, const void *);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(
    GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (*PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint, GLint, GLenum, GLsizei, const void *);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (*PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (*PFNGLVERTEXATTRIBDIVISORPROC)(GLuint, GLuint);
typedef void (*PFNGLVERTEXATTRIB4FPROC)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLBLENDFUNCPROC)(GLenum, GLenum);
typedef void (*PFNGLDEPTHMASKPROC)(GLboolean);
typedef void (*PFNGLGENTEXTURESPROC)(GLsizei, GLuint *);
typedef void (*PFNGLDELETETEXTURESPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void (*PFNGLBINDTEXTUREPROC)(GLenum, GLuint);
typedef void (*PFNGLTEXIMAGE2DPROC)(
    GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (*PFNGLTEXPARAMETERIPROC)(GLenum, GLenum, GLint);
typedef void (*PFNGLTEXBUFFERPROC)(GLenum, GLenum, GLuint);
typedef void (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*PFNGLGENRENDERBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLDELETERENDERBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLBINDRENDERBUFFERPROC)(GLenum, GLuint);
typedef void (*PFNGLRENDERBUFFERSTORAGEPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void (*PFNGLREADPIXELSPROC)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void (*PFNGLDRAWBUFFERPROC)(GLenum);
typedef void (*PFNGLDRAWBUFFERSPROC)(GLsizei, const GLenum *);
typedef void (*PFNGLREADBUFFERPROC)(GLenum);
typedef GLenum (*PFNGLGETERRORPROC)(void);

static struct {
    void *lib;
    PFNGLGETERRORPROC GetError;
    PFNGLCLEARPROC Clear;
    PFNGLCLEARCOLORPROC ClearColor;
    PFNGLCLEARDEPTHPROC ClearDepth;
    PFNGLENABLEPROC Enable;
    PFNGLDISABLEPROC Disable;
    PFNGLDEPTHFUNCPROC DepthFunc;
    PFNGLCULLFACEPROC CullFace;
    PFNGLFRONTFACEPROC FrontFace;
    PFNGLVIEWPORTPROC Viewport;
    PFNGLPOLYGONMODEPROC PolygonMode;
    PFNGLDRAWELEMENTSPROC DrawElements;
    PFNGLDRAWARRAYSPROC DrawArrays;
    PFNGLDRAWELEMENTSINSTANCEDPROC DrawElementsInstanced;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLGETUNIFORMBLOCKINDEXPROC GetUniformBlockIndex;
    PFNGLUNIFORMBLOCKBINDINGPROC UniformBlockBinding;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM1FVPROC Uniform1fv;
    PFNGLUNIFORM2FPROC Uniform2f;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBINDBUFFERBASEPROC BindBufferBase;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLBUFFERSUBDATAPROC BufferSubData;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLVERTEXATTRIBIPOINTERPROC VertexAttribIPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
    PFNGLVERTEXATTRIBDIVISORPROC VertexAttribDivisor;
    PFNGLVERTEXATTRIB4FPROC VertexAttrib4f;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLBLENDFUNCPROC BlendFunc;
    PFNGLDEPTHMASKPROC DepthMask;
    PFNGLGENTEXTURESPROC GenTextures;
    PFNGLDELETETEXTURESPROC DeleteTextures;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    PFNGLBINDTEXTUREPROC BindTexture;
    PFNGLTEXIMAGE2DPROC TexImage2D;
    PFNGLTEXPARAMETERIPROC TexParameteri;
    PFNGLTEXBUFFERPROC TexBuffer;
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
    PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
    PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;
    PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
    PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
    PFNGLREADPIXELSPROC ReadPixels;
    PFNGLDRAWBUFFERPROC DrawBuffer;
    PFNGLDRAWBUFFERSPROC DrawBuffers;
    PFNGLREADBUFFERPROC ReadBuffer;
} gl;

/* Debug GL error checking — enabled in debug builds only */
#ifndef NDEBUG
static __attribute__((unused)) void gl_check_error(const char *file, int line) {
    if (!gl.GetError)
        return;
    GLenum err = gl.GetError();
    if (err != GL_NO_ERROR)
        fprintf(stderr, "GL error 0x%04X at %s:%d\n", (unsigned)err, file, line);
}

#define GL_CHECK() gl_check_error(__FILE__, __LINE__)
#else
#define GL_CHECK() ((void)0)
#endif

typedef void *GLXContext;
typedef unsigned long GLXDrawable;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef void (*__GLXextFuncPtr)(void);
typedef GLXContext (*PFNGLXCREATENEWCONTEXTPROC)(Display *, GLXFBConfig, int, GLXContext, int);
typedef GLXFBConfig *(*PFNGLXCHOOSEFBCONFIGPROC)(Display *, int, const int *, int *);
typedef void (*PFNGLXSWAPBUFFERSPROC)(Display *, GLXDrawable);
typedef int (*PFNGLXMAKECURRENTPROC)(Display *, GLXDrawable, GLXContext);
typedef void (*PFNGLXDESTROYCONTEXTPROC)(Display *, GLXContext);
typedef __GLXextFuncPtr (*PFNGLXGETPROCADDRESSPROC)(const unsigned char *);
typedef GLXContext (*PFNGLXCREATECONTEXTATTRIBSARBPROC)(
    Display *, GLXFBConfig, GLXContext, int, const int *);

static struct {
    PFNGLXCHOOSEFBCONFIGPROC ChooseFBConfig;
    PFNGLXCREATENEWCONTEXTPROC CreateNewContext;
    PFNGLXCREATECONTEXTATTRIBSARBPROC CreateContextAttribsARB;
    PFNGLXSWAPBUFFERSPROC SwapBuffers;
    PFNGLXMAKECURRENTPROC MakeCurrent;
    PFNGLXDESTROYCONTEXTPROC DestroyContext;
    PFNGLXGETPROCADDRESSPROC GetProcAddress;
} glx;

#define GLX_RGBA_BIT 0x0001
#define GLX_RENDER_TYPE 0x8011
#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_WINDOW_BIT 0x0001
#define GLX_DOUBLEBUFFER 5
#define GLX_DEPTH_SIZE 12
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10
#define GLX_ALPHA_SIZE 11
#define GLX_RGBA_TYPE 0x8014
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef struct {
    const void *pixels;
    uint64_t generation;
    GLuint tex;
} gl_texture_cache_entry_t;

typedef struct {
    const void *cubemap;
    uint64_t generation;
    GLuint tex;
} gl_cubemap_cache_entry_t;

#define GL_MESH_CACHE_CAPACITY 128

typedef struct {
    const void *key;
    uint32_t revision;
    uint32_t vertex_count;
    uint32_t index_count;
    GLuint vbo;
    GLuint ibo;
    uint64_t last_used_frame;
} gl_mesh_cache_entry_t;

typedef struct {
    Display *display;
    Window window;
    GLXContext glxCtx;

    GLuint program;
    GLuint shadow_program;
    GLuint postfx_program;
    GLuint skybox_program;
    GLuint vao;
    GLuint fullscreen_vao;
    GLuint skybox_vao;
    GLuint skybox_vbo;

    GLuint mesh_vbo;
    GLuint mesh_ibo;
    size_t mesh_vbo_capacity;
    size_t mesh_ibo_capacity;

    GLuint instance_vbo;
    size_t instance_vbo_capacity;
    GLuint prev_instance_vbo;
    size_t prev_instance_vbo_capacity;

    GLuint bone_ubo;
    GLuint prev_bone_ubo;

    GLuint morph_buffer;
    GLuint morph_tbo;
    size_t morph_capacity_bytes;

    GLuint morph_normal_buffer;
    GLuint morph_normal_tbo;
    size_t morph_normal_capacity_bytes;

    GLuint scene_fbo;
    GLuint scene_color_tex;
    GLuint scene_motion_tex;
    GLuint scene_depth_tex;
    int32_t scene_width;
    int32_t scene_height;

    GLuint rtt_fbo;
    GLuint rtt_color_tex;
    GLuint rtt_depth_rbo;
    int32_t rtt_width;
    int32_t rtt_height;
    int8_t rtt_active;
    vgfx3d_rendertarget_t *rtt_target;

    GLuint shadow_fbo;
    GLuint shadow_depth_tex;
    int32_t shadow_width;
    int32_t shadow_height;
    int8_t shadow_active;
    float shadow_bias;
    float shadow_vp[16];

    gl_texture_cache_entry_t *texture_cache;
    int32_t texture_cache_count;
    int32_t texture_cache_capacity;
    gl_cubemap_cache_entry_t *cubemap_cache;
    int32_t cubemap_cache_count;
    int32_t cubemap_cache_capacity;
    gl_mesh_cache_entry_t mesh_cache[GL_MESH_CACHE_CAPACITY];
    uint64_t frame_serial;

    int32_t width;
    int32_t height;
    float view[16];
    float projection[16];
    float vp[16];
    float inv_vp[16];
    float prev_vp[16];
    int8_t prev_vp_valid;
    float cam_pos[3];
    int8_t fog_enabled;
    float fog_near;
    float fog_far;
    float fog_color[3];
    float clearR, clearG, clearB;

    GLint uModelMatrix, uPrevModelMatrix, uViewProjection, uPrevViewProjection, uNormalMatrix,
        uShadowVP;
    GLint uCameraPos, uAmbientColor, uDiffuseColor, uSpecularColor, uEmissiveColor, uAlpha;
    GLint uPbrScalars0, uPbrScalars1;
    GLint uUnlit, uShadingModel, uLightCount, uHasTexture, uHasNormalMap, uHasSpecularMap,
        uHasEmissiveMap;
    GLint uHasEnvMap, uReflectivity, uWorkflow, uAlphaMode, uHasMetallicRoughnessMap, uHasAOMap;
    GLint uCustomParams;
    GLint uHasSplat, uFogEnabled, uFogNear, uFogFar, uFogColor;
    GLint uShadowEnabled, uShadowBias;
    GLint uUseInstancing, uHasSkinning, uMorphShapeCount, uVertexCount;
    GLint uHasPrevModelMatrix, uHasPrevInstanceMatrices, uHasPrevSkinning, uHasPrevMorphWeights;
    GLint uMorphWeights, uPrevMorphWeights, uMorphDeltas, uMorphNormalDeltas, uHasMorphNormalDeltas;
    GLint uDiffuseTex, uNormalTex, uSpecularTex, uEmissiveTex, uShadowTex, uEnvMap;
    GLint uMetallicRoughnessTex, uAOTex;
    GLint uSplatTex, uSplatLayer0, uSplatLayer1, uSplatLayer2, uSplatLayer3, uSplatScales;
    GLint uLightType[8], uLightDir[8], uLightPos[8], uLightColor[8], uLightIntensity[8];
    GLint uLightAtten[8], uLightInnerCos[8], uLightOuterCos[8];

    GLint shadow_uModelMatrix, shadow_uViewProjection;
    GLint shadow_uHasSkinning, shadow_uMorphShapeCount, shadow_uVertexCount;
    GLint shadow_uMorphWeights, shadow_uMorphDeltas;

    GLint skybox_uProjection;
    GLint skybox_uViewRotation;
    GLint skybox_uSkybox;

    GLint postfx_uSceneTex, postfx_uSceneDepthTex, postfx_uSceneMotionTex, postfx_uInvResolution;
    GLint postfx_uBloomEnabled, postfx_uBloomThreshold, postfx_uBloomIntensity;
    GLint postfx_uTonemapMode, postfx_uTonemapExposure, postfx_uFxaaEnabled;
    GLint postfx_uColorGradeEnabled, postfx_uCgBrightness, postfx_uCgContrast, postfx_uCgSaturation;
    GLint postfx_uVignetteEnabled, postfx_uVignetteRadius, postfx_uVignetteSoftness;
    GLint postfx_uSsaoEnabled, postfx_uSsaoRadius, postfx_uSsaoIntensity, postfx_uSsaoSamples;
    GLint postfx_uDofEnabled, postfx_uDofFocusDistance, postfx_uDofAperture, postfx_uDofMaxBlur;
    GLint postfx_uMotionBlurEnabled, postfx_uMotionBlurIntensity, postfx_uMotionBlurSamples;
    GLint postfx_uCameraPos, postfx_uInvViewProjection, postfx_uPrevViewProjection;
} gl_context_t;

static int gl_loaded = 0;

static void transpose4x4(const float *src, float *dst) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst[r * 4 + c] = src[c * 4 + r];
}

static void mat4f_mul_gl(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

static int mat4f_inverse_gl(const float *m, float *out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    {
        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (fabsf(det) < 1e-12f)
            return -1;
        det = 1.0f / det;
        for (int i = 0; i < 16; i++)
            out[i] = inv[i] * det;
    }
    return 0;
}

static int load_gl(void) {
    if (gl_loaded)
        return 0;

    gl.lib = dlopen("libGL.so.1", RTLD_LAZY);
    if (!gl.lib)
        gl.lib = dlopen("libGL.so", RTLD_LAZY);
    if (!gl.lib)
        return -1;

#define LOAD(name)                                                                                 \
    gl.name = (__typeof__(gl.name))dlsym(gl.lib, "gl" #name);                                      \
    if (!gl.name)                                                                                  \
    return -1
#define LOADX(name)                                                                                \
    glx.name = (__typeof__(glx.name))dlsym(gl.lib, "glX" #name);                                   \
    if (!glx.name)                                                                                 \
    return -1
#define LOADP(name)                                                                                \
    gl.name = (__typeof__(gl.name))glx.GetProcAddress((const unsigned char *)"gl" #name);          \
    if (!gl.name)                                                                                  \
    return -1

    LOAD(GetError);
    LOAD(Clear);
    LOAD(ClearColor);
    LOAD(ClearDepth);
    LOAD(Enable);
    LOAD(Disable);
    LOAD(DepthFunc);
    LOAD(CullFace);
    LOAD(FrontFace);
    LOAD(Viewport);
    LOAD(BlendFunc);
    LOAD(DepthMask);

    LOADX(ChooseFBConfig);
    LOADX(CreateNewContext);
    LOADX(SwapBuffers);
    LOADX(MakeCurrent);
    LOADX(DestroyContext);
    LOADX(GetProcAddress);
    glx.CreateContextAttribsARB = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glx.GetProcAddress(
        (const unsigned char *)"glXCreateContextAttribsARB");

    LOADP(PolygonMode);
    LOADP(DrawElements);
    LOADP(DrawArrays);
    LOADP(DrawElementsInstanced);
    LOADP(CreateShader);
    LOADP(ShaderSource);
    LOADP(CompileShader);
    LOADP(GetShaderiv);
    LOADP(GetShaderInfoLog);
    LOADP(CreateProgram);
    LOADP(AttachShader);
    LOADP(LinkProgram);
    LOADP(GetProgramiv);
    LOADP(GetProgramInfoLog);
    LOADP(UseProgram);
    LOADP(DeleteShader);
    LOADP(DeleteProgram);
    LOADP(GetUniformLocation);
    LOADP(GetUniformBlockIndex);
    LOADP(UniformBlockBinding);
    LOADP(UniformMatrix4fv);
    LOADP(Uniform1i);
    LOADP(Uniform1f);
    LOADP(Uniform1fv);
    LOADP(Uniform2f);
    LOADP(Uniform3f);
    LOADP(Uniform4f);
    LOADP(GenVertexArrays);
    LOADP(BindVertexArray);
    LOADP(GenBuffers);
    LOADP(BindBuffer);
    LOADP(BindBufferBase);
    LOADP(BufferData);
    LOADP(BufferSubData);
    LOADP(VertexAttribPointer);
    LOADP(VertexAttribIPointer);
    LOADP(EnableVertexAttribArray);
    LOADP(DisableVertexAttribArray);
    LOADP(VertexAttribDivisor);
    LOADP(VertexAttrib4f);
    LOADP(DeleteBuffers);
    LOADP(DeleteVertexArrays);
    LOADP(GenTextures);
    LOADP(DeleteTextures);
    LOADP(ActiveTexture);
    LOADP(BindTexture);
    LOADP(TexImage2D);
    LOADP(TexParameteri);
    LOADP(TexBuffer);
    LOADP(GenFramebuffers);
    LOADP(DeleteFramebuffers);
    LOADP(BindFramebuffer);
    LOADP(CheckFramebufferStatus);
    LOADP(FramebufferTexture2D);
    LOADP(GenRenderbuffers);
    LOADP(DeleteRenderbuffers);
    LOADP(BindRenderbuffer);
    LOADP(RenderbufferStorage);
    LOADP(FramebufferRenderbuffer);
    LOADP(ReadPixels);
    LOADP(DrawBuffer);
    LOADP(DrawBuffers);
    LOADP(ReadBuffer);

#undef LOAD
#undef LOADX
#undef LOADP

    gl_loaded = 1;
    return 0;
}

static const char *const glsl_vertex_src[] = {
    "#version 330 core\n"
    "layout(location=0) in vec3 aPosition;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec2 aUV;\n"
    "layout(location=3) in vec4 aColor;\n"
    "layout(location=4) in vec3 aTangent;\n"
    "layout(location=5) in uvec4 aBoneIdx;\n"
    "layout(location=6) in vec4 aBoneWt;\n"
    "layout(location=7) in vec4 aInstanceRow0;\n"
    "layout(location=8) in vec4 aInstanceRow1;\n"
    "layout(location=9) in vec4 aInstanceRow2;\n"
    "layout(location=10) in vec4 aInstanceRow3;\n"
    "layout(location=11) in vec4 aPrevInstanceRow0;\n"
    "layout(location=12) in vec4 aPrevInstanceRow1;\n"
    "layout(location=13) in vec4 aPrevInstanceRow2;\n"
    "layout(location=14) in vec4 aPrevInstanceRow3;\n"
    "layout(std140) uniform Bones { mat4 uBonePalette[128]; };\n"
    "layout(std140) uniform PrevBones { mat4 uPrevBonePalette[128]; };\n"
    "uniform mat4 uModelMatrix;\n"
    "uniform mat4 uPrevModelMatrix;\n"
    "uniform mat4 uViewProjection;\n"
    "uniform mat4 uPrevViewProjection;\n"
    "uniform mat4 uNormalMatrix;\n"
    "uniform int uUseInstancing;\n"
    "uniform int uHasPrevModelMatrix;\n"
    "uniform int uHasPrevInstanceMatrices;\n"
    "uniform int uHasSkinning;\n"
    "uniform int uHasPrevSkinning;\n"
    "uniform int uMorphShapeCount;\n"
    "uniform int uVertexCount;\n"
    "uniform samplerBuffer uMorphDeltas;\n"
    "uniform samplerBuffer uMorphNormalDeltas;\n"
    "uniform float uMorphWeights[32];\n"
    "uniform float uPrevMorphWeights[32];\n"
    "uniform int uHasMorphNormalDeltas;\n"
    "uniform int uHasPrevMorphWeights;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec3 vTangent;\n"
    "out vec2 vUV;\n"
    "out vec4 vColor;\n"
    "out vec4 vCurrClip;\n"
    "out vec4 vPrevClip;\n"
    "flat out float vHasObjectHistory;\n"
    "void applyMorph(inout vec3 pos, inout vec3 nrm, int usePrevWeights) {\n"
    "    for (int s = 0; s < uMorphShapeCount; s++) {\n"
    "        float w = (usePrevWeights != 0) ? uPrevMorphWeights[s] : uMorphWeights[s];\n"
    "        if (abs(w) > 0.0001) {\n"
    "            int base = (s * uVertexCount + gl_VertexID) * 3;\n"
    "            pos.x += texelFetch(uMorphDeltas, base + 0).r * w;\n"
    "            pos.y += texelFetch(uMorphDeltas, base + 1).r * w;\n"
    "            pos.z += texelFetch(uMorphDeltas, base + 2).r * w;\n"
    "            if (uHasMorphNormalDeltas != 0 && usePrevWeights == 0) {\n"
    "                nrm.x += texelFetch(uMorphNormalDeltas, base + 0).r * w;\n"
    "                nrm.y += texelFetch(uMorphNormalDeltas, base + 1).r * w;\n"
    "                nrm.z += texelFetch(uMorphNormalDeltas, base + 2).r * w;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n",
    "vec4 skinPosition(vec4 localPos, int usePrevPalette) {\n"
    "    if (uHasSkinning == 0) return localPos;\n"
    "    vec4 skinnedPos = vec4(0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float bw = aBoneWt[i];\n"
    "        if (bw > 0.0001) {\n"
    "            int b = min(int(aBoneIdx[i]), 127);\n"
    "            mat4 bm = (usePrevPalette != 0 && uHasPrevSkinning != 0) ? uPrevBonePalette[b] : "
    "uBonePalette[b];\n"
    "            skinnedPos += bm * localPos * bw;\n"
    "        }\n"
    "    }\n"
    "    return skinnedPos;\n"
    "}\n"
    "vec3 skinNormal(vec3 localNormal) {\n"
    "    if (uHasSkinning == 0) return localNormal;\n"
    "    vec3 skinnedNormal = vec3(0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float bw = aBoneWt[i];\n"
    "        if (bw > 0.0001) {\n"
    "            int b = min(int(aBoneIdx[i]), 127);\n"
    "            mat4 bm = uBonePalette[b];\n"
    "            skinnedNormal += (bm * vec4(localNormal, 0.0)).xyz * bw;\n"
    "        }\n"
    "    }\n"
    "    return skinnedNormal;\n"
    "}\n",
    "void main() {\n"
    "    vec3 pos = aPosition;\n"
    "    vec3 nrm = aNormal;\n"
    "    vec3 prevPos = aPosition;\n"
    "    vec3 prevNrm = aNormal;\n"
    "    applyMorph(pos, nrm, 0);\n"
    "    applyMorph(prevPos, prevNrm, uHasPrevMorphWeights);\n"
    "    vec4 localPos = vec4(pos, 1.0);\n"
    "    vec4 prevLocalPos = vec4(prevPos, 1.0);\n"
    "    vec3 localNormal = normalize(nrm);\n"
    "    localPos = skinPosition(localPos, 0);\n"
    "    prevLocalPos = skinPosition(prevLocalPos, 1);\n"
    "    localNormal = skinNormal(localNormal);\n"
    "    mat4 model = uModelMatrix;\n"
    "    mat4 prevModel = (uHasPrevModelMatrix != 0) ? uPrevModelMatrix : uModelMatrix;\n"
    "    if (uUseInstancing != 0) {\n"
    "        model = transpose(mat4(aInstanceRow0, aInstanceRow1, aInstanceRow2, aInstanceRow3));\n"
    "        prevModel = (uHasPrevInstanceMatrices != 0)\n"
    "            ? transpose(mat4(aPrevInstanceRow0, aPrevInstanceRow1, aPrevInstanceRow2, "
    "aPrevInstanceRow3))\n"
    "            : model;\n"
    "    }\n"
    "    mat3 normalMatrix = (uUseInstancing != 0) ? transpose(inverse(mat3(model))) : "
    "mat3(uNormalMatrix);\n"
    "    vec4 wp = model * localPos;\n"
    "    vec4 prevWp = prevModel * prevLocalPos;\n"
    "    gl_Position = uViewProjection * wp;\n"
    "    vWorldPos = wp.xyz;\n"
    "    vNormal = normalMatrix * localNormal;\n"
    "    vTangent = mat3(model) * aTangent;\n"
    "    vUV = aUV;\n"
    "    vColor = aColor;\n"
    "    vCurrClip = gl_Position;\n"
    "    vPrevClip = uPrevViewProjection * prevWp;\n"
    "    vHasObjectHistory = float((uHasPrevModelMatrix != 0) || (uHasPrevInstanceMatrices != 0) "
    "||\n"
    "                             (uHasPrevSkinning != 0) || (uHasPrevMorphWeights != 0));\n"
    "}\n",
};

static const char *const glsl_fragment_src[] = {
    "#version 330 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec3 vTangent;\n"
    "in vec2 vUV;\n"
    "in vec4 vColor;\n"
    "in vec4 vCurrClip;\n"
    "in vec4 vPrevClip;\n"
    "flat in float vHasObjectHistory;\n"
    "layout(location=0) out vec4 FragColor;\n"
    "layout(location=1) out vec4 MotionColor;\n"
    "uniform vec3 uCameraPos;\n"
    "uniform vec3 uAmbientColor;\n"
    "uniform vec4 uDiffuseColor;\n"
    "uniform vec4 uSpecularColor;\n"
    "uniform vec3 uEmissiveColor;\n"
    "uniform float uAlpha;\n"
    "uniform vec4 uPbrScalars0;\n"
    "uniform vec4 uPbrScalars1;\n"
    "uniform int uUnlit;\n"
    "uniform int uShadingModel;\n"
    "uniform int uLightCount;\n"
    "uniform int uHasTexture;\n"
    "uniform int uHasNormalMap;\n"
    "uniform int uHasSpecularMap;\n"
    "uniform int uHasEmissiveMap;\n"
    "uniform int uHasEnvMap;\n"
    "uniform float uReflectivity;\n"
    "uniform int uWorkflow;\n"
    "uniform int uAlphaMode;\n"
    "uniform int uHasMetallicRoughnessMap;\n"
    "uniform int uHasAOMap;\n"
    "uniform int uHasSplat;\n"
    "uniform int uFogEnabled;\n"
    "uniform float uFogNear;\n"
    "uniform float uFogFar;\n"
    "uniform vec3 uFogColor;\n"
    "uniform int uShadowEnabled;\n"
    "uniform mat4 uShadowVP;\n"
    "uniform float uShadowBias;\n"
    "uniform int uLightType[8];\n"
    "uniform vec3 uLightDir[8];\n"
    "uniform vec3 uLightPos[8];\n"
    "uniform vec3 uLightColor[8];\n"
    "uniform float uLightIntensity[8];\n"
    "uniform float uLightAtten[8];\n"
    "uniform float uLightInnerCos[8];\n"
    "uniform float uLightOuterCos[8];\n"
    "uniform sampler2D uDiffuseTex;\n"
    "uniform sampler2D uNormalTex;\n"
    "uniform sampler2D uSpecularTex;\n"
    "uniform sampler2D uEmissiveTex;\n"
    "uniform sampler2D uShadowTex;\n"
    "uniform samplerCube uEnvMap;\n"
    "uniform sampler2D uMetallicRoughnessTex;\n"
    "uniform sampler2D uAOTex;\n"
    "uniform sampler2D uSplatTex;\n"
    "uniform sampler2D uSplatLayer0;\n"
    "uniform sampler2D uSplatLayer1;\n"
    "uniform sampler2D uSplatLayer2;\n"
    "uniform sampler2D uSplatLayer3;\n"
    "uniform vec4 uSplatScales;\n"
    "uniform float uCustomParams[8];\n"
    "float distributionGGX(float NdotH, float roughness) {\n"
    "    float a = roughness * roughness;\n"
    "    float a2 = a * a;\n"
    "    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
    "    return a2 / (3.14159265 * denom * denom + 1e-6);\n"
    "}\n"
    "float geometrySchlickGGX(float NdotV, float roughness) {\n"
    "    float r = roughness + 1.0;\n"
    "    float k = (r * r) / 8.0;\n"
    "    return NdotV / (NdotV * (1.0 - k) + k + 1e-6);\n"
    "}\n"
    "float geometrySmith(float NdotV, float NdotL, float roughness) {\n"
    "    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);\n"
    "}\n"
    "vec3 fresnelSchlick(float cosTheta, vec3 F0) {\n"
    "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
    "}\n"
    "float sampleShadow(vec3 worldPos) {\n"
    "    if (uShadowEnabled == 0) return 1.0;\n"
    "    vec4 lc = uShadowVP * vec4(worldPos, 1.0);\n"
    "    float invW = 1.0 / max(lc.w, 0.0001);\n"
    "    vec3 ndc = lc.xyz * invW;\n"
    "    vec2 uv = ndc.xy * 0.5 + 0.5;\n"
    "    float depth = ndc.z * 0.5 + 0.5;\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth > 1.0) return 1.0;\n"
    "    vec2 texel = 1.0 / vec2(textureSize(uShadowTex, 0));\n"
    "    float lit = 0.0;\n"
    "    for (int y = -1; y <= 1; y++) {\n"
    "        for (int x = -1; x <= 1; x++) {\n"
    "            float smp = texture(uShadowTex, uv + vec2(x, y) * texel).r;\n"
    "            lit += (depth - uShadowBias <= smp) ? 1.0 : 0.0;\n"
    "        }\n"
    "    }\n"
    "    return lit / 9.0;\n"
    "}\n",
    "void main() {\n"
    "    vec3 baseColor = uDiffuseColor.rgb * vColor.rgb;\n"
    "    float texAlpha = 1.0;\n"
    "    float materialAlpha = uDiffuseColor.a * uAlpha * vColor.a;\n"
    "    if (uHasTexture != 0) {\n"
    "        vec4 texSample = texture(uDiffuseTex, vUV);\n"
    "        baseColor *= texSample.rgb;\n"
    "        texAlpha = texSample.a;\n"
    "    }\n"
    "    if (uHasSplat != 0) {\n"
    "        vec4 sp = texture(uSplatTex, vUV);\n"
    "        float sum = sp.r + sp.g + sp.b + sp.a;\n"
    "        if (sum > 0.0001) {\n"
    "            sp /= sum;\n"
    "            vec3 splatColor = texture(uSplatLayer0, vUV * uSplatScales.x).rgb * sp.r +\n"
    "                              texture(uSplatLayer1, vUV * uSplatScales.y).rgb * sp.g +\n"
    "                              texture(uSplatLayer2, vUV * uSplatScales.z).rgb * sp.b +\n"
    "                              texture(uSplatLayer3, vUV * uSplatScales.w).rgb * sp.a;\n"
    "            baseColor = splatColor * uDiffuseColor.rgb * vColor.rgb;\n"
    "        }\n"
    "    }\n"
    "    vec3 N = normalize(vNormal);\n"
    "    if (uHasNormalMap != 0) {\n"
    "        vec3 mapN = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;\n"
    "        mapN.xy *= uPbrScalars1.x;\n"
    "        vec3 T = normalize(vTangent - N * dot(vTangent, N));\n"
    "        vec3 B = normalize(cross(N, T));\n"
    "        N = normalize(mat3(T, B, N) * mapN);\n"
    "    }\n"
    "    vec3 emissive = uEmissiveColor * uPbrScalars0.w;\n"
    "    if (uHasEmissiveMap != 0) emissive *= texture(uEmissiveTex, vUV).rgb;\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    "    float finalAlpha = materialAlpha * texAlpha;\n"
    "    if (uWorkflow != 0) {\n"
    "        if (uAlphaMode == 1) {\n"
    "            if (finalAlpha < uPbrScalars1.y) discard;\n"
    "            finalAlpha = materialAlpha;\n"
    "        } else if (uAlphaMode == 0) {\n"
    "            finalAlpha = materialAlpha;\n"
    "        }\n"
    "    }\n"
    "    if (uUnlit != 0) {\n"
    "        vec3 unlitColor = baseColor + emissive;\n"
    "        if (uHasEnvMap != 0) {\n"
    "            vec3 R = reflect(-V, N);\n"
    "            vec3 envColor = texture(uEnvMap, R).rgb;\n"
    "            unlitColor = mix(unlitColor, envColor, clamp(uReflectivity, 0.0, 1.0));\n"
    "        }\n"
    "        if (uFogEnabled != 0) {\n"
    "            float dist = length(uCameraPos - vWorldPos);\n"
    "            float fogFactor = clamp((dist - uFogNear) / max(uFogFar - uFogNear, 0.001), 0.0, "
    "1.0);\n"
    "            unlitColor = mix(unlitColor, uFogColor, fogFactor);\n"
    "        }\n"
    "        FragColor = vec4(unlitColor, finalAlpha);\n"
    "        vec2 currNdc = vCurrClip.xy / max(vCurrClip.w, 0.0001);\n"
    "        vec2 prevNdc = vPrevClip.xy / max(vPrevClip.w, 0.0001);\n"
    "        vec2 velocity = (currNdc - prevNdc) * 0.5;\n"
    "        MotionColor = vec4(clamp(velocity * 0.5 + 0.5, 0.0, 1.0), vHasObjectHistory, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 result = vec3(0.0);\n"
    "    if (uWorkflow != 0) {\n"
    "        float metallic = clamp(uPbrScalars0.x, 0.0, 1.0);\n"
    "        float roughness = clamp(uPbrScalars0.y, 0.045, 1.0);\n"
    "        float ao = clamp(uPbrScalars0.z, 0.0, 1.0);\n"
    "        if (uHasMetallicRoughnessMap != 0) {\n"
    "            vec4 mr = texture(uMetallicRoughnessTex, vUV);\n"
    "            roughness = clamp(roughness * mr.g, 0.045, 1.0);\n"
    "            metallic = clamp(metallic * mr.b, 0.0, 1.0);\n"
    "        }\n"
    "        if (uHasAOMap != 0) {\n"
    "            vec4 aoSample = texture(uAOTex, vUV);\n"
    "            ao = clamp(ao * aoSample.r, 0.0, 1.0);\n"
    "        }\n"
    "        result = uAmbientColor * baseColor * ao;\n"
    "        for (int i = 0; i < uLightCount; i++) {\n"
    "            vec3 L = vec3(0.0);\n"
    "            float atten = 1.0;\n"
    "            if (uLightType[i] == 0) {\n"
    "                L = normalize(-uLightDir[i]);\n"
    "                atten *= mix(0.15, 1.0, sampleShadow(vWorldPos));\n"
    "            } else if (uLightType[i] == 1) {\n"
    "                vec3 toLight = uLightPos[i] - vWorldPos;\n"
    "                float d = length(toLight);\n"
    "                L = toLight / max(d, 0.0001);\n"
    "                atten = 1.0 / (1.0 + uLightAtten[i] * d * d);\n"
    "            } else if (uLightType[i] == 2) {\n"
    "                result += uLightColor[i] * uLightIntensity[i] * baseColor * ao;\n"
    "                continue;\n"
    "            } else if (uLightType[i] == 3) {\n"
    "                vec3 toLight = uLightPos[i] - vWorldPos;\n"
    "                float d = length(toLight);\n"
    "                L = toLight / max(d, 0.0001);\n"
    "                float spotDot = dot(normalize(-uLightDir[i]), L);\n"
    "                if (spotDot < uLightOuterCos[i]) {\n"
    "                    atten = 0.0;\n"
    "                } else if (spotDot < uLightInnerCos[i]) {\n"
    "                    float t = (spotDot - uLightOuterCos[i]) / "
    "max(uLightInnerCos[i] - uLightOuterCos[i], 0.0001);\n"
    "                    atten = (t * t * (3.0 - 2.0 * t)) / (1.0 + uLightAtten[i] * d * d);\n"
    "                } else {\n"
    "                    atten = 1.0 / (1.0 + uLightAtten[i] * d * d);\n"
    "                }\n"
    "            } else {\n"
    "                continue;\n"
    "            }\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            if (NdotL <= 0.0) continue;\n"
    "            vec3 H = normalize(L + V);\n"
    "            float NdotV = max(dot(N, V), 0.001);\n"
    "            float NdotH = max(dot(N, H), 0.0);\n"
    "            float VdotH = max(dot(V, H), 0.0);\n"
    "            vec3 F0 = mix(vec3(0.04), baseColor, metallic);\n"
    "            vec3 F = fresnelSchlick(VdotH, F0);\n"
    "            float D = distributionGGX(NdotH, roughness);\n"
    "            float G = geometrySmith(NdotV, NdotL, roughness);\n"
    "            vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);\n"
    "            vec3 kS = F;\n"
    "            vec3 kD = (1.0 - kS) * (1.0 - metallic);\n"
    "            vec3 diffuse = kD * baseColor / 3.14159265;\n"
    "            vec3 radiance = uLightColor[i] * uLightIntensity[i] * atten;\n"
    "            result += (diffuse + specular) * radiance * NdotL;\n"
    "        }\n"
    "    } else {\n"
    "        vec3 specColor = uSpecularColor.rgb;\n"
    "        if (uHasSpecularMap != 0) specColor *= texture(uSpecularTex, vUV).rgb;\n"
    "        result = uAmbientColor * baseColor;\n"
    "        for (int i = 0; i < uLightCount; i++) {\n"
    "            vec3 L = vec3(0.0);\n"
    "            float atten = 1.0;\n"
    "            if (uLightType[i] == 0) {\n"
    "                L = normalize(-uLightDir[i]);\n"
    "                atten *= mix(0.15, 1.0, sampleShadow(vWorldPos));\n"
    "            } else if (uLightType[i] == 1) {\n"
    "                vec3 toLight = uLightPos[i] - vWorldPos;\n"
    "                float d = length(toLight);\n"
    "                L = toLight / max(d, 0.0001);\n"
    "                atten = 1.0 / (1.0 + uLightAtten[i] * d * d);\n"
    "            } else if (uLightType[i] == 2) {\n"
    "                result += uLightColor[i] * uLightIntensity[i] * baseColor;\n"
    "                continue;\n"
    "            } else if (uLightType[i] == 3) {\n"
    "                vec3 toLight = uLightPos[i] - vWorldPos;\n"
    "                float d = length(toLight);\n"
    "                L = toLight / max(d, 0.0001);\n"
    "                float cone = smoothstep(uLightOuterCos[i], uLightInnerCos[i], "
    "dot(normalize(-uLightDir[i]), L));\n"
    "                atten = cone / (1.0 + uLightAtten[i] * d * d);\n"
    "            } else {\n"
    "                continue;\n"
    "            }\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            if (uShadingModel == 1) {\n"
    "                float bands = max(uCustomParams[0], 2.0);\n"
    "                NdotL = floor(NdotL * bands) / max(bands - 1.0, 1.0);\n"
    "            }\n"
    "            result += uLightColor[i] * uLightIntensity[i] * NdotL * baseColor * atten;\n"
    "            if (NdotL > 0.0 && uSpecularColor.w > 0.0) {\n"
    "                vec3 H = normalize(L + V);\n"
    "                float spec = pow(max(dot(N, H), 0.0), uSpecularColor.w);\n"
    "                if (uShadingModel == 1) spec = spec >= max(uCustomParams[1], 0.5) ? 1.0 : 0.0;\n"
    "                result += uLightColor[i] * uLightIntensity[i] * spec * specColor * atten;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    result += emissive;\n"
    "    if (uHasEnvMap != 0) {\n"
    "        vec3 R = reflect(-V, N);\n"
    "        vec3 envColor = texture(uEnvMap, R).rgb;\n"
    "        result = mix(result, envColor, clamp(uReflectivity, 0.0, 1.0));\n"
    "    }\n"
    "    if (uShadingModel == 1 && uWorkflow != 0) {\n"
    "        float bands = max(uCustomParams[0], 2.0);\n"
    "        result = floor(result * bands) / bands;\n"
    "    } else if (uShadingModel == 4) {\n"
    "        float ndv = clamp(dot(N, V), 0.0, 1.0);\n"
    "        float power = max(uCustomParams[0], 1.0);\n"
    "        float bias = uCustomParams[1];\n"
    "        finalAlpha *= clamp(pow(1.0 - ndv, power) + bias, 0.0, 1.0);\n"
    "    } else if (uShadingModel == 5) {\n"
    "        float strength = max(uCustomParams[0], 1.0);\n"
    "        result += emissive * (strength - 1.0);\n"
    "    }\n"
    "    if (uFogEnabled != 0) {\n"
    "        float dist = length(uCameraPos - vWorldPos);\n"
    "        float fogFactor = clamp((dist - uFogNear) / max(uFogFar - uFogNear, 0.001), 0.0, "
    "1.0);\n"
    "        result = mix(result, uFogColor, fogFactor);\n"
    "    }\n"
    "    FragColor = vec4(result, finalAlpha);\n"
    "    vec2 currNdc = vCurrClip.xy / max(vCurrClip.w, 0.0001);\n"
    "    vec2 prevNdc = vPrevClip.xy / max(vPrevClip.w, 0.0001);\n"
    "    vec2 velocity = (currNdc - prevNdc) * 0.5;\n"
    "    MotionColor = vec4(clamp(velocity * 0.5 + 0.5, 0.0, 1.0), vHasObjectHistory, 1.0);\n"
    "}\n",
};

static const char *glsl_shadow_vertex_src =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPosition;\n"
    "layout(location=5) in uvec4 aBoneIdx;\n"
    "layout(location=6) in vec4 aBoneWt;\n"
    "layout(std140) uniform Bones { mat4 uBonePalette[128]; };\n"
    "uniform mat4 uModelMatrix;\n"
    "uniform mat4 uViewProjection;\n"
    "uniform int uHasSkinning;\n"
    "uniform int uMorphShapeCount;\n"
    "uniform int uVertexCount;\n"
    "uniform samplerBuffer uMorphDeltas;\n"
    "uniform float uMorphWeights[32];\n"
    "void applyMorph(inout vec3 pos) {\n"
    "    for (int s = 0; s < uMorphShapeCount; s++) {\n"
    "        float w = uMorphWeights[s];\n"
    "        if (abs(w) > 0.0001) {\n"
    "            int base = (s * uVertexCount + gl_VertexID) * 3;\n"
    "            pos.x += texelFetch(uMorphDeltas, base + 0).r * w;\n"
    "            pos.y += texelFetch(uMorphDeltas, base + 1).r * w;\n"
    "            pos.z += texelFetch(uMorphDeltas, base + 2).r * w;\n"
    "        }\n"
    "    }\n"
    "}\n"
    "void main() {\n"
    "    vec3 pos = aPosition;\n"
    "    applyMorph(pos);\n"
    "    vec4 localPos = vec4(pos, 1.0);\n"
    "    if (uHasSkinning != 0) {\n"
    "        vec4 skinnedPos = vec4(0.0);\n"
    "        for (int i = 0; i < 4; i++) {\n"
    "            float bw = aBoneWt[i];\n"
    "            if (bw > 0.0001) {\n"
    "                int b = min(int(aBoneIdx[i]), 127);\n"
    "                skinnedPos += uBonePalette[b] * localPos * bw;\n"
    "            }\n"
    "        }\n"
    "        localPos = skinnedPos;\n"
    "    }\n"
    "    gl_Position = uViewProjection * (uModelMatrix * localPos);\n"
    "}\n";

static const char *glsl_shadow_fragment_src = "#version 330 core\n"
                                              "void main() {}\n";

static const char *glsl_skybox_vertex_src =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPosition;\n"
    "out vec3 vDir;\n"
    "uniform mat4 uProjection;\n"
    "uniform mat4 uViewRotation;\n"
    "void main() {\n"
    "    vDir = aPosition;\n"
    "    vec4 pos = uProjection * uViewRotation * vec4(aPosition, 1.0);\n"
    "    gl_Position = pos.xyww;\n"
    "}\n";

static const char *glsl_skybox_fragment_src = "#version 330 core\n"
                                              "in vec3 vDir;\n"
                                              "out vec4 FragColor;\n"
                                              "uniform samplerCube uSkybox;\n"
                                              "void main() {\n"
                                              "    FragColor = texture(uSkybox, normalize(vDir));\n"
                                              "}\n";

static const float gl_skybox_vertices[] = {
    -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
    1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f,
    -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,
    1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,
    -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
    -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,
    1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,
};

static const char *glsl_postfx_vertex_src =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vec2 pos;\n"
    "    if (gl_VertexID == 0) pos = vec2(-1.0, -1.0);\n"
    "    else if (gl_VertexID == 1) pos = vec2(3.0, -1.0);\n"
    "    else pos = vec2(-1.0, 3.0);\n"
    "    vUV = pos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *const glsl_postfx_fragment_src[] = {
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uSceneTex;\n"
    "uniform sampler2D uSceneDepthTex;\n"
    "uniform sampler2D uSceneMotionTex;\n"
    "uniform vec2 uInvResolution;\n"
    "uniform int uBloomEnabled;\n"
    "uniform float uBloomThreshold;\n"
    "uniform float uBloomIntensity;\n"
    "uniform int uTonemapMode;\n"
    "uniform float uTonemapExposure;\n"
    "uniform int uFxaaEnabled;\n"
    "uniform int uColorGradeEnabled;\n"
    "uniform float uCgBrightness;\n"
    "uniform float uCgContrast;\n"
    "uniform float uCgSaturation;\n"
    "uniform int uVignetteEnabled;\n"
    "uniform float uVignetteRadius;\n"
    "uniform float uVignetteSoftness;\n"
    "uniform int uSsaoEnabled;\n"
    "uniform float uSsaoRadius;\n"
    "uniform float uSsaoIntensity;\n"
    "uniform int uSsaoSamples;\n"
    "uniform int uDofEnabled;\n"
    "uniform float uDofFocusDistance;\n"
    "uniform float uDofAperture;\n"
    "uniform float uDofMaxBlur;\n"
    "uniform int uMotionBlurEnabled;\n"
    "uniform float uMotionBlurIntensity;\n"
    "uniform int uMotionBlurSamples;\n"
    "uniform vec3 uCameraPos;\n"
    "uniform mat4 uInvViewProjection;\n"
    "uniform mat4 uPrevViewProjection;\n"
    "vec3 sampleScene(vec2 uv) { return texture(uSceneTex, uv).rgb; }\n"
    "float sampleDepth(vec2 uv) { return texture(uSceneDepthTex, uv).r; }\n"
    "vec3 sampleMotion(vec2 uv) { return texture(uSceneMotionTex, uv).rgb; }\n"
    "vec3 reconstructWorld(vec2 uv, float depth) {\n"
    "    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
    "    vec4 world = uInvViewProjection * clip;\n"
    "    return world.xyz / max(world.w, 0.0001);\n"
    "}\n"
    "float computeSsao(vec2 uv, float centerDepth) {\n"
    "    float radius = max(uSsaoRadius, 0.0001) * max(uInvResolution.x, uInvResolution.y) * "
    "120.0;\n"
    "    vec2 offsets[8] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, "
    "-1.0),\n"
    "                            vec2(0.707, 0.707), vec2(-0.707, 0.707), vec2(0.707, -0.707), "
    "vec2(-0.707, -0.707));\n"
    "    int count = clamp(uSsaoSamples, 1, 8);\n"
    "    float occ = 0.0;\n"
    "    for (int i = 0; i < count; i++) {\n"
    "        float sd = sampleDepth(uv + offsets[i] * radius);\n"
    "        occ += max(sd - centerDepth, 0.0);\n"
    "    }\n"
    "    float ao = 1.0 - clamp((occ / float(count)) * uSsaoIntensity * 32.0, 0.0, 1.0);\n"
    "    return ao;\n"
    "}\n",
    "vec3 applyDof(vec2 uv, vec3 color, vec3 worldPos) {\n"
    "    float dist = length(worldPos - uCameraPos);\n"
    "    float blur = clamp(abs(dist - uDofFocusDistance) * max(uDofAperture, 0.0) * 0.02, 0.0, "
    "uDofMaxBlur);\n"
    "    if (blur < 0.001) return color;\n"
    "    vec2 stepUv = uInvResolution * blur;\n"
    "    vec3 acc = color * 0.4;\n"
    "    acc += sampleScene(uv + vec2(stepUv.x, 0.0)) * 0.15;\n"
    "    acc += sampleScene(uv - vec2(stepUv.x, 0.0)) * 0.15;\n"
    "    acc += sampleScene(uv + vec2(0.0, stepUv.y)) * 0.15;\n"
    "    acc += sampleScene(uv - vec2(0.0, stepUv.y)) * 0.15;\n"
    "    return acc;\n"
    "}\n"
    "vec2 cameraVelocity(vec2 uv, vec3 worldPos) {\n"
    "    vec4 prevClip = uPrevViewProjection * vec4(worldPos, 1.0);\n"
    "    float invW = 1.0 / max(prevClip.w, 0.0001);\n"
    "    vec2 prevUv = prevClip.xy * invW * 0.5 + 0.5;\n"
    "    return (uv - prevUv);\n"
    "}\n"
    "vec3 applyMotionBlur(vec2 uv, vec3 color, vec3 worldPos) {\n"
    "    vec3 motionSample = sampleMotion(uv);\n"
    "    vec2 velocity = motionSample.xy * 2.0 - 1.0;\n"
    "    if (motionSample.z < 0.5) velocity = cameraVelocity(uv, worldPos);\n"
    "    velocity *= uMotionBlurIntensity;\n"
    "    float vlen = length(velocity);\n"
    "    if (vlen < 0.0005) return color;\n"
    "    int taps = clamp(uMotionBlurSamples, 2, 8);\n"
    "    vec3 acc = vec3(0.0);\n"
    "    float weight = 0.0;\n"
    "    for (int i = 0; i < taps; i++) {\n"
    "        float t = (float(i) / float(taps - 1)) - 0.5;\n"
    "        vec3 s = sampleScene(uv + velocity * t);\n"
    "        acc += s;\n"
    "        weight += 1.0;\n"
    "    }\n"
    "    return acc / max(weight, 1.0);\n"
    "}\n"
    "vec3 applyFxaa(vec2 uv, vec3 color) {\n"
    "    float lumaM = dot(color, vec3(0.299, 0.587, 0.114));\n"
    "    float lumaN = dot(sampleScene(uv + vec2(0.0, -uInvResolution.y)), vec3(0.299, 0.587, "
    "0.114));\n"
    "    float lumaS = dot(sampleScene(uv + vec2(0.0, uInvResolution.y)), vec3(0.299, 0.587, "
    "0.114));\n"
    "    float lumaE = dot(sampleScene(uv + vec2(uInvResolution.x, 0.0)), vec3(0.299, 0.587, "
    "0.114));\n"
    "    float lumaW = dot(sampleScene(uv + vec2(-uInvResolution.x, 0.0)), vec3(0.299, 0.587, "
    "0.114));\n"
    "    float edge = abs(lumaN + lumaS - 2.0 * lumaM) + abs(lumaE + lumaW - 2.0 * lumaM);\n"
    "    if (edge < 0.08) return color;\n"
    "    vec3 avg = (sampleScene(uv + vec2(uInvResolution.x, 0.0)) +\n"
    "                sampleScene(uv + vec2(-uInvResolution.x, 0.0)) +\n"
    "                sampleScene(uv + vec2(0.0, uInvResolution.y)) +\n"
    "                sampleScene(uv + vec2(0.0, -uInvResolution.y))) * 0.25;\n"
    "    return mix(color, avg, 0.5);\n"
    "}\n",
    "void main() {\n"
    "    vec3 color = sampleScene(vUV);\n"
    "    float depth = sampleDepth(vUV);\n"
    "    vec3 worldPos = reconstructWorld(vUV, depth);\n"
    "    if (uMotionBlurEnabled != 0) color = applyMotionBlur(vUV, color, worldPos);\n"
    "    if (uDofEnabled != 0) color = applyDof(vUV, color, worldPos);\n"
    "    if (uSsaoEnabled != 0) color *= computeSsao(vUV, depth);\n"
    "    if (uFxaaEnabled != 0) color = applyFxaa(vUV, color);\n"
    "    if (uBloomEnabled != 0) {\n"
    "        vec3 bright = max(color - vec3(uBloomThreshold), vec3(0.0));\n"
    "        bright += max(sampleScene(vUV + vec2(uInvResolution.x, 0.0)) - vec3(uBloomThreshold), "
    "vec3(0.0));\n"
    "        bright += max(sampleScene(vUV - vec2(uInvResolution.x, 0.0)) - vec3(uBloomThreshold), "
    "vec3(0.0));\n"
    "        bright += max(sampleScene(vUV + vec2(0.0, uInvResolution.y)) - vec3(uBloomThreshold), "
    "vec3(0.0));\n"
    "        bright += max(sampleScene(vUV - vec2(0.0, uInvResolution.y)) - vec3(uBloomThreshold), "
    "vec3(0.0));\n"
    "        color += bright * (uBloomIntensity / 5.0);\n"
    "    }\n"
    "    if (uTonemapMode == 1) {\n"
    "        color *= uTonemapExposure;\n"
    "        color = color / (vec3(1.0) + color);\n"
    "    } else if (uTonemapMode == 2) {\n"
    "        color *= uTonemapExposure;\n"
    "        color = clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + "
    "0.14), 0.0, 1.0);\n"
    "    }\n"
    "    if (uColorGradeEnabled != 0) {\n"
    "        color += vec3(uCgBrightness);\n"
    "        color = (color - 0.5) * uCgContrast + 0.5;\n"
    "        float luma = dot(color, vec3(0.299, 0.587, 0.114));\n"
    "        color = mix(vec3(luma), color, uCgSaturation);\n"
    "    }\n"
    "    if (uVignetteEnabled != 0) {\n"
    "        vec2 p = vUV - vec2(0.5);\n"
    "        float dist = length(p) * 1.41421356;\n"
    "        float vig = smoothstep(uVignetteRadius + uVignetteSoftness, uVignetteRadius, dist);\n"
    "        color *= vig;\n"
    "    }\n"
    "    FragColor = vec4(color, 1.0);\n"
    "}\n",
};

static GLuint compile_shader_parts(GLenum type, const char *const *src, GLsizei src_count) {
    GLuint shader = gl.CreateShader(type);
    if (!shader)
        return 0;
    gl.ShaderSource(shader, src_count, src, NULL);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl.GetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "[OpenGL] shader compile failed: %s\n", log);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint compile_shader(GLenum type, const char *src) {
    const char *parts[] = {src};
    return compile_shader_parts(type, parts, 1);
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint program = gl.CreateProgram();
    if (!program)
        return 0;
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl.GetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "[OpenGL] program link failed: %s\n", log);
        gl.DeleteProgram(program);
        return 0;
    }
    return program;
}

static int ensure_buffer_capacity(GLenum target,
                                  GLuint buffer,
                                  size_t *capacity,
                                  size_t needed,
                                  size_t initial_capacity,
                                  GLenum usage) {
    if (needed == 0)
        needed = 4;
    if (*capacity >= needed)
        return 0;
    size_t new_capacity = *capacity > 0 ? *capacity : initial_capacity;
    while (new_capacity < needed)
        new_capacity *= 2;
    gl.BindBuffer(target, buffer);
    gl.BufferData(target, (GLsizeiptr)new_capacity, NULL, usage);
    *capacity = new_capacity;
    return 0;
}

static void texture_cache_clear(gl_context_t *ctx) {
    if (!ctx || !ctx->texture_cache)
        return;
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        if (ctx->texture_cache[i].tex)
            gl.DeleteTextures(1, &ctx->texture_cache[i].tex);
    }
    ctx->texture_cache_count = 0;
}

static void cubemap_cache_clear(gl_context_t *ctx) {
    if (!ctx || !ctx->cubemap_cache)
        return;
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].tex)
            gl.DeleteTextures(1, &ctx->cubemap_cache[i].tex);
    }
    ctx->cubemap_cache_count = 0;
}

static void texture_cache_destroy(gl_context_t *ctx) {
    if (!ctx)
        return;
    texture_cache_clear(ctx);
    cubemap_cache_clear(ctx);
    free(ctx->texture_cache);
    ctx->texture_cache = NULL;
    ctx->texture_cache_capacity = 0;
    free(ctx->cubemap_cache);
    ctx->cubemap_cache = NULL;
    ctx->cubemap_cache_capacity = 0;
}

static GLuint gl_get_cached_texture(gl_context_t *ctx, const void *pixels_ptr) {
    uint64_t generation;
    if (!ctx || !pixels_ptr)
        return 0;
    generation = vgfx3d_get_pixels_generation(pixels_ptr);

    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        if (ctx->texture_cache[i].pixels == pixels_ptr &&
            ctx->texture_cache[i].generation == generation)
            return ctx->texture_cache[i].tex;
    }

    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        if (ctx->texture_cache[i].pixels == pixels_ptr) {
            int32_t w = 0, h = 0;
            uint8_t *rgba = NULL;
            if (vgfx3d_unpack_pixels_rgba(pixels_ptr, &w, &h, &rgba) != 0)
                return 0;
            gl.BindTexture(GL_TEXTURE_2D, ctx->texture_cache[i].tex);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            free(rgba);
            ctx->texture_cache[i].generation = generation;
            return ctx->texture_cache[i].tex;
        }
    }

    int32_t w = 0, h = 0;
    uint8_t *rgba = NULL;
    if (vgfx3d_unpack_pixels_rgba(pixels_ptr, &w, &h, &rgba) != 0)
        return 0;

    GLuint tex = 0;
    gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(rgba);

    if (ctx->texture_cache_count >= ctx->texture_cache_capacity) {
        int32_t new_cap = ctx->texture_cache_capacity > 0 ? ctx->texture_cache_capacity * 2 : 16;
        gl_texture_cache_entry_t *nv = (gl_texture_cache_entry_t *)realloc(
            ctx->texture_cache, (size_t)new_cap * sizeof(gl_texture_cache_entry_t));
        if (!nv) {
            gl.DeleteTextures(1, &tex);
            return 0;
        }
        ctx->texture_cache = nv;
        ctx->texture_cache_capacity = new_cap;
    }

    ctx->texture_cache[ctx->texture_cache_count].pixels = pixels_ptr;
    ctx->texture_cache[ctx->texture_cache_count].generation = generation;
    ctx->texture_cache[ctx->texture_cache_count].tex = tex;
    ctx->texture_cache_count++;
    return tex;
}

static GLuint gl_get_cached_cubemap(gl_context_t *ctx, const rt_cubemap3d *cubemap) {
    uint64_t generation;
    if (!ctx || !cubemap)
        return 0;
    generation = vgfx3d_get_cubemap_generation(cubemap);

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap == cubemap &&
            ctx->cubemap_cache[i].generation == generation)
            return ctx->cubemap_cache[i].tex;
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap == cubemap) {
            gl.BindTexture(GL_TEXTURE_CUBE_MAP, ctx->cubemap_cache[i].tex);
            for (int face = 0; face < 6; face++) {
                int32_t w = 0, h = 0;
                uint8_t *rgba = NULL;
                if (vgfx3d_unpack_pixels_rgba(cubemap->faces[face], &w, &h, &rgba) != 0 || !rgba) {
                    if (rgba)
                        free(rgba);
                    return 0;
                }
                gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face,
                              0,
                              GL_RGBA8,
                              w,
                              h,
                              0,
                              GL_RGBA,
                              GL_UNSIGNED_BYTE,
                              rgba);
                free(rgba);
            }
            ctx->cubemap_cache[i].generation = generation;
            return ctx->cubemap_cache[i].tex;
        }
    }

    GLuint tex = 0;
    gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_CUBE_MAP, tex);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    for (int face = 0; face < 6; face++) {
        int32_t w = 0, h = 0;
        uint8_t *rgba = NULL;
        if (vgfx3d_unpack_pixels_rgba(cubemap->faces[face], &w, &h, &rgba) != 0 || !rgba) {
            if (rgba)
                free(rgba);
            gl.DeleteTextures(1, &tex);
            return 0;
        }
        gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face,
                      0,
                      GL_RGBA8,
                      w,
                      h,
                      0,
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      rgba);
        free(rgba);
    }

    if (ctx->cubemap_cache_count >= ctx->cubemap_cache_capacity) {
        int32_t new_cap = ctx->cubemap_cache_capacity > 0 ? ctx->cubemap_cache_capacity * 2 : 8;
        gl_cubemap_cache_entry_t *nv = (gl_cubemap_cache_entry_t *)realloc(
            ctx->cubemap_cache, (size_t)new_cap * sizeof(gl_cubemap_cache_entry_t));
        if (!nv) {
            gl.DeleteTextures(1, &tex);
            return 0;
        }
        ctx->cubemap_cache = nv;
        ctx->cubemap_cache_capacity = new_cap;
    }

    ctx->cubemap_cache[ctx->cubemap_cache_count].cubemap = cubemap;
    ctx->cubemap_cache[ctx->cubemap_cache_count].generation = generation;
    ctx->cubemap_cache[ctx->cubemap_cache_count].tex = tex;
    ctx->cubemap_cache_count++;
    return tex;
}

static void destroy_scene_targets(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->scene_depth_tex)
        gl.DeleteTextures(1, &ctx->scene_depth_tex);
    if (ctx->scene_motion_tex)
        gl.DeleteTextures(1, &ctx->scene_motion_tex);
    if (ctx->scene_color_tex)
        gl.DeleteTextures(1, &ctx->scene_color_tex);
    if (ctx->scene_fbo)
        gl.DeleteFramebuffers(1, &ctx->scene_fbo);
    ctx->scene_fbo = 0;
    ctx->scene_color_tex = 0;
    ctx->scene_motion_tex = 0;
    ctx->scene_depth_tex = 0;
    ctx->scene_width = 0;
    ctx->scene_height = 0;
}

static int ensure_scene_targets(gl_context_t *ctx, int32_t w, int32_t h) {
    if (!ctx || w <= 0 || h <= 0)
        return -1;
    if (ctx->scene_fbo && ctx->scene_width == w && ctx->scene_height == h)
        return 0;

    destroy_scene_targets(ctx);

    gl.GenFramebuffers(1, &ctx->scene_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);

    gl.GenTextures(1, &ctx->scene_color_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->scene_color_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->scene_color_tex, 0);
    gl.GenTextures(1, &ctx->scene_motion_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->scene_motion_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, ctx->scene_motion_tex, 0);
    {
        const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        gl.DrawBuffers(2, attachments);
    }
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);

    gl.GenTextures(1, &ctx->scene_depth_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->scene_depth_tex);
    gl.TexImage2D(
        GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->scene_depth_tex, 0);

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroy_scene_targets(ctx);
        return -1;
    }

    ctx->scene_width = w;
    ctx->scene_height = h;
    return 0;
}

static void destroy_rtt_targets(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->rtt_depth_rbo)
        gl.DeleteRenderbuffers(1, &ctx->rtt_depth_rbo);
    if (ctx->rtt_color_tex)
        gl.DeleteTextures(1, &ctx->rtt_color_tex);
    if (ctx->rtt_fbo)
        gl.DeleteFramebuffers(1, &ctx->rtt_fbo);
    ctx->rtt_fbo = 0;
    ctx->rtt_color_tex = 0;
    ctx->rtt_depth_rbo = 0;
    ctx->rtt_width = 0;
    ctx->rtt_height = 0;
    ctx->rtt_active = 0;
    ctx->rtt_target = NULL;
}

static int ensure_rtt_targets(gl_context_t *ctx, vgfx3d_rendertarget_t *rt) {
    if (!ctx || !rt)
        return -1;
    if (ctx->rtt_fbo && ctx->rtt_width == rt->width && ctx->rtt_height == rt->height) {
        ctx->rtt_active = 1;
        ctx->rtt_target = rt;
        return 0;
    }

    destroy_rtt_targets(ctx);

    gl.GenFramebuffers(1, &ctx->rtt_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);

    gl.GenTextures(1, &ctx->rtt_color_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->rtt_color_tex);
    gl.TexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, rt->width, rt->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->rtt_color_tex, 0);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);

    gl.GenRenderbuffers(1, &ctx->rtt_depth_rbo);
    gl.BindRenderbuffer(GL_RENDERBUFFER, ctx->rtt_depth_rbo);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, rt->width, rt->height);
    gl.FramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->rtt_depth_rbo);

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroy_rtt_targets(ctx);
        return -1;
    }

    ctx->rtt_width = rt->width;
    ctx->rtt_height = rt->height;
    ctx->rtt_active = 1;
    ctx->rtt_target = rt;
    return 0;
}

static void destroy_shadow_targets(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->shadow_depth_tex)
        gl.DeleteTextures(1, &ctx->shadow_depth_tex);
    if (ctx->shadow_fbo)
        gl.DeleteFramebuffers(1, &ctx->shadow_fbo);
    ctx->shadow_fbo = 0;
    ctx->shadow_depth_tex = 0;
    ctx->shadow_width = 0;
    ctx->shadow_height = 0;
}

static int ensure_shadow_targets(gl_context_t *ctx, int32_t w, int32_t h) {
    if (!ctx || w <= 0 || h <= 0)
        return -1;
    if (ctx->shadow_fbo && ctx->shadow_width == w && ctx->shadow_height == h)
        return 0;

    destroy_shadow_targets(ctx);

    gl.GenFramebuffers(1, &ctx->shadow_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);

    gl.GenTextures(1, &ctx->shadow_depth_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->shadow_depth_tex);
    gl.TexImage2D(
        GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->shadow_depth_tex, 0);
    gl.DrawBuffer(GL_NONE);
    gl.ReadBuffer(GL_NONE);

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroy_shadow_targets(ctx);
        return -1;
    }

    ctx->shadow_width = w;
    ctx->shadow_height = h;
    return 0;
}

static void bind_main_framebuffer(gl_context_t *ctx) {
    if (ctx->rtt_active) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl.Viewport(0, 0, ctx->rtt_width, ctx->rtt_height);
    } else {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
        {
            const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            gl.DrawBuffers(2, attachments);
        }
        gl.Viewport(0, 0, ctx->scene_width, ctx->scene_height);
    }
}

static void gl_destroy_mesh_cache(gl_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < GL_MESH_CACHE_CAPACITY; i++) {
        if (ctx->mesh_cache[i].vbo)
            gl.DeleteBuffers(1, &ctx->mesh_cache[i].vbo);
        if (ctx->mesh_cache[i].ibo)
            gl.DeleteBuffers(1, &ctx->mesh_cache[i].ibo);
        memset(&ctx->mesh_cache[i], 0, sizeof(ctx->mesh_cache[i]));
    }
}

static int gl_lookup_cached_mesh_buffers(gl_context_t *ctx,
                                         const vgfx3d_draw_cmd_t *cmd,
                                         GLuint *out_vbo,
                                         GLuint *out_ibo) {
    gl_mesh_cache_entry_t *slot = NULL;
    gl_mesh_cache_entry_t *oldest = NULL;

    if (!ctx || !cmd || !out_vbo || !out_ibo || !cmd->geometry_key || cmd->geometry_revision == 0)
        return 0;

    for (int32_t i = 0; i < GL_MESH_CACHE_CAPACITY; i++) {
        gl_mesh_cache_entry_t *entry = &ctx->mesh_cache[i];
        if (entry->key == cmd->geometry_key) {
            slot = entry;
            break;
        }
        if (!entry->key && !slot)
            slot = entry;
        if (!oldest || entry->last_used_frame < oldest->last_used_frame)
            oldest = entry;
    }

    if (!slot)
        slot = oldest;
    if (!slot)
        return 0;

    if (slot->key != cmd->geometry_key || slot->revision != cmd->geometry_revision ||
        slot->vertex_count != cmd->vertex_count || slot->index_count != cmd->index_count ||
        !slot->vbo || !slot->ibo) {
        size_t vbytes;
        size_t ibytes;

        if (slot->vbo)
            gl.DeleteBuffers(1, &slot->vbo);
        if (slot->ibo)
            gl.DeleteBuffers(1, &slot->ibo);
        memset(slot, 0, sizeof(*slot));

        gl.GenBuffers(1, &slot->vbo);
        gl.GenBuffers(1, &slot->ibo);
        vbytes = (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t);
        ibytes = (size_t)cmd->index_count * sizeof(uint32_t);
        gl.BindBuffer(GL_ARRAY_BUFFER, slot->vbo);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbytes, cmd->vertices, GL_STATIC_DRAW);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot->ibo);
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ibytes, cmd->indices, GL_STATIC_DRAW);

        slot->key = cmd->geometry_key;
        slot->revision = cmd->geometry_revision;
        slot->vertex_count = cmd->vertex_count;
        slot->index_count = cmd->index_count;
    }

    slot->last_used_frame = ctx->frame_serial;
    *out_vbo = slot->vbo;
    *out_ibo = slot->ibo;
    return 1;
}

static void prepare_mesh_buffers(gl_context_t *ctx,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 GLuint *out_vbo,
                                 GLuint *out_ibo) {
    size_t vbytes = (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t);
    size_t ibytes = (size_t)cmd->index_count * sizeof(uint32_t);

    if (gl_lookup_cached_mesh_buffers(ctx, cmd, out_vbo, out_ibo))
        return;

    ensure_buffer_capacity(GL_ARRAY_BUFFER,
                           ctx->mesh_vbo,
                           &ctx->mesh_vbo_capacity,
                           vbytes,
                           4u * 1024u * 1024u,
                           GL_STREAM_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->mesh_vbo);
    gl.BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)vbytes, cmd->vertices);

    ensure_buffer_capacity(GL_ELEMENT_ARRAY_BUFFER,
                           ctx->mesh_ibo,
                           &ctx->mesh_ibo_capacity,
                           ibytes,
                           1u * 1024u * 1024u,
                           GL_STREAM_DRAW);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->mesh_ibo);
    gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)ibytes, cmd->indices);
    *out_vbo = ctx->mesh_vbo;
    *out_ibo = ctx->mesh_ibo;
}

static void set_identity_instance_constants(void) {
    gl.DisableVertexAttribArray(7);
    gl.DisableVertexAttribArray(8);
    gl.DisableVertexAttribArray(9);
    gl.DisableVertexAttribArray(10);
    gl.DisableVertexAttribArray(11);
    gl.DisableVertexAttribArray(12);
    gl.DisableVertexAttribArray(13);
    gl.DisableVertexAttribArray(14);
    gl.VertexAttribDivisor(7, 0);
    gl.VertexAttribDivisor(8, 0);
    gl.VertexAttribDivisor(9, 0);
    gl.VertexAttribDivisor(10, 0);
    gl.VertexAttribDivisor(11, 0);
    gl.VertexAttribDivisor(12, 0);
    gl.VertexAttribDivisor(13, 0);
    gl.VertexAttribDivisor(14, 0);
    gl.VertexAttrib4f(7, 1.0f, 0.0f, 0.0f, 0.0f);
    gl.VertexAttrib4f(8, 0.0f, 1.0f, 0.0f, 0.0f);
    gl.VertexAttrib4f(9, 0.0f, 0.0f, 1.0f, 0.0f);
    gl.VertexAttrib4f(10, 0.0f, 0.0f, 0.0f, 1.0f);
    gl.VertexAttrib4f(11, 1.0f, 0.0f, 0.0f, 0.0f);
    gl.VertexAttrib4f(12, 0.0f, 1.0f, 0.0f, 0.0f);
    gl.VertexAttrib4f(13, 0.0f, 0.0f, 1.0f, 0.0f);
    gl.VertexAttrib4f(14, 0.0f, 0.0f, 0.0f, 1.0f);
}

static void configure_mesh_attributes(gl_context_t *ctx, GLuint mesh_vbo, GLuint mesh_ibo) {
    GLsizei stride = (GLsizei)sizeof(vgfx3d_vertex_t);
    gl.BindVertexArray(ctx->vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_ibo);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)12);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)24);
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)32);
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void *)48);
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, stride, (void *)60);
    gl.EnableVertexAttribArray(5);
    gl.VertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void *)64);
    gl.EnableVertexAttribArray(6);
    set_identity_instance_constants();
}

static void configure_instance_attributes(gl_context_t *ctx,
                                          const float *instance_matrices,
                                          const float *prev_instance_matrices,
                                          int32_t instance_count) {
    size_t bytes = (size_t)instance_count * 16 * sizeof(float);
    ensure_buffer_capacity(GL_ARRAY_BUFFER,
                           ctx->instance_vbo,
                           &ctx->instance_vbo_capacity,
                           bytes,
                           64u * 1024u,
                           GL_STREAM_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->instance_vbo);
    gl.BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, instance_matrices);

    gl.EnableVertexAttribArray(7);
    gl.EnableVertexAttribArray(8);
    gl.EnableVertexAttribArray(9);
    gl.EnableVertexAttribArray(10);
    gl.VertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)0);
    gl.VertexAttribPointer(
        8, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)(4 * sizeof(float)));
    gl.VertexAttribPointer(
        9, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)(8 * sizeof(float)));
    gl.VertexAttribPointer(
        10, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)(12 * sizeof(float)));
    gl.VertexAttribDivisor(7, 1);
    gl.VertexAttribDivisor(8, 1);
    gl.VertexAttribDivisor(9, 1);
    gl.VertexAttribDivisor(10, 1);

    if (prev_instance_matrices) {
        ensure_buffer_capacity(GL_ARRAY_BUFFER,
                               ctx->prev_instance_vbo,
                               &ctx->prev_instance_vbo_capacity,
                               bytes,
                               64u * 1024u,
                               GL_STREAM_DRAW);
        gl.BindBuffer(GL_ARRAY_BUFFER, ctx->prev_instance_vbo);
        gl.BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, prev_instance_matrices);
        gl.EnableVertexAttribArray(11);
        gl.EnableVertexAttribArray(12);
        gl.EnableVertexAttribArray(13);
        gl.EnableVertexAttribArray(14);
        gl.VertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)0);
        gl.VertexAttribPointer(
            12, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)(4 * sizeof(float)));
        gl.VertexAttribPointer(
            13, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)(8 * sizeof(float)));
        gl.VertexAttribPointer(
            14, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (void *)(12 * sizeof(float)));
        gl.VertexAttribDivisor(11, 1);
        gl.VertexAttribDivisor(12, 1);
        gl.VertexAttribDivisor(13, 1);
        gl.VertexAttribDivisor(14, 1);
    } else {
        gl.DisableVertexAttribArray(11);
        gl.DisableVertexAttribArray(12);
        gl.DisableVertexAttribArray(13);
        gl.DisableVertexAttribArray(14);
        gl.VertexAttribDivisor(11, 0);
        gl.VertexAttribDivisor(12, 0);
        gl.VertexAttribDivisor(13, 0);
        gl.VertexAttribDivisor(14, 0);
        gl.VertexAttrib4f(11, 1.0f, 0.0f, 0.0f, 0.0f);
        gl.VertexAttrib4f(12, 0.0f, 1.0f, 0.0f, 0.0f);
        gl.VertexAttrib4f(13, 0.0f, 0.0f, 1.0f, 0.0f);
        gl.VertexAttrib4f(14, 0.0f, 0.0f, 0.0f, 1.0f);
    }
}

static void upload_bone_palette(gl_context_t *ctx,
                                GLuint ubo,
                                const float *bone_palette,
                                int32_t bone_count) {
    if (!ctx || !bone_palette || bone_count <= 0)
        return;

    float upload[128 * 16];
    if (bone_count > 128)
        bone_count = 128;
    for (int32_t i = 0; i < bone_count; i++)
        transpose4x4(&bone_palette[i * 16], &upload[i * 16]);

    gl.BindBuffer(GL_UNIFORM_BUFFER, ubo);
    gl.BufferSubData(
        GL_UNIFORM_BUFFER, 0, (GLsizeiptr)((size_t)bone_count * 16 * sizeof(float)), upload);
}

static void upload_active_bone_palettes(gl_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    if (!ctx || !cmd || !cmd->bone_palette || cmd->bone_count <= 0)
        return;
    upload_bone_palette(ctx, ctx->bone_ubo, cmd->bone_palette, cmd->bone_count);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->bone_ubo);
    if (cmd->prev_bone_palette) {
        upload_bone_palette(ctx, ctx->prev_bone_ubo, cmd->prev_bone_palette, cmd->bone_count);
        gl.BindBufferBase(GL_UNIFORM_BUFFER, 1, ctx->prev_bone_ubo);
    } else {
        upload_bone_palette(ctx, ctx->prev_bone_ubo, cmd->bone_palette, cmd->bone_count);
        gl.BindBufferBase(GL_UNIFORM_BUFFER, 1, ctx->prev_bone_ubo);
    }
}

static void bind_morph_payload(gl_context_t *ctx,
                               const vgfx3d_draw_cmd_t *cmd,
                               GLint uHasSkinning,
                               GLint uMorphShapeCount,
                               GLint uVertexCount,
                               GLint uMorphWeights,
                               GLint uPrevMorphWeights,
                               GLint uMorphDeltas,
                               GLint uHasMorphNormalDeltas,
                               GLint uMorphNormalDeltas) {
    int use_skinning = (cmd->bone_palette && cmd->bone_count > 0 && cmd->bone_count <= 128) ? 1 : 0;
    int morph_count = (cmd->morph_deltas && cmd->morph_weights && cmd->morph_shape_count > 0)
                          ? cmd->morph_shape_count
                          : 0;
    if (morph_count > 32)
        morph_count = 32;

    gl.Uniform1i(uHasSkinning, use_skinning);
    gl.Uniform1i(uMorphShapeCount, morph_count);
    gl.Uniform1i(uVertexCount, morph_count > 0 ? (GLint)cmd->vertex_count : 0);
    if (morph_count > 0 && uMorphWeights >= 0)
        gl.Uniform1fv(uMorphWeights, morph_count, cmd->morph_weights);
    if (morph_count > 0 && uPrevMorphWeights >= 0)
        gl.Uniform1fv(uPrevMorphWeights,
                      morph_count,
                      cmd->prev_morph_weights ? cmd->prev_morph_weights : cmd->morph_weights);

    if (use_skinning)
        upload_active_bone_palettes(ctx, cmd);

    gl.ActiveTexture(GL_TEXTURE0 + 10);
    if (morph_count > 0) {
        size_t bytes = (size_t)morph_count * (size_t)cmd->vertex_count * 3 * sizeof(float);
        ensure_buffer_capacity(GL_TEXTURE_BUFFER,
                               ctx->morph_buffer,
                               &ctx->morph_capacity_bytes,
                               bytes,
                               64u * 1024u,
                               GL_STREAM_DRAW);
        gl.BindBuffer(GL_TEXTURE_BUFFER, ctx->morph_buffer);
        gl.BufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)bytes, cmd->morph_deltas);
        gl.BindTexture(GL_TEXTURE_BUFFER, ctx->morph_tbo);
        gl.TexBuffer(GL_TEXTURE_BUFFER, GL_R32F, ctx->morph_buffer);
    } else {
        gl.BindTexture(GL_TEXTURE_BUFFER, 0);
    }
    gl.Uniform1i(uMorphDeltas, 10);
    gl.ActiveTexture(GL_TEXTURE0 + 11);
    if (uHasMorphNormalDeltas >= 0)
        gl.Uniform1i(uHasMorphNormalDeltas, (morph_count > 0 && cmd->morph_normal_deltas) ? 1 : 0);
    if (morph_count > 0 && cmd->morph_normal_deltas && uMorphNormalDeltas >= 0) {
        size_t bytes = (size_t)morph_count * (size_t)cmd->vertex_count * 3 * sizeof(float);
        ensure_buffer_capacity(GL_TEXTURE_BUFFER,
                               ctx->morph_normal_buffer,
                               &ctx->morph_normal_capacity_bytes,
                               bytes,
                               64u * 1024u,
                               GL_STREAM_DRAW);
        gl.BindBuffer(GL_TEXTURE_BUFFER, ctx->morph_normal_buffer);
        gl.BufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)bytes, cmd->morph_normal_deltas);
        gl.BindTexture(GL_TEXTURE_BUFFER, ctx->morph_normal_tbo);
        gl.TexBuffer(GL_TEXTURE_BUFFER, GL_R32F, ctx->morph_normal_buffer);
    } else {
        gl.BindTexture(GL_TEXTURE_BUFFER, 0);
    }
    if (uMorphNormalDeltas >= 0)
        gl.Uniform1i(uMorphNormalDeltas, 11);
    gl.ActiveTexture(GL_TEXTURE0);
}

static void bind_texture_unit(GLint uniform_loc, int unit, GLenum target, GLuint texture) {
    gl.ActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    gl.BindTexture(target, texture);
    if (uniform_loc >= 0)
        gl.Uniform1i(uniform_loc, unit);
}

static void upload_light_uniforms(gl_context_t *ctx,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count) {
    if (light_count > 8)
        light_count = 8;
    gl.Uniform1i(ctx->uLightCount, light_count);
    for (int32_t i = 0; i < light_count; i++) {
        gl.Uniform1i(ctx->uLightType[i], lights[i].type);
        gl.Uniform3f(ctx->uLightDir[i],
                     lights[i].direction[0],
                     lights[i].direction[1],
                     lights[i].direction[2]);
        gl.Uniform3f(
            ctx->uLightPos[i], lights[i].position[0], lights[i].position[1], lights[i].position[2]);
        gl.Uniform3f(
            ctx->uLightColor[i], lights[i].color[0], lights[i].color[1], lights[i].color[2]);
        gl.Uniform1f(ctx->uLightIntensity[i], lights[i].intensity);
        gl.Uniform1f(ctx->uLightAtten[i], lights[i].attenuation);
        gl.Uniform1f(ctx->uLightInnerCos[i], lights[i].inner_cos);
        gl.Uniform1f(ctx->uLightOuterCos[i], lights[i].outer_cos);
    }
}

static void upload_main_uniforms(gl_context_t *ctx,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 const vgfx3d_light_params_t *lights,
                                 int32_t light_count,
                                 const float *ambient,
                                 int8_t instanced) {
    float normal_matrix[16];
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_matrix);

    gl.UniformMatrix4fv(ctx->uModelMatrix, 1, GL_TRUE, cmd->model_matrix);
    gl.UniformMatrix4fv(ctx->uPrevModelMatrix,
                        1,
                        GL_TRUE,
                        cmd->has_prev_model_matrix ? cmd->prev_model_matrix : cmd->model_matrix);
    gl.UniformMatrix4fv(ctx->uViewProjection, 1, GL_TRUE, ctx->vp);
    gl.UniformMatrix4fv(
        ctx->uPrevViewProjection, 1, GL_TRUE, ctx->prev_vp_valid ? ctx->prev_vp : ctx->vp);
    gl.UniformMatrix4fv(ctx->uNormalMatrix, 1, GL_TRUE, normal_matrix);
    gl.UniformMatrix4fv(ctx->uShadowVP, 1, GL_TRUE, ctx->shadow_vp);

    gl.Uniform3f(ctx->uCameraPos, ctx->cam_pos[0], ctx->cam_pos[1], ctx->cam_pos[2]);
    gl.Uniform3f(ctx->uAmbientColor, ambient[0], ambient[1], ambient[2]);
    gl.Uniform4f(ctx->uDiffuseColor,
                 cmd->diffuse_color[0],
                 cmd->diffuse_color[1],
                 cmd->diffuse_color[2],
                 cmd->diffuse_color[3]);
    gl.Uniform4f(
        ctx->uSpecularColor, cmd->specular[0], cmd->specular[1], cmd->specular[2], cmd->shininess);
    gl.Uniform3f(ctx->uEmissiveColor,
                 cmd->emissive_color[0],
                 cmd->emissive_color[1],
                 cmd->emissive_color[2]);
    gl.Uniform4f(ctx->uPbrScalars0,
                 cmd->metallic,
                 cmd->roughness,
                 cmd->ao,
                 cmd->emissive_intensity);
    gl.Uniform4f(ctx->uPbrScalars1, cmd->normal_scale, cmd->alpha_cutoff, 0.0f, 0.0f);
    gl.Uniform1i(ctx->uHasEnvMap, (cmd->env_map && cmd->reflectivity > 0.0001f) ? 1 : 0);
    gl.Uniform1f(ctx->uReflectivity, cmd->reflectivity);
    gl.Uniform1f(ctx->uAlpha, cmd->alpha);
    gl.Uniform1i(ctx->uUnlit, cmd->unlit);
    gl.Uniform1i(ctx->uShadingModel, cmd->shading_model);
    gl.Uniform1i(ctx->uWorkflow, cmd->workflow);
    gl.Uniform1i(ctx->uAlphaMode, cmd->alpha_mode);
    gl.Uniform1fv(ctx->uCustomParams, 8, cmd->custom_params);
    gl.Uniform1i(ctx->uUseInstancing, instanced ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevModelMatrix, cmd->has_prev_model_matrix ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevInstanceMatrices, cmd->has_prev_instance_matrices ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevSkinning, cmd->prev_bone_palette ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevMorphWeights, cmd->prev_morph_weights ? 1 : 0);
    gl.Uniform1i(ctx->uFogEnabled, ctx->fog_enabled ? 1 : 0);
    gl.Uniform1f(ctx->uFogNear, ctx->fog_near);
    gl.Uniform1f(ctx->uFogFar, ctx->fog_far);
    gl.Uniform3f(ctx->uFogColor, ctx->fog_color[0], ctx->fog_color[1], ctx->fog_color[2]);
    gl.Uniform1i(ctx->uShadowEnabled, ctx->shadow_active ? 1 : 0);
    gl.Uniform1f(ctx->uShadowBias, ctx->shadow_bias);

    upload_light_uniforms(ctx, lights, light_count);

    bind_morph_payload(ctx,
                       cmd,
                       ctx->uHasSkinning,
                       ctx->uMorphShapeCount,
                       ctx->uVertexCount,
                       ctx->uMorphWeights,
                       ctx->uPrevMorphWeights,
                       ctx->uMorphDeltas,
                       ctx->uHasMorphNormalDeltas,
                       ctx->uMorphNormalDeltas);
}

static void bind_material_textures(gl_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    GLuint diffuse_tex = cmd->texture ? gl_get_cached_texture(ctx, cmd->texture) : 0;
    GLuint normal_tex = cmd->normal_map ? gl_get_cached_texture(ctx, cmd->normal_map) : 0;
    GLuint specular_tex = cmd->specular_map ? gl_get_cached_texture(ctx, cmd->specular_map) : 0;
    GLuint emissive_tex = cmd->emissive_map ? gl_get_cached_texture(ctx, cmd->emissive_map) : 0;
    GLuint metallic_roughness_tex =
        cmd->metallic_roughness_map ? gl_get_cached_texture(ctx, cmd->metallic_roughness_map) : 0;
    GLuint ao_tex = cmd->ao_map ? gl_get_cached_texture(ctx, cmd->ao_map) : 0;
    GLuint env_tex =
        cmd->env_map ? gl_get_cached_cubemap(ctx, (const rt_cubemap3d *)cmd->env_map) : 0;
    GLuint splat_tex = cmd->splat_map ? gl_get_cached_texture(ctx, cmd->splat_map) : 0;
    GLuint splat_layer0 =
        cmd->splat_layers[0] ? gl_get_cached_texture(ctx, cmd->splat_layers[0]) : 0;
    GLuint splat_layer1 =
        cmd->splat_layers[1] ? gl_get_cached_texture(ctx, cmd->splat_layers[1]) : 0;
    GLuint splat_layer2 =
        cmd->splat_layers[2] ? gl_get_cached_texture(ctx, cmd->splat_layers[2]) : 0;
    GLuint splat_layer3 =
        cmd->splat_layers[3] ? gl_get_cached_texture(ctx, cmd->splat_layers[3]) : 0;
    int has_splat = cmd->has_splat && splat_tex != 0;

    gl.Uniform1i(ctx->uHasTexture, diffuse_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasNormalMap, normal_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasSpecularMap, specular_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasEmissiveMap, emissive_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasMetallicRoughnessMap, metallic_roughness_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasAOMap, ao_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasSplat, has_splat ? 1 : 0);

    bind_texture_unit(ctx->uDiffuseTex, 0, GL_TEXTURE_2D, diffuse_tex);
    bind_texture_unit(ctx->uNormalTex, 1, GL_TEXTURE_2D, normal_tex);
    bind_texture_unit(ctx->uSpecularTex, 2, GL_TEXTURE_2D, specular_tex);
    bind_texture_unit(ctx->uEmissiveTex, 3, GL_TEXTURE_2D, emissive_tex);
    bind_texture_unit(
        ctx->uShadowTex, 4, GL_TEXTURE_2D, ctx->shadow_active ? ctx->shadow_depth_tex : 0);
    bind_texture_unit(ctx->uSplatTex, 5, GL_TEXTURE_2D, splat_tex);
    bind_texture_unit(ctx->uSplatLayer0, 6, GL_TEXTURE_2D, splat_layer0);
    bind_texture_unit(ctx->uSplatLayer1, 7, GL_TEXTURE_2D, splat_layer1);
    bind_texture_unit(ctx->uSplatLayer2, 8, GL_TEXTURE_2D, splat_layer2);
    bind_texture_unit(ctx->uSplatLayer3, 9, GL_TEXTURE_2D, splat_layer3);
    bind_texture_unit(ctx->uEnvMap, 12, GL_TEXTURE_CUBE_MAP, env_tex);
    bind_texture_unit(ctx->uMetallicRoughnessTex, 14, GL_TEXTURE_2D, metallic_roughness_tex);
    bind_texture_unit(ctx->uAOTex, 15, GL_TEXTURE_2D, ao_tex);
    if (ctx->uSplatScales >= 0) {
        gl.Uniform4f(ctx->uSplatScales,
                     cmd->splat_layer_scales[0],
                     cmd->splat_layer_scales[1],
                     cmd->splat_layer_scales[2],
                     cmd->splat_layer_scales[3]);
    }
    gl.ActiveTexture(GL_TEXTURE0);
}

static void bind_shadow_anim(gl_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    gl.Uniform1i(ctx->shadow_uHasSkinning,
                 (cmd->bone_palette && cmd->bone_count > 0 && cmd->bone_count <= 128) ? 1 : 0);
    int morph_count = (cmd->morph_deltas && cmd->morph_weights && cmd->morph_shape_count > 0)
                          ? cmd->morph_shape_count
                          : 0;
    if (morph_count > 32)
        morph_count = 32;
    gl.Uniform1i(ctx->shadow_uMorphShapeCount, morph_count);
    gl.Uniform1i(ctx->shadow_uVertexCount, morph_count > 0 ? (GLint)cmd->vertex_count : 0);
    if (morph_count > 0)
        gl.Uniform1fv(ctx->shadow_uMorphWeights, morph_count, cmd->morph_weights);
    bind_morph_payload(ctx,
                       cmd,
                       ctx->shadow_uHasSkinning,
                       ctx->shadow_uMorphShapeCount,
                       ctx->shadow_uVertexCount,
                       ctx->shadow_uMorphWeights,
                       -1,
                       ctx->shadow_uMorphDeltas,
                       -1,
                       -1);
}

static void draw_scene_texture(gl_context_t *ctx, const vgfx3d_postfx_snapshot_t *snapshot) {
    if (!ctx || !ctx->postfx_program || !ctx->scene_color_tex)
        return;

    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, ctx->width, ctx->height);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.DepthMask(GL_FALSE);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.UseProgram(ctx->postfx_program);
    gl.BindVertexArray(ctx->fullscreen_vao);
    bind_texture_unit(ctx->postfx_uSceneTex, 0, GL_TEXTURE_2D, ctx->scene_color_tex);
    bind_texture_unit(ctx->postfx_uSceneDepthTex, 1, GL_TEXTURE_2D, ctx->scene_depth_tex);
    bind_texture_unit(ctx->postfx_uSceneMotionTex, 2, GL_TEXTURE_2D, ctx->scene_motion_tex);
    gl.Uniform2f(ctx->postfx_uInvResolution,
                 1.0f / (float)ctx->scene_width,
                 1.0f / (float)ctx->scene_height);
    gl.Uniform3f(ctx->postfx_uCameraPos, ctx->cam_pos[0], ctx->cam_pos[1], ctx->cam_pos[2]);
    gl.UniformMatrix4fv(ctx->postfx_uInvViewProjection, 1, GL_TRUE, ctx->inv_vp);
    gl.UniformMatrix4fv(
        ctx->postfx_uPrevViewProjection, 1, GL_TRUE, ctx->prev_vp_valid ? ctx->prev_vp : ctx->vp);

    if (snapshot) {
        gl.Uniform1i(ctx->postfx_uBloomEnabled, snapshot->bloom_enabled ? 1 : 0);
        gl.Uniform1f(ctx->postfx_uBloomThreshold, snapshot->bloom_threshold);
        gl.Uniform1f(ctx->postfx_uBloomIntensity, snapshot->bloom_intensity);
        gl.Uniform1i(ctx->postfx_uTonemapMode, snapshot->tonemap_mode);
        gl.Uniform1f(ctx->postfx_uTonemapExposure, snapshot->tonemap_exposure);
        gl.Uniform1i(ctx->postfx_uFxaaEnabled, snapshot->fxaa_enabled ? 1 : 0);
        gl.Uniform1i(ctx->postfx_uColorGradeEnabled, snapshot->color_grade_enabled ? 1 : 0);
        gl.Uniform1f(ctx->postfx_uCgBrightness, snapshot->cg_brightness);
        gl.Uniform1f(ctx->postfx_uCgContrast, snapshot->cg_contrast);
        gl.Uniform1f(ctx->postfx_uCgSaturation, snapshot->cg_saturation);
        gl.Uniform1i(ctx->postfx_uVignetteEnabled, snapshot->vignette_enabled ? 1 : 0);
        gl.Uniform1f(ctx->postfx_uVignetteRadius, snapshot->vignette_radius);
        gl.Uniform1f(ctx->postfx_uVignetteSoftness, snapshot->vignette_softness);
        gl.Uniform1i(ctx->postfx_uSsaoEnabled, snapshot->ssao_enabled ? 1 : 0);
        gl.Uniform1f(ctx->postfx_uSsaoRadius, snapshot->ssao_radius);
        gl.Uniform1f(ctx->postfx_uSsaoIntensity, snapshot->ssao_intensity);
        gl.Uniform1i(ctx->postfx_uSsaoSamples, snapshot->ssao_samples);
        gl.Uniform1i(ctx->postfx_uDofEnabled, snapshot->dof_enabled ? 1 : 0);
        gl.Uniform1f(ctx->postfx_uDofFocusDistance, snapshot->dof_focus_distance);
        gl.Uniform1f(ctx->postfx_uDofAperture, snapshot->dof_aperture);
        gl.Uniform1f(ctx->postfx_uDofMaxBlur, snapshot->dof_max_blur);
        gl.Uniform1i(ctx->postfx_uMotionBlurEnabled, snapshot->motion_blur_enabled ? 1 : 0);
        gl.Uniform1f(ctx->postfx_uMotionBlurIntensity, snapshot->motion_blur_intensity);
        gl.Uniform1i(ctx->postfx_uMotionBlurSamples, snapshot->motion_blur_samples);
    } else {
        gl.Uniform1i(ctx->postfx_uBloomEnabled, 0);
        gl.Uniform1f(ctx->postfx_uBloomThreshold, 1.0f);
        gl.Uniform1f(ctx->postfx_uBloomIntensity, 0.0f);
        gl.Uniform1i(ctx->postfx_uTonemapMode, 0);
        gl.Uniform1f(ctx->postfx_uTonemapExposure, 1.0f);
        gl.Uniform1i(ctx->postfx_uFxaaEnabled, 0);
        gl.Uniform1i(ctx->postfx_uColorGradeEnabled, 0);
        gl.Uniform1f(ctx->postfx_uCgBrightness, 0.0f);
        gl.Uniform1f(ctx->postfx_uCgContrast, 1.0f);
        gl.Uniform1f(ctx->postfx_uCgSaturation, 1.0f);
        gl.Uniform1i(ctx->postfx_uVignetteEnabled, 0);
        gl.Uniform1f(ctx->postfx_uVignetteRadius, 1.0f);
        gl.Uniform1f(ctx->postfx_uVignetteSoftness, 0.0f);
        gl.Uniform1i(ctx->postfx_uSsaoEnabled, 0);
        gl.Uniform1f(ctx->postfx_uSsaoRadius, 0.0f);
        gl.Uniform1f(ctx->postfx_uSsaoIntensity, 0.0f);
        gl.Uniform1i(ctx->postfx_uSsaoSamples, 0);
        gl.Uniform1i(ctx->postfx_uDofEnabled, 0);
        gl.Uniform1f(ctx->postfx_uDofFocusDistance, 0.0f);
        gl.Uniform1f(ctx->postfx_uDofAperture, 0.0f);
        gl.Uniform1f(ctx->postfx_uDofMaxBlur, 0.0f);
        gl.Uniform1i(ctx->postfx_uMotionBlurEnabled, 0);
        gl.Uniform1f(ctx->postfx_uMotionBlurIntensity, 0.0f);
        gl.Uniform1i(ctx->postfx_uMotionBlurSamples, 0);
    }

    gl.DrawArrays(GL_TRIANGLES, 0, 3);
}

static void gl_draw_skybox_impl(gl_context_t *ctx, const rt_cubemap3d *cubemap) {
    if (!ctx || !cubemap || !ctx->skybox_program)
        return;

    GLuint tex = gl_get_cached_cubemap(ctx, cubemap);
    if (!tex)
        return;

    float view_rotation[16];
    memcpy(view_rotation, ctx->view, sizeof(view_rotation));
    view_rotation[3] = 0.0f;
    view_rotation[7] = 0.0f;
    view_rotation[11] = 0.0f;

    gl.DepthMask(GL_FALSE);
    gl.DepthFunc(GL_LEQUAL);
    gl.Disable(GL_CULL_FACE);
    gl.UseProgram(ctx->skybox_program);
    gl.BindVertexArray(ctx->skybox_vao);
    gl.UniformMatrix4fv(ctx->skybox_uProjection, 1, GL_TRUE, ctx->projection);
    gl.UniformMatrix4fv(ctx->skybox_uViewRotation, 1, GL_TRUE, view_rotation);
    bind_texture_unit(ctx->skybox_uSkybox, 13, GL_TEXTURE_CUBE_MAP, tex);
    gl.DrawArrays(GL_TRIANGLES, 0, 36);

    gl.ActiveTexture(GL_TEXTURE0);
    gl.DepthFunc(GL_LESS);
    gl.DepthMask(GL_TRUE);
    gl.Enable(GL_CULL_FACE);
    gl.UseProgram(ctx->program);
    gl.BindVertexArray(ctx->vao);
}

static void query_main_uniforms(gl_context_t *ctx) {
#define U(name) ctx->name = gl.GetUniformLocation(ctx->program, #name)
    U(uModelMatrix);
    U(uPrevModelMatrix);
    U(uViewProjection);
    U(uPrevViewProjection);
    U(uNormalMatrix);
    U(uShadowVP);
    U(uCameraPos);
    U(uAmbientColor);
    U(uDiffuseColor);
    U(uSpecularColor);
    U(uEmissiveColor);
    U(uAlpha);
    U(uPbrScalars0);
    U(uPbrScalars1);
    U(uUnlit);
    U(uShadingModel);
    U(uLightCount);
    U(uHasTexture);
    U(uHasNormalMap);
    U(uHasSpecularMap);
    U(uHasEmissiveMap);
    U(uHasEnvMap);
    U(uReflectivity);
    U(uWorkflow);
    U(uAlphaMode);
    U(uHasMetallicRoughnessMap);
    U(uHasAOMap);
    U(uCustomParams);
    U(uHasSplat);
    U(uFogEnabled);
    U(uFogNear);
    U(uFogFar);
    U(uFogColor);
    U(uShadowEnabled);
    U(uShadowBias);
    U(uUseInstancing);
    U(uHasPrevModelMatrix);
    U(uHasPrevInstanceMatrices);
    U(uHasSkinning);
    U(uHasPrevSkinning);
    U(uMorphShapeCount);
    U(uVertexCount);
    U(uHasPrevMorphWeights);
    ctx->uMorphWeights = gl.GetUniformLocation(ctx->program, "uMorphWeights[0]");
    ctx->uPrevMorphWeights = gl.GetUniformLocation(ctx->program, "uPrevMorphWeights[0]");
    U(uMorphDeltas);
    U(uMorphNormalDeltas);
    U(uHasMorphNormalDeltas);
    U(uDiffuseTex);
    U(uNormalTex);
    U(uSpecularTex);
    U(uEmissiveTex);
    U(uShadowTex);
    U(uEnvMap);
    U(uMetallicRoughnessTex);
    U(uAOTex);
    U(uSplatTex);
    U(uSplatLayer0);
    U(uSplatLayer1);
    U(uSplatLayer2);
    U(uSplatLayer3);
    U(uSplatScales);
#undef U
    for (int i = 0; i < 8; i++) {
        char name[64];
        snprintf(name, sizeof(name), "uLightType[%d]", i);
        ctx->uLightType[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightDir[%d]", i);
        ctx->uLightDir[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightPos[%d]", i);
        ctx->uLightPos[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightColor[%d]", i);
        ctx->uLightColor[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightIntensity[%d]", i);
        ctx->uLightIntensity[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightAtten[%d]", i);
        ctx->uLightAtten[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightInnerCos[%d]", i);
        ctx->uLightInnerCos[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightOuterCos[%d]", i);
        ctx->uLightOuterCos[i] = gl.GetUniformLocation(ctx->program, name);
    }
}

static void query_shadow_uniforms(gl_context_t *ctx) {
    ctx->shadow_uModelMatrix = gl.GetUniformLocation(ctx->shadow_program, "uModelMatrix");
    ctx->shadow_uViewProjection = gl.GetUniformLocation(ctx->shadow_program, "uViewProjection");
    ctx->shadow_uHasSkinning = gl.GetUniformLocation(ctx->shadow_program, "uHasSkinning");
    ctx->shadow_uMorphShapeCount = gl.GetUniformLocation(ctx->shadow_program, "uMorphShapeCount");
    ctx->shadow_uVertexCount = gl.GetUniformLocation(ctx->shadow_program, "uVertexCount");
    ctx->shadow_uMorphWeights = gl.GetUniformLocation(ctx->shadow_program, "uMorphWeights[0]");
    ctx->shadow_uMorphDeltas = gl.GetUniformLocation(ctx->shadow_program, "uMorphDeltas");
}

static void query_skybox_uniforms(gl_context_t *ctx) {
    ctx->skybox_uProjection = gl.GetUniformLocation(ctx->skybox_program, "uProjection");
    ctx->skybox_uViewRotation = gl.GetUniformLocation(ctx->skybox_program, "uViewRotation");
    ctx->skybox_uSkybox = gl.GetUniformLocation(ctx->skybox_program, "uSkybox");
}

static void query_postfx_uniforms(gl_context_t *ctx) {
    ctx->postfx_uSceneTex = gl.GetUniformLocation(ctx->postfx_program, "uSceneTex");
    ctx->postfx_uSceneDepthTex = gl.GetUniformLocation(ctx->postfx_program, "uSceneDepthTex");
    ctx->postfx_uSceneMotionTex = gl.GetUniformLocation(ctx->postfx_program, "uSceneMotionTex");
    ctx->postfx_uInvResolution = gl.GetUniformLocation(ctx->postfx_program, "uInvResolution");
    ctx->postfx_uBloomEnabled = gl.GetUniformLocation(ctx->postfx_program, "uBloomEnabled");
    ctx->postfx_uBloomThreshold = gl.GetUniformLocation(ctx->postfx_program, "uBloomThreshold");
    ctx->postfx_uBloomIntensity = gl.GetUniformLocation(ctx->postfx_program, "uBloomIntensity");
    ctx->postfx_uTonemapMode = gl.GetUniformLocation(ctx->postfx_program, "uTonemapMode");
    ctx->postfx_uTonemapExposure = gl.GetUniformLocation(ctx->postfx_program, "uTonemapExposure");
    ctx->postfx_uFxaaEnabled = gl.GetUniformLocation(ctx->postfx_program, "uFxaaEnabled");
    ctx->postfx_uColorGradeEnabled =
        gl.GetUniformLocation(ctx->postfx_program, "uColorGradeEnabled");
    ctx->postfx_uCgBrightness = gl.GetUniformLocation(ctx->postfx_program, "uCgBrightness");
    ctx->postfx_uCgContrast = gl.GetUniformLocation(ctx->postfx_program, "uCgContrast");
    ctx->postfx_uCgSaturation = gl.GetUniformLocation(ctx->postfx_program, "uCgSaturation");
    ctx->postfx_uVignetteEnabled = gl.GetUniformLocation(ctx->postfx_program, "uVignetteEnabled");
    ctx->postfx_uVignetteRadius = gl.GetUniformLocation(ctx->postfx_program, "uVignetteRadius");
    ctx->postfx_uVignetteSoftness = gl.GetUniformLocation(ctx->postfx_program, "uVignetteSoftness");
    ctx->postfx_uSsaoEnabled = gl.GetUniformLocation(ctx->postfx_program, "uSsaoEnabled");
    ctx->postfx_uSsaoRadius = gl.GetUniformLocation(ctx->postfx_program, "uSsaoRadius");
    ctx->postfx_uSsaoIntensity = gl.GetUniformLocation(ctx->postfx_program, "uSsaoIntensity");
    ctx->postfx_uSsaoSamples = gl.GetUniformLocation(ctx->postfx_program, "uSsaoSamples");
    ctx->postfx_uDofEnabled = gl.GetUniformLocation(ctx->postfx_program, "uDofEnabled");
    ctx->postfx_uDofFocusDistance = gl.GetUniformLocation(ctx->postfx_program, "uDofFocusDistance");
    ctx->postfx_uDofAperture = gl.GetUniformLocation(ctx->postfx_program, "uDofAperture");
    ctx->postfx_uDofMaxBlur = gl.GetUniformLocation(ctx->postfx_program, "uDofMaxBlur");
    ctx->postfx_uMotionBlurEnabled =
        gl.GetUniformLocation(ctx->postfx_program, "uMotionBlurEnabled");
    ctx->postfx_uMotionBlurIntensity =
        gl.GetUniformLocation(ctx->postfx_program, "uMotionBlurIntensity");
    ctx->postfx_uMotionBlurSamples =
        gl.GetUniformLocation(ctx->postfx_program, "uMotionBlurSamples");
    ctx->postfx_uCameraPos = gl.GetUniformLocation(ctx->postfx_program, "uCameraPos");
    ctx->postfx_uInvViewProjection =
        gl.GetUniformLocation(ctx->postfx_program, "uInvViewProjection");
    ctx->postfx_uPrevViewProjection =
        gl.GetUniformLocation(ctx->postfx_program, "uPrevViewProjection");
}

static void *gl_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    if (load_gl() != 0)
        return NULL;

    void *native = vgfx_get_native_view(win);
    if (!native)
        return NULL;
    Window xwin = (Window)(uintptr_t)native;
    Display *dpy = (Display *)vgfx_get_native_display(win);
    if (!dpy)
        return NULL;

    int fb_attribs[] = {GLX_RENDER_TYPE,
                        GLX_RGBA_BIT,
                        GLX_DRAWABLE_TYPE,
                        GLX_WINDOW_BIT,
                        GLX_DOUBLEBUFFER,
                        1,
                        GLX_RED_SIZE,
                        8,
                        GLX_GREEN_SIZE,
                        8,
                        GLX_BLUE_SIZE,
                        8,
                        GLX_ALPHA_SIZE,
                        8,
                        GLX_DEPTH_SIZE,
                        24,
                        0};
    int fb_count = 0;
    GLXFBConfig *configs = glx.ChooseFBConfig(dpy, DefaultScreen(dpy), fb_attribs, &fb_count);
    if (!configs || fb_count == 0)
        return NULL;

    GLXContext glxCtx = NULL;
    if (glx.CreateContextAttribsARB) {
        const int ctx_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                                   3,
                                   GLX_CONTEXT_MINOR_VERSION_ARB,
                                   3,
                                   GLX_CONTEXT_PROFILE_MASK_ARB,
                                   GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                                   0};
        glxCtx = glx.CreateContextAttribsARB(dpy, configs[0], NULL, 1, ctx_attribs);
    }
    if (!glxCtx)
        glxCtx = glx.CreateNewContext(dpy, configs[0], GLX_RGBA_TYPE, NULL, 1);
    XFree(configs);
    if (!glxCtx)
        return NULL;
    glx.MakeCurrent(dpy, xwin, glxCtx);

    gl_context_t *ctx = (gl_context_t *)calloc(1, sizeof(gl_context_t));
    if (!ctx) {
        glx.DestroyContext(dpy, glxCtx);
        return NULL;
    }
    ctx->display = dpy;
    ctx->window = xwin;
    ctx->glxCtx = glxCtx;
    ctx->width = w;
    ctx->height = h;

    GLuint vs = compile_shader_parts(
        GL_VERTEX_SHADER, glsl_vertex_src, (GLsizei)(sizeof(glsl_vertex_src) / sizeof(glsl_vertex_src[0])));
    GLuint fs = compile_shader_parts(
        GL_FRAGMENT_SHADER, glsl_fragment_src, (GLsizei)(sizeof(glsl_fragment_src) / sizeof(glsl_fragment_src[0])));
    GLuint svs = compile_shader(GL_VERTEX_SHADER, glsl_shadow_vertex_src);
    GLuint sfs = compile_shader(GL_FRAGMENT_SHADER, glsl_shadow_fragment_src);
    GLuint pvs = compile_shader(GL_VERTEX_SHADER, glsl_postfx_vertex_src);
    GLuint pfs = compile_shader_parts(GL_FRAGMENT_SHADER,
                                      glsl_postfx_fragment_src,
                                      (GLsizei)(sizeof(glsl_postfx_fragment_src) /
                                                sizeof(glsl_postfx_fragment_src[0])));
    GLuint skyvs = compile_shader(GL_VERTEX_SHADER, glsl_skybox_vertex_src);
    GLuint skyfs = compile_shader(GL_FRAGMENT_SHADER, glsl_skybox_fragment_src);
    if (!vs || !fs || !svs || !sfs || !pvs || !pfs || !skyvs || !skyfs) {
        if (vs)
            gl.DeleteShader(vs);
        if (fs)
            gl.DeleteShader(fs);
        if (svs)
            gl.DeleteShader(svs);
        if (sfs)
            gl.DeleteShader(sfs);
        if (pvs)
            gl.DeleteShader(pvs);
        if (pfs)
            gl.DeleteShader(pfs);
        if (skyvs)
            gl.DeleteShader(skyvs);
        if (skyfs)
            gl.DeleteShader(skyfs);
        glx.DestroyContext(dpy, glxCtx);
        free(ctx);
        return NULL;
    }

    ctx->program = link_program(vs, fs);
    ctx->shadow_program = link_program(svs, sfs);
    ctx->postfx_program = link_program(pvs, pfs);
    ctx->skybox_program = link_program(skyvs, skyfs);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    gl.DeleteShader(svs);
    gl.DeleteShader(sfs);
    gl.DeleteShader(pvs);
    gl.DeleteShader(pfs);
    gl.DeleteShader(skyvs);
    gl.DeleteShader(skyfs);
    if (!ctx->program || !ctx->shadow_program || !ctx->postfx_program || !ctx->skybox_program) {
        if (ctx->program)
            gl.DeleteProgram(ctx->program);
        if (ctx->shadow_program)
            gl.DeleteProgram(ctx->shadow_program);
        if (ctx->postfx_program)
            gl.DeleteProgram(ctx->postfx_program);
        if (ctx->skybox_program)
            gl.DeleteProgram(ctx->skybox_program);
        glx.DestroyContext(dpy, glxCtx);
        free(ctx);
        return NULL;
    }

    query_main_uniforms(ctx);
    query_shadow_uniforms(ctx);
    query_skybox_uniforms(ctx);
    query_postfx_uniforms(ctx);

    gl.UseProgram(ctx->program);
    gl.Uniform1i(ctx->uDiffuseTex, 0);
    gl.Uniform1i(ctx->uNormalTex, 1);
    gl.Uniform1i(ctx->uSpecularTex, 2);
    gl.Uniform1i(ctx->uEmissiveTex, 3);
    gl.Uniform1i(ctx->uShadowTex, 4);
    gl.Uniform1i(ctx->uSplatTex, 5);
    gl.Uniform1i(ctx->uSplatLayer0, 6);
    gl.Uniform1i(ctx->uSplatLayer1, 7);
    gl.Uniform1i(ctx->uSplatLayer2, 8);
    gl.Uniform1i(ctx->uSplatLayer3, 9);
    gl.Uniform1i(ctx->uMorphDeltas, 10);
    gl.Uniform1i(ctx->uMorphNormalDeltas, 11);
    gl.Uniform1i(ctx->uEnvMap, 12);
    gl.Uniform1i(ctx->uMetallicRoughnessTex, 14);
    gl.Uniform1i(ctx->uAOTex, 15);

    gl.UseProgram(ctx->shadow_program);
    gl.Uniform1i(ctx->shadow_uMorphDeltas, 10);

    gl.UseProgram(ctx->skybox_program);
    gl.Uniform1i(ctx->skybox_uSkybox, 13);

    gl.UseProgram(ctx->postfx_program);
    gl.Uniform1i(ctx->postfx_uSceneTex, 0);
    gl.Uniform1i(ctx->postfx_uSceneDepthTex, 1);
    gl.Uniform1i(ctx->postfx_uSceneMotionTex, 2);

    GLuint main_block = gl.GetUniformBlockIndex(ctx->program, "Bones");
    GLuint prev_main_block = gl.GetUniformBlockIndex(ctx->program, "PrevBones");
    GLuint shadow_block = gl.GetUniformBlockIndex(ctx->shadow_program, "Bones");
    if (main_block != GL_INVALID_INDEX)
        gl.UniformBlockBinding(ctx->program, main_block, 0);
    if (prev_main_block != GL_INVALID_INDEX)
        gl.UniformBlockBinding(ctx->program, prev_main_block, 1);
    if (shadow_block != GL_INVALID_INDEX)
        gl.UniformBlockBinding(ctx->shadow_program, shadow_block, 0);

    gl.GenVertexArrays(1, &ctx->vao);
    gl.GenVertexArrays(1, &ctx->fullscreen_vao);
    gl.GenVertexArrays(1, &ctx->skybox_vao);
    gl.GenBuffers(1, &ctx->mesh_vbo);
    gl.GenBuffers(1, &ctx->mesh_ibo);
    gl.GenBuffers(1, &ctx->instance_vbo);
    gl.GenBuffers(1, &ctx->prev_instance_vbo);
    gl.GenBuffers(1, &ctx->bone_ubo);
    gl.GenBuffers(1, &ctx->prev_bone_ubo);
    gl.GenBuffers(1, &ctx->morph_buffer);
    gl.GenBuffers(1, &ctx->morph_normal_buffer);
    gl.GenBuffers(1, &ctx->skybox_vbo);
    gl.GenTextures(1, &ctx->morph_tbo);
    gl.GenTextures(1, &ctx->morph_normal_tbo);

    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->mesh_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(4 * 1024 * 1024), NULL, GL_STREAM_DRAW);
    ctx->mesh_vbo_capacity = 4u * 1024u * 1024u;
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->mesh_ibo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(1 * 1024 * 1024), NULL, GL_STREAM_DRAW);
    ctx->mesh_ibo_capacity = 1u * 1024u * 1024u;
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->instance_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->instance_vbo_capacity = 64u * 1024u;
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->prev_instance_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->prev_instance_vbo_capacity = 64u * 1024u;
    gl.BindBuffer(GL_TEXTURE_BUFFER, ctx->morph_buffer);
    gl.BufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->morph_capacity_bytes = 64u * 1024u;
    gl.BindBuffer(GL_TEXTURE_BUFFER, ctx->morph_normal_buffer);
    gl.BufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->morph_normal_capacity_bytes = 64u * 1024u;

    gl.BindBuffer(GL_UNIFORM_BUFFER, ctx->bone_ubo);
    gl.BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)(128 * 16 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->bone_ubo);
    gl.BindBuffer(GL_UNIFORM_BUFFER, ctx->prev_bone_ubo);
    gl.BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)(128 * 16 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 1, ctx->prev_bone_ubo);

    gl.BindVertexArray(ctx->skybox_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->skybox_vbo);
    gl.BufferData(GL_ARRAY_BUFFER,
                  (GLsizeiptr)sizeof(gl_skybox_vertices),
                  gl_skybox_vertices,
                  GL_STATIC_DRAW);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * (GLsizei)sizeof(float), (void *)0);
    gl.EnableVertexAttribArray(0);

    gl.Enable(GL_DEPTH_TEST);
    gl.DepthFunc(GL_LESS);
    gl.Enable(GL_CULL_FACE);
    gl.CullFace(GL_BACK);
    gl.FrontFace(GL_CCW);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (ensure_scene_targets(ctx, w, h) != 0) {
        gl.DeleteProgram(ctx->program);
        gl.DeleteProgram(ctx->shadow_program);
        gl.DeleteProgram(ctx->postfx_program);
        gl.DeleteProgram(ctx->skybox_program);
        glx.DestroyContext(dpy, glxCtx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

static void gl_destroy_ctx(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;

    if (ctx->display && ctx->window && ctx->glxCtx)
        glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);

    texture_cache_destroy(ctx);
    gl_destroy_mesh_cache(ctx);
    destroy_scene_targets(ctx);
    destroy_rtt_targets(ctx);
    destroy_shadow_targets(ctx);

    if (ctx->skybox_vbo)
        gl.DeleteBuffers(1, &ctx->skybox_vbo);
    if (ctx->morph_normal_tbo)
        gl.DeleteTextures(1, &ctx->morph_normal_tbo);
    if (ctx->morph_tbo)
        gl.DeleteTextures(1, &ctx->morph_tbo);
    if (ctx->morph_normal_buffer)
        gl.DeleteBuffers(1, &ctx->morph_normal_buffer);
    if (ctx->bone_ubo)
        gl.DeleteBuffers(1, &ctx->bone_ubo);
    if (ctx->prev_bone_ubo)
        gl.DeleteBuffers(1, &ctx->prev_bone_ubo);
    if (ctx->morph_buffer)
        gl.DeleteBuffers(1, &ctx->morph_buffer);
    if (ctx->instance_vbo)
        gl.DeleteBuffers(1, &ctx->instance_vbo);
    if (ctx->prev_instance_vbo)
        gl.DeleteBuffers(1, &ctx->prev_instance_vbo);
    if (ctx->mesh_vbo)
        gl.DeleteBuffers(1, &ctx->mesh_vbo);
    if (ctx->mesh_ibo)
        gl.DeleteBuffers(1, &ctx->mesh_ibo);
    if (ctx->skybox_vao)
        gl.DeleteVertexArrays(1, &ctx->skybox_vao);
    if (ctx->fullscreen_vao)
        gl.DeleteVertexArrays(1, &ctx->fullscreen_vao);
    if (ctx->vao)
        gl.DeleteVertexArrays(1, &ctx->vao);
    if (ctx->program)
        gl.DeleteProgram(ctx->program);
    if (ctx->shadow_program)
        gl.DeleteProgram(ctx->shadow_program);
    if (ctx->postfx_program)
        gl.DeleteProgram(ctx->postfx_program);
    if (ctx->skybox_program)
        gl.DeleteProgram(ctx->skybox_program);
    if (ctx->glxCtx && ctx->display)
        glx.DestroyContext(ctx->display, ctx->glxCtx);
    free(ctx);
}

static void gl_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    ctx->clearR = r;
    ctx->clearG = g;
    ctx->clearB = b;
}

static void gl_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    static const float kIdentity4x4[16] = {
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    if (!ctx || !cam)
        return;

    ctx->frame_serial++;
    ctx->shadow_active = 0;
    if (ctx->prev_vp_valid)
        memcpy(ctx->prev_vp, ctx->vp, sizeof(ctx->prev_vp));
    memcpy(ctx->view, cam->view, sizeof(ctx->view));
    memcpy(ctx->projection, cam->projection, sizeof(ctx->projection));
    mat4f_mul_gl(cam->projection, cam->view, ctx->vp);
    if (mat4f_inverse_gl(ctx->vp, ctx->inv_vp) != 0)
        memcpy(ctx->inv_vp, kIdentity4x4, sizeof(ctx->inv_vp));
    if (!ctx->prev_vp_valid) {
        memcpy(ctx->prev_vp, ctx->vp, sizeof(ctx->prev_vp));
        ctx->prev_vp_valid = 1;
    }
    memcpy(ctx->cam_pos, cam->position, sizeof(float) * 3);
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    ctx->fog_color[0] = cam->fog_color[0];
    ctx->fog_color[1] = cam->fog_color[1];
    ctx->fog_color[2] = cam->fog_color[2];
    glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);

    if (!ctx->rtt_active)
        ensure_scene_targets(ctx, ctx->width, ctx->height);
    bind_main_framebuffer(ctx);

    if (ctx->rtt_active) {
        GLbitfield clear_mask = 0;
        if (!cam->load_existing_color) {
            gl.ClearColor(ctx->clearR, ctx->clearG, ctx->clearB, 1.0f);
            clear_mask |= GL_COLOR_BUFFER_BIT;
        }
        if (!cam->load_existing_depth) {
            gl.ClearDepth(1.0);
            clear_mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (clear_mask)
            gl.Clear(clear_mask);
    } else {
        if (!cam->load_existing_color) {
            gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl.ClearColor(ctx->clearR, ctx->clearG, ctx->clearB, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            gl.DrawBuffer(GL_COLOR_ATTACHMENT1);
            gl.ClearColor(0.5f, 0.5f, 0.0f, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        if (!cam->load_existing_depth) {
            gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl.ClearDepth(1.0);
            gl.Clear(GL_DEPTH_BUFFER_BIT);
        }
        {
            const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            gl.DrawBuffers(2, attachments);
        }
    }
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthMask(GL_TRUE);
    gl.UseProgram(ctx->program);
    gl.BindVertexArray(ctx->vao);
}

static void gl_draw_skybox(void *ctx_ptr, const void *cubemap_ptr) {
    gl_draw_skybox_impl((gl_context_t *)ctx_ptr, (const rt_cubemap3d *)cubemap_ptr);
}

static void gl_submit_draw(void *ctx_ptr,
                           vgfx_window_t win,
                           const vgfx3d_draw_cmd_t *cmd,
                           const vgfx3d_light_params_t *lights,
                           int32_t light_count,
                           const float *ambient,
                           int8_t wireframe,
                           int8_t backface_cull) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLuint mesh_vbo, mesh_ibo;
    if (!ctx || !cmd || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;

    if (backface_cull)
        gl.Enable(GL_CULL_FACE);
    else
        gl.Disable(GL_CULL_FACE);
    gl.PolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    gl.DepthMask(vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? GL_FALSE : GL_TRUE);

    gl.UseProgram(ctx->program);
    upload_main_uniforms(ctx, cmd, lights, light_count, ambient, 0);
    bind_material_textures(ctx, cmd);
    prepare_mesh_buffers(ctx, cmd, &mesh_vbo, &mesh_ibo);
    configure_mesh_attributes(ctx, mesh_vbo, mesh_ibo);
    gl.DrawElements(GL_TRIANGLES, (GLsizei)cmd->index_count, GL_UNSIGNED_INT, NULL);
}

static void gl_end_frame(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;

    if (ctx->rtt_active && ctx->rtt_target && ctx->rtt_color_tex) {
        int32_t w = ctx->rtt_width;
        int32_t h = ctx->rtt_height;
        size_t bytes = (size_t)w * (size_t)h * 4;
        uint8_t *tmp = (uint8_t *)malloc(bytes);
        if (!tmp)
            return;
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
        gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        vgfx3d_flip_rgba_rows(tmp, w, h);
        memcpy(ctx->rtt_target->color_buf, tmp, bytes);
        free(tmp);
    }
}

static void gl_resize(void *ctx_ptr, int32_t w, int32_t h) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx || w <= 0 || h <= 0)
        return;
    ctx->width = w;
    ctx->height = h;
    glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);
    if (!ctx->rtt_active)
        destroy_scene_targets(ctx);
}

static int gl_readback_rgba(
    void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    int32_t copy_w, copy_h;
    uint8_t *tmp;

    if (!ctx || !dst_rgba || w <= 0 || h <= 0 || stride < w * 4)
        return 0;

    glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);
    if (!ctx->scene_fbo || ctx->scene_width <= 0 || ctx->scene_height <= 0)
        return 0;

    copy_w = ctx->scene_width < w ? ctx->scene_width : w;
    copy_h = ctx->scene_height < h ? ctx->scene_height : h;
    tmp = (uint8_t *)malloc((size_t)copy_w * (size_t)copy_h * 4u);
    if (!tmp)
        return 0;

    memset(dst_rgba, 0, (size_t)stride * (size_t)h);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadPixels(0, 0, copy_w, copy_h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    vgfx3d_flip_rgba_rows(tmp, copy_w, copy_h);
    for (int32_t y = 0; y < copy_h; y++)
        memcpy(dst_rgba + (size_t)y * (size_t)stride,
               tmp + (size_t)y * (size_t)copy_w * 4u,
               (size_t)copy_w * 4u);
    free(tmp);

    if (ctx->rtt_active)
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
    else
        bind_main_framebuffer(ctx);
    return 1;
}

static void gl_present_impl(gl_context_t *ctx, const vgfx3d_postfx_snapshot_t *snapshot) {
    if (!ctx || ctx->rtt_active)
        return;
    glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);
    draw_scene_texture(ctx, snapshot);
    glx.SwapBuffers(ctx->display, ctx->window);
}

static void gl_present(void *ctx_ptr) {
    gl_present_impl((gl_context_t *)ctx_ptr, NULL);
}

static void gl_present_postfx(void *ctx_ptr, const vgfx3d_postfx_snapshot_t *postfx) {
    gl_present_impl((gl_context_t *)ctx_ptr, postfx);
}

static void gl_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);
    if (!rt) {
        destroy_rtt_targets(ctx);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    ensure_rtt_targets(ctx, rt);
}

static void gl_shadow_begin(
    void *ctx_ptr, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    (void)depth_buf;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx || !light_vp)
        return;
    if (ensure_shadow_targets(ctx, w, h) != 0)
        return;
    memcpy(ctx->shadow_vp, light_vp, sizeof(ctx->shadow_vp));
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl.Viewport(0, 0, w, h);
    gl.ClearDepth(1.0);
    gl.Clear(GL_DEPTH_BUFFER_BIT);
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthMask(GL_TRUE);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.UseProgram(ctx->shadow_program);
    gl.BindVertexArray(ctx->vao);
}

static void gl_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLuint mesh_vbo, mesh_ibo;
    if (!ctx || !cmd || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;
    prepare_mesh_buffers(ctx, cmd, &mesh_vbo, &mesh_ibo);
    configure_mesh_attributes(ctx, mesh_vbo, mesh_ibo);
    gl.UniformMatrix4fv(ctx->shadow_uModelMatrix, 1, GL_TRUE, cmd->model_matrix);
    gl.UniformMatrix4fv(ctx->shadow_uViewProjection, 1, GL_TRUE, ctx->shadow_vp);
    bind_shadow_anim(ctx, cmd);
    gl.DrawElements(GL_TRIANGLES, (GLsizei)cmd->index_count, GL_UNSIGNED_INT, NULL);
}

static void gl_shadow_end(void *ctx_ptr, float bias) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    ctx->shadow_active = 1;
    ctx->shadow_bias = bias;
    bind_main_framebuffer(ctx);
    gl.UseProgram(ctx->program);
}

static void gl_submit_draw_instanced(void *ctx_ptr,
                                     vgfx_window_t win,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     const float *instance_matrices,
                                     int32_t instance_count,
                                     const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     const float *ambient,
                                     int8_t wireframe,
                                     int8_t backface_cull) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLuint mesh_vbo, mesh_ibo;
    if (!ctx || !cmd || !instance_matrices || instance_count <= 0)
        return;

    if (backface_cull)
        gl.Enable(GL_CULL_FACE);
    else
        gl.Disable(GL_CULL_FACE);
    gl.PolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    gl.DepthMask(vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? GL_FALSE : GL_TRUE);

    gl.UseProgram(ctx->program);
    upload_main_uniforms(ctx, cmd, lights, light_count, ambient, 1);
    bind_material_textures(ctx, cmd);
    prepare_mesh_buffers(ctx, cmd, &mesh_vbo, &mesh_ibo);
    configure_mesh_attributes(ctx, mesh_vbo, mesh_ibo);
    configure_instance_attributes(ctx,
                                  instance_matrices,
                                  cmd->has_prev_instance_matrices ? cmd->prev_instance_matrices
                                                                  : NULL,
                                  instance_count);
    gl.DrawElementsInstanced(
        GL_TRIANGLES, (GLsizei)cmd->index_count, GL_UNSIGNED_INT, NULL, (GLsizei)instance_count);
    set_identity_instance_constants();
}

const vgfx3d_backend_t vgfx3d_opengl_backend = {
    .name = "opengl",
    .create_ctx = gl_create_ctx,
    .destroy_ctx = gl_destroy_ctx,
    .clear = gl_clear,
    .resize = gl_resize,
    .begin_frame = gl_begin_frame,
    .submit_draw = gl_submit_draw,
    .end_frame = gl_end_frame,
    .set_render_target = gl_set_render_target,
    .shadow_begin = gl_shadow_begin,
    .shadow_draw = gl_shadow_draw,
    .shadow_end = gl_shadow_end,
    .draw_skybox = gl_draw_skybox,
    .submit_draw_instanced = gl_submit_draw_instanced,
    .present = gl_present,
    .readback_rgba = gl_readback_rgba,
    .present_postfx = gl_present_postfx,
};

#endif /* __linux__ && VIPER_ENABLE_GRAPHICS */
