//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_opengl.c
// Purpose: OpenGL 3.3 Core GPU backend for Viper.Graphics3D (Linux).
//   Custom GL loader via dlopen (no GLAD/GLEW dependency).
//
// Key invariants:
//   - Requires OpenGL 3.3 Core Profile via Mesa
//   - Falls back to software if libGL.so not found or context fails
//   - GLSL 330 core shaders compiled at runtime
//   - Row-major matrices uploaded with GL_TRUE transpose flag
//   - Depth buffer GL_DEPTH_COMPONENT32F
//
// Links: vgfx3d_backend.h, plans/3d/04-opengl-backend.md
//
//===----------------------------------------------------------------------===//

#if defined(__linux__) && defined(VIPER_ENABLE_GRAPHICS)

#include "vgfx3d_backend.h"
#include "vgfx.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>

//=============================================================================
// GL type definitions (subset of GL 3.3 Core)
//=============================================================================

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

#define GL_TRUE                   1
#define GL_FALSE                  0
#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_DEPTH_BUFFER_BIT       0x00000100
#define GL_DEPTH_TEST             0x0B71
#define GL_CULL_FACE              0x0B44
#define GL_BACK                   0x0405
#define GL_FRONT                  0x0404
#define GL_CCW                    0x0901
#define GL_CW                     0x0900
#define GL_LESS                   0x0201
#define GL_TRIANGLES              0x0004
#define GL_UNSIGNED_INT           0x1405
#define GL_FLOAT                  0x1406
#define GL_UNSIGNED_BYTE          0x1401
#define GL_ARRAY_BUFFER           0x8892
#define GL_ELEMENT_ARRAY_BUFFER   0x8893
#define GL_STATIC_DRAW            0x88E4
#define GL_VERTEX_SHADER          0x8B31
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_INFO_LOG_LENGTH        0x8B84
#define GL_TEXTURE_2D             0x0DE1
#define GL_RGBA                   0x1908
#define GL_RGBA8                  0x8058
#define GL_DEPTH_COMPONENT32F     0x8CAC
#define GL_TEXTURE0               0x84C0
#define GL_BLEND                  0x0BE2
#define GL_SRC_ALPHA              0x0302
#define GL_ONE_MINUS_SRC_ALPHA    0x0303

//=============================================================================
// GL function pointer types
//=============================================================================

typedef void (*PFNGLCLEARPROC)(GLuint);
typedef void (*PFNGLCLEARCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLCLEARDEPTHPROC)(double);
typedef void (*PFNGLENABLEPROC)(GLenum);
typedef void (*PFNGLDISABLEPROC)(GLenum);
typedef void (*PFNGLDEPTHFUNCPROC)(GLenum);
typedef void (*PFNGLCULLFACEPROC)(GLenum);
typedef void (*PFNGLFRONTFACEPROC)(GLenum);
typedef void (*PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const void *);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar **, const GLint *);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (*PFNGLDELETESHADERPROC)(GLuint);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (*PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void (*PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (*PFNGLGENBUFFERSPROC)(GLsizei, GLuint *);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (*PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint, GLint, GLenum, GLsizei, const void *);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void (*PFNGLBLENDFUNCPROC)(GLenum, GLenum);
typedef void (*PFNGLDEPTHMASKPROC)(GLboolean);

//=============================================================================
// Loaded GL function pointers
//=============================================================================

static struct {
    void *lib;
    PFNGLCLEARPROC Clear;
    PFNGLCLEARCOLORPROC ClearColor;
    PFNGLCLEARDEPTHPROC ClearDepth;
    PFNGLENABLEPROC Enable;
    PFNGLDISABLEPROC Disable;
    PFNGLDEPTHFUNCPROC DepthFunc;
    PFNGLCULLFACEPROC CullFace;
    PFNGLFRONTFACEPROC FrontFace;
    PFNGLVIEWPORTPROC Viewport;
    PFNGLDRAWELEMENTSPROC DrawElements;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLVERTEXATTRIBIPOINTERPROC VertexAttribIPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLBLENDFUNCPROC BlendFunc;
    PFNGLDEPTHMASKPROC DepthMask;
} gl;

/* GLX types */
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

static struct {
    PFNGLXCHOOSEFBCONFIGPROC ChooseFBConfig;
    PFNGLXCREATENEWCONTEXTPROC CreateNewContext;
    PFNGLXSWAPBUFFERSPROC SwapBuffers;
    PFNGLXMAKECURRENTPROC MakeCurrent;
    PFNGLXDESTROYCONTEXTPROC DestroyContext;
    PFNGLXGETPROCADDRESSPROC GetProcAddress;
} glx;

#define GLX_RGBA_BIT   0x0001
#define GLX_RENDER_TYPE 0x8011
#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_WINDOW_BIT 0x0001
#define GLX_DOUBLEBUFFER 5
#define GLX_DEPTH_SIZE 12
#define GLX_RED_SIZE   8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE  10
#define GLX_ALPHA_SIZE 11
#define GLX_RGBA_TYPE  0x8014

static int gl_loaded = 0;

static int load_gl(void) {
    if (gl_loaded) return 0;
    gl.lib = dlopen("libGL.so.1", RTLD_LAZY);
    if (!gl.lib) gl.lib = dlopen("libGL.so", RTLD_LAZY);
    if (!gl.lib) return -1;

    #define LOAD(name) gl.name = (typeof(gl.name))dlsym(gl.lib, "gl" #name); if (!gl.name) return -1
    #define LOADX(name) glx.name = (typeof(glx.name))dlsym(gl.lib, "glX" #name); if (!glx.name) return -1

    LOAD(Clear); LOAD(ClearColor); LOAD(ClearDepth);
    LOAD(Enable); LOAD(Disable); LOAD(DepthFunc);
    LOAD(CullFace); LOAD(FrontFace); LOAD(Viewport);
    LOAD(DrawElements);
    LOAD(BlendFunc);
    LOAD(DepthMask);

    LOADX(ChooseFBConfig); LOADX(CreateNewContext); LOADX(SwapBuffers);
    LOADX(MakeCurrent); LOADX(DestroyContext); LOADX(GetProcAddress);

    /* GL 3.3 functions via glXGetProcAddress */
    #define LOADP(name) gl.name = (typeof(gl.name))glx.GetProcAddress((const unsigned char *)"gl" #name); if (!gl.name) return -1
    LOADP(CreateShader); LOADP(ShaderSource); LOADP(CompileShader);
    LOADP(GetShaderiv); LOADP(GetShaderInfoLog);
    LOADP(CreateProgram); LOADP(AttachShader); LOADP(LinkProgram);
    LOADP(GetProgramiv); LOADP(UseProgram); LOADP(DeleteShader);
    LOADP(GetUniformLocation); LOADP(UniformMatrix4fv);
    LOADP(Uniform3f); LOADP(Uniform1i); LOADP(Uniform1f); LOADP(Uniform4f);
    LOADP(GenVertexArrays); LOADP(BindVertexArray);
    LOADP(GenBuffers); LOADP(BindBuffer); LOADP(BufferData);
    LOADP(VertexAttribPointer); LOADP(VertexAttribIPointer);
    LOADP(EnableVertexAttribArray);
    LOADP(DeleteBuffers); LOADP(DeleteVertexArrays); LOADP(DeleteProgram);

    #undef LOAD
    #undef LOADX
    #undef LOADP

    gl_loaded = 1;
    return 0;
}

//=============================================================================
// GLSL 330 Core shaders
//=============================================================================

static const char *glsl_vertex_src =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPosition;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec2 aUV;\n"
    "layout(location=3) in vec4 aColor;\n"
    "layout(location=4) in vec3 aTangent;\n"
    "layout(location=5) in uvec4 aBoneIdx;\n"
    "layout(location=6) in vec4 aBoneWt;\n"
    "uniform mat4 uModelMatrix;\n"
    "uniform mat4 uViewProjection;\n"
    "uniform mat4 uNormalMatrix;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec2 vUV;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vec4 wp = uModelMatrix * vec4(aPosition, 1.0);\n"
    "    gl_Position = uViewProjection * wp;\n"
    "    vWorldPos = wp.xyz;\n"
    "    vNormal = (uNormalMatrix * vec4(aNormal, 0.0)).xyz;\n"
    "    vUV = aUV;\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *glsl_fragment_src =
    "#version 330 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec2 vUV;\n"
    "in vec4 vColor;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uCameraPos;\n"
    "uniform vec3 uAmbientColor;\n"
    "uniform vec4 uDiffuseColor;\n"
    "uniform vec4 uSpecularColor;\n" /* w = shininess */
    "uniform vec3 uEmissiveColor;\n"
    "uniform int uUnlit;\n"
    "uniform int uLightCount;\n"
    "uniform int uLightType[8];\n"
    "uniform vec3 uLightDir[8];\n"
    "uniform vec3 uLightPos[8];\n"
    "uniform vec3 uLightColor[8];\n"
    "uniform float uLightIntensity[8];\n"
    "uniform float uLightAtten[8];\n"
    "void main() {\n"
    "    if (uUnlit != 0) { FragColor = vec4(uDiffuseColor.rgb, uAlpha); return; }\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    "    vec3 result = uAmbientColor * uDiffuseColor.rgb;\n"
    "    for (int i = 0; i < uLightCount; i++) {\n"
    "        vec3 L; float atten = 1.0;\n"
    "        if (uLightType[i] == 0) {\n"
    "            L = normalize(-uLightDir[i]);\n"
    "        } else if (uLightType[i] == 1) {\n"
    "            vec3 tl = uLightPos[i] - vWorldPos;\n"
    "            float d = length(tl); L = tl / max(d, 0.0001);\n"
    "            atten = 1.0 / (1.0 + uLightAtten[i] * d * d);\n"
    "        } else {\n"
    "            result += uLightColor[i] * uLightIntensity[i] * uDiffuseColor.rgb;\n"
    "            continue;\n"
    "        }\n"
    "        float NdotL = max(dot(N, L), 0.0);\n"
    "        result += uLightColor[i] * uLightIntensity[i] * NdotL * uDiffuseColor.rgb * atten;\n"
    "        if (NdotL > 0.0 && uSpecularColor.w > 0.0) {\n"
    "            vec3 H = normalize(L + V);\n"
    "            float spec = pow(max(dot(N, H), 0.0), uSpecularColor.w);\n"
    "            result += uLightColor[i] * uLightIntensity[i] * spec * uSpecularColor.rgb * atten;\n"
    "        }\n"
    "    }\n"
    "    result += uEmissiveColor;\n"
    "    FragColor = vec4(result, uAlpha);\n"
    "}\n";

//=============================================================================
// OpenGL context
//=============================================================================

typedef struct {
    Display *display;
    Window window;
    GLXContext glxCtx;
    GLuint program;
    GLuint vao;
    int32_t width, height;
    float vp[16];
    float cam_pos[3];
    float clearR, clearG, clearB;
    /* Uniform locations */
    GLint uModelMatrix, uViewProjection, uNormalMatrix;
    GLint uCameraPos, uAmbientColor, uDiffuseColor, uSpecularColor, uEmissiveColor, uAlpha;
    GLint uUnlit, uLightCount;
    GLint uLightType[8], uLightDir[8], uLightPos[8];
    GLint uLightColor[8], uLightIntensity[8], uLightAtten[8];
} gl_context_t;

//=============================================================================
// Shader compilation helper
//=============================================================================

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, NULL);
    gl.CompileShader(s);
    GLint ok;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        gl.GetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "[OpenGL] Shader error: %s\n", log);
        gl.DeleteShader(s);
        return 0;
    }
    return s;
}

static void mat4f_mul_gl(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r*4+c] = a[r*4+0]*b[0*4+c] + a[r*4+1]*b[1*4+c] +
                         a[r*4+2]*b[2*4+c] + a[r*4+3]*b[3*4+c];
}

//=============================================================================
// Backend vtable
//=============================================================================

static void *gl_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    if (load_gl() != 0) return NULL;

    /* Get X11 display and window from vgfx */
    void *native = vgfx_get_native_view(win);
    if (!native) return NULL;
    Window xwin = (Window)(uintptr_t)native;

    /* Get Display from vgfx (shared X11 connection) */
    Display *dpy = (Display *)vgfx_get_native_display(win);
    if (!dpy) return NULL; /* No X11 display available */

    /* Choose FBConfig */
    int fb_attribs[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_DOUBLEBUFFER, 1,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        0
    };
    int fb_count = 0;
    GLXFBConfig *configs = glx.ChooseFBConfig(dpy, DefaultScreen(dpy), fb_attribs, &fb_count);
    if (!configs || fb_count == 0) { /* dpy owned by vgfx */return NULL; }

    /* Create GLX context */
    GLXContext glxCtx = glx.CreateNewContext(dpy, configs[0], GLX_RGBA_TYPE, NULL, 1);
    XFree(configs);
    if (!glxCtx) { /* dpy owned by vgfx */return NULL; }

    glx.MakeCurrent(dpy, xwin, glxCtx);

    gl_context_t *ctx = (gl_context_t *)calloc(1, sizeof(gl_context_t));
    if (!ctx) { glx.DestroyContext(dpy, glxCtx); /* dpy owned by vgfx */return NULL; }
    ctx->display = dpy;
    ctx->window = xwin;
    ctx->glxCtx = glxCtx;
    ctx->width = w;
    ctx->height = h;

    /* Compile shaders */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, glsl_vertex_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, glsl_fragment_src);
    if (!vs || !fs) { free(ctx); glx.DestroyContext(dpy, glxCtx); /* dpy owned by vgfx */return NULL; }

    ctx->program = gl.CreateProgram();
    gl.AttachShader(ctx->program, vs);
    gl.AttachShader(ctx->program, fs);
    gl.LinkProgram(ctx->program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    GLint linked;
    gl.GetProgramiv(ctx->program, GL_LINK_STATUS, &linked);
    if (!linked) { free(ctx); glx.DestroyContext(dpy, glxCtx); /* dpy owned by vgfx */return NULL; }

    /* Get uniform locations */
    ctx->uModelMatrix = gl.GetUniformLocation(ctx->program, "uModelMatrix");
    ctx->uViewProjection = gl.GetUniformLocation(ctx->program, "uViewProjection");
    ctx->uNormalMatrix = gl.GetUniformLocation(ctx->program, "uNormalMatrix");
    ctx->uCameraPos = gl.GetUniformLocation(ctx->program, "uCameraPos");
    ctx->uAmbientColor = gl.GetUniformLocation(ctx->program, "uAmbientColor");
    ctx->uDiffuseColor = gl.GetUniformLocation(ctx->program, "uDiffuseColor");
    ctx->uSpecularColor = gl.GetUniformLocation(ctx->program, "uSpecularColor");
    ctx->uEmissiveColor = gl.GetUniformLocation(ctx->program, "uEmissiveColor");
    ctx->uAlpha = gl.GetUniformLocation(ctx->program, "uAlpha");
    ctx->uUnlit = gl.GetUniformLocation(ctx->program, "uUnlit");
    ctx->uLightCount = gl.GetUniformLocation(ctx->program, "uLightCount");
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
    }

    /* Create VAO */
    gl.GenVertexArrays(1, &ctx->vao);

    /* Enable depth test and backface culling */
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthFunc(GL_LESS);
    gl.Enable(GL_CULL_FACE);
    gl.CullFace(GL_BACK);
    /* OpenGL tests winding in clip space (no Y-flip issue like Metal/D3D11).
     * CCW = front-facing is the OpenGL default and matches our convention. */
    gl.FrontFace(GL_CCW);

    return ctx;
}

static void gl_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr) return;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (ctx->program) gl.DeleteProgram(ctx->program);
    if (ctx->vao) gl.DeleteVertexArrays(1, &ctx->vao);
    if (ctx->glxCtx && ctx->display) glx.DestroyContext(ctx->display, ctx->glxCtx);
    /* Display is owned by vgfx — do NOT close it here */
    free(ctx);
}

static void gl_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    ctx->clearR = r; ctx->clearG = g; ctx->clearB = b;
}

static void gl_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    mat4f_mul_gl(cam->projection, cam->view, ctx->vp);
    memcpy(ctx->cam_pos, cam->position, sizeof(float) * 3);

    glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);
    gl.Viewport(0, 0, ctx->width, ctx->height);
    gl.ClearColor(ctx->clearR, ctx->clearG, ctx->clearB, 1.0f);
    gl.ClearDepth(1.0);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.UseProgram(ctx->program);
}

static void gl_submit_draw(void *ctx_ptr, vgfx_window_t win,
                            const vgfx3d_draw_cmd_t *cmd,
                            const vgfx3d_light_params_t *lights, int32_t light_count,
                            const float *ambient, int8_t wireframe, int8_t backface_cull) {
    (void)win; (void)wireframe;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;

    if (backface_cull)
        gl.Enable(GL_CULL_FACE);
    else
        gl.Disable(GL_CULL_FACE);

    /* Upload matrices (GL_TRUE = transpose from row-major to column-major) */
    gl.UniformMatrix4fv(ctx->uModelMatrix, 1, GL_TRUE, cmd->model_matrix);
    gl.UniformMatrix4fv(ctx->uViewProjection, 1, GL_TRUE, ctx->vp);
    gl.UniformMatrix4fv(ctx->uNormalMatrix, 1, GL_TRUE, cmd->model_matrix);

    /* Material uniforms */
    gl.Uniform4f(ctx->uDiffuseColor, cmd->diffuse_color[0], cmd->diffuse_color[1],
                 cmd->diffuse_color[2], cmd->diffuse_color[3]);
    gl.Uniform4f(ctx->uSpecularColor, cmd->specular[0], cmd->specular[1],
                 cmd->specular[2], cmd->shininess);
    gl.Uniform3f(ctx->uEmissiveColor, cmd->emissive_color[0], cmd->emissive_color[1], cmd->emissive_color[2]);
    gl.Uniform1f(ctx->uAlpha, cmd->alpha);
    gl.Uniform1i(ctx->uUnlit, cmd->unlit);

    /* Toggle depth write for transparent draws */
    gl.DepthMask(cmd->alpha >= 1.0f ? GL_TRUE : GL_FALSE);

    /* Scene uniforms */
    gl.Uniform3f(ctx->uCameraPos, ctx->cam_pos[0], ctx->cam_pos[1], ctx->cam_pos[2]);
    gl.Uniform3f(ctx->uAmbientColor, ambient[0], ambient[1], ambient[2]);
    gl.Uniform1i(ctx->uLightCount, light_count);

    /* Light uniforms */
    for (int32_t i = 0; i < light_count && i < 8; i++) {
        gl.Uniform1i(ctx->uLightType[i], lights[i].type);
        gl.Uniform3f(ctx->uLightDir[i], lights[i].direction[0], lights[i].direction[1], lights[i].direction[2]);
        gl.Uniform3f(ctx->uLightPos[i], lights[i].position[0], lights[i].position[1], lights[i].position[2]);
        gl.Uniform3f(ctx->uLightColor[i], lights[i].color[0], lights[i].color[1], lights[i].color[2]);
        gl.Uniform1f(ctx->uLightIntensity[i], lights[i].intensity);
        gl.Uniform1f(ctx->uLightAtten[i], lights[i].attenuation);
    }

    /* Create and bind VBO + IBO */
    GLuint vbo, ibo;
    gl.GenBuffers(1, &vbo);
    gl.GenBuffers(1, &ibo);

    gl.BindVertexArray(ctx->vao);

    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(cmd->vertex_count * sizeof(vgfx3d_vertex_t)),
                  cmd->vertices, GL_STATIC_DRAW);

    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cmd->index_count * sizeof(uint32_t)),
                  cmd->indices, GL_STATIC_DRAW);

    /* Vertex attributes (80-byte stride) */
    GLsizei stride = (GLsizei)sizeof(vgfx3d_vertex_t);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);   /* pos */
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)12);  /* normal */
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)24);  /* uv */
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)32);  /* color */
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void *)48);  /* tangent */
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, stride, (void *)60);   /* bone indices */
    gl.EnableVertexAttribArray(5);
    gl.VertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void *)64);  /* bone weights */
    gl.EnableVertexAttribArray(6);

    gl.DrawElements(GL_TRIANGLES, (GLsizei)cmd->index_count, GL_UNSIGNED_INT, NULL);

    gl.DeleteBuffers(1, &vbo);
    gl.DeleteBuffers(1, &ibo);
}

static void gl_end_frame(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    glx.SwapBuffers(ctx->display, ctx->window);
}

static void gl_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt)
{
    (void)ctx_ptr; (void)rt;
}

const vgfx3d_backend_t vgfx3d_opengl_backend = {
    .name = "opengl",
    .create_ctx = gl_create_ctx,
    .destroy_ctx = gl_destroy_ctx,
    .clear = gl_clear,
    .begin_frame = gl_begin_frame,
    .submit_draw = gl_submit_draw,
    .end_frame = gl_end_frame,
    .set_render_target = gl_set_render_target,
};

#endif /* __linux__ && VIPER_ENABLE_GRAPHICS */
