//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_metal.m
// Purpose: Metal GPU backend for Viper.Graphics3D.
//
// Links: vgfx3d_backend.h, plans/3d/02-metal-backend.md
//
//===----------------------------------------------------------------------===//

#if defined(__APPLE__) && defined(VIPER_ENABLE_GRAPHICS)

/* Include sys/types.h before Metal headers to define BSD types (u_int, u_char)
 * that are hidden by _POSIX_C_SOURCE but required by macOS system headers. */
#include <sys/types.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

#include "vgfx3d_backend.h"
#include "vgfx.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Objective-C wrapper to hold Metal objects under ARC
//=============================================================================

@interface VGFXMetalContext : NSObject {
    @public
    float _vp[16];
    float _camPos[3];
}
@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, strong) id<MTLDepthStencilState> depthState;
@property (nonatomic, strong) id<MTLDepthStencilState> depthStateNoWrite;
@property (nonatomic, strong) CAMetalLayer *metalLayer;
@property (nonatomic, strong) id<MTLTexture> depthTexture;
@property (nonatomic, strong) id<MTLLibrary> library;
@property (nonatomic, strong) id<MTLCommandBuffer> cmdBuf;
@property (nonatomic, strong) id<MTLRenderCommandEncoder> encoder;
@property (nonatomic, strong) id<CAMetalDrawable> drawable;
@property (nonatomic) int32_t width;
@property (nonatomic) int32_t height;
@property (nonatomic) float clearR, clearG, clearB;
@property (nonatomic) BOOL inFrame;
@end

@implementation VGFXMetalContext
@end

//=============================================================================
// MSL Shader source
//=============================================================================

static NSString *metal_shader_source = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float3 position [[attribute(0)]];\n"
    "    float3 normal   [[attribute(1)]];\n"
    "    float2 uv       [[attribute(2)]];\n"
    "    float4 color    [[attribute(3)]];\n"
    "    float3 tangent  [[attribute(4)]];\n"
    "    uchar4 boneIdx  [[attribute(5)]];\n"
    "    float4 boneWt   [[attribute(6)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float3 worldPos;\n"
    "    float3 normal;\n"
    "    float2 uv;\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "struct PerObject {\n"
    "    float4x4 modelMatrix;\n"
    "    float4x4 viewProjection;\n"
    "    float4x4 normalMatrix;\n"
    "};\n"
    "\n"
    "struct Light {\n"
    "    int type; float _p0, _p1, _p2;\n"
    "    float4 direction;\n"
    "    float4 position;\n"
    "    float4 color;\n"
    "    float intensity;\n"
    "    float attenuation;\n"
    "    float _p3[2];\n"
    "};\n"
    "\n"
    "struct PerScene {\n"
    "    float4 cameraPosition;\n"
    "    float4 ambientColor;\n"
    "    int lightCount;\n"
    "    int _pad[3];\n"
    "};\n"
    "\n"
    "struct PerMaterial {\n"
    "    float4 diffuseColor;\n"
    "    float4 specularColor;\n"
    "    float4 emissiveColor;\n"
    "    float alpha;\n"
    "    int hasTexture;\n"
    "    int unlit;\n"
    "    int _pad;\n"
    "};\n"
    "\n"
    "vertex VertexOut vertex_main(\n"
    "    VertexIn in [[stage_in]],\n"
    "    constant PerObject &obj [[buffer(1)]]\n"
    ") {\n"
    "    VertexOut out;\n"
    "    float4 wp = obj.modelMatrix * float4(in.position, 1.0);\n"
    "    out.position = obj.viewProjection * wp;\n"
    "    /* Remap Z from OpenGL NDC [-1,1] to Metal [0,1] */\n"
    "    out.position.z = out.position.z * 0.5 + out.position.w * 0.5;\n"
    "    out.worldPos = wp.xyz;\n"
    "    out.normal = (obj.normalMatrix * float4(in.normal, 0.0)).xyz;\n"
    "    out.uv = in.uv;\n"
    "    out.color = in.color;\n"
    "    return out;\n"
    "}\n"
    "\n"
    "fragment float4 fragment_main(\n"
    "    VertexOut in [[stage_in]],\n"
    "    constant PerScene &scene [[buffer(0)]],\n"
    "    constant PerMaterial &material [[buffer(1)]],\n"
    "    constant Light *lights [[buffer(2)]]\n"
    ") {\n"
    "    if (material.unlit) return float4(material.diffuseColor.rgb, material.alpha);\n"
    "    float3 N = normalize(in.normal);\n"
    "    float3 V = normalize(scene.cameraPosition.xyz - in.worldPos);\n"
    "    float3 result = scene.ambientColor.rgb * material.diffuseColor.rgb;\n"
    "    for (int i = 0; i < scene.lightCount; i++) {\n"
    "        float3 L; float atten = 1.0;\n"
    "        if (lights[i].type == 0) {\n"
    "            L = normalize(-lights[i].direction.xyz);\n"
    "        } else if (lights[i].type == 1) {\n"
    "            float3 tl = lights[i].position.xyz - in.worldPos;\n"
    "            float d = length(tl); L = tl / max(d, 0.0001);\n"
    "            atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
    "        } else {\n"
    "            result += lights[i].color.rgb * lights[i].intensity * material.diffuseColor.rgb;\n"
    "            continue;\n"
    "        }\n"
    "        float NdotL = max(dot(N, L), 0.0);\n"
    "        result += lights[i].color.rgb * lights[i].intensity * NdotL * material.diffuseColor.rgb * atten;\n"
    "        if (NdotL > 0.0 && material.specularColor.w > 0.0) {\n"
    "            float3 H = normalize(L + V);\n"
    "            float spec = pow(max(dot(N, H), 0.0), material.specularColor.w);\n"
    "            result += lights[i].color.rgb * lights[i].intensity * spec * material.specularColor.rgb * atten;\n"
    "        }\n"
    "    }\n"
    "    result += material.emissiveColor.rgb;\n"
    "    return float4(result, material.alpha);\n"
    "}\n";

//=============================================================================
// Uniform buffer structs (must match shader)
//=============================================================================

typedef struct { float m[16]; float vp[16]; float nm[16]; } mtl_per_object_t;
typedef struct { float cp[4]; float ac[4]; int32_t lc; int32_t _p[3]; } mtl_per_scene_t;
typedef struct {
    int32_t type; float _p0, _p1, _p2;
    float dir[4]; float pos[4]; float col[4];
    float intensity; float attenuation; float _p3[2];
} mtl_light_t;
typedef struct { float dc[4]; float sc[4]; float ec[4]; float alpha; int32_t ht; int32_t unlit; int32_t _p; } mtl_per_material_t;

//=============================================================================
// Helpers
//=============================================================================

static void transpose4x4(const float *src, float *dst) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst[c * 4 + r] = src[r * 4 + c];
}

static void mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r*4+0]*b[0*4+c] + a[r*4+1]*b[1*4+c] +
                             a[r*4+2]*b[2*4+c] + a[r*4+3]*b[3*4+c];
}

//=============================================================================
// Vertex descriptor (80-byte vgfx3d_vertex_t)
//=============================================================================

static MTLVertexDescriptor *create_vertex_descriptor(void) {
    MTLVertexDescriptor *d = [[MTLVertexDescriptor alloc] init];
    d.attributes[0].format = MTLVertexFormatFloat3;  d.attributes[0].offset = 0;  d.attributes[0].bufferIndex = 0;
    d.attributes[1].format = MTLVertexFormatFloat3;  d.attributes[1].offset = 12; d.attributes[1].bufferIndex = 0;
    d.attributes[2].format = MTLVertexFormatFloat2;  d.attributes[2].offset = 24; d.attributes[2].bufferIndex = 0;
    d.attributes[3].format = MTLVertexFormatFloat4;  d.attributes[3].offset = 32; d.attributes[3].bufferIndex = 0;
    d.attributes[4].format = MTLVertexFormatFloat3;  d.attributes[4].offset = 48; d.attributes[4].bufferIndex = 0;
    d.attributes[5].format = MTLVertexFormatUChar4;  d.attributes[5].offset = 60; d.attributes[5].bufferIndex = 0;
    d.attributes[6].format = MTLVertexFormatFloat4;  d.attributes[6].offset = 64; d.attributes[6].bufferIndex = 0;
    d.layouts[0].stride = 80;
    d.layouts[0].stepRate = 1;
    d.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return d;
}

//=============================================================================
// Backend vtable
//=============================================================================

static void *metal_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return NULL;

        NSView *view = (__bridge NSView *)vgfx_get_native_view(win);
        if (!view) return NULL;

        VGFXMetalContext *ctx = [[VGFXMetalContext alloc] init];
        ctx.device = device;
        ctx.width = w;
        ctx.height = h;

        view.wantsLayer = YES;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake((CGFloat)w, (CGFloat)h);
        layer.frame = view.bounds;
        layer.opaque = NO; /* Allow software renderer to show through when Metal isn't active */
        /* Add as sublayer on top of existing content (don't replace view.layer) */
        [view.layer addSublayer:layer];
        ctx.metalLayer = layer;

        ctx.commandQueue = [device newCommandQueue];
        if (!ctx.commandQueue) return NULL;

        NSError *error = nil;
        ctx.library = [device newLibraryWithSource:metal_shader_source options:nil error:&error];
        if (!ctx.library) {
            NSLog(@"[Metal] Shader error: %@", error);
            return NULL;
        }
        /* Shaders compiled successfully */

        id<MTLFunction> vf = [ctx.library newFunctionWithName:@"vertex_main"];
        id<MTLFunction> ff = [ctx.library newFunctionWithName:@"fragment_main"];
        if (!vf || !ff) return NULL;

        MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = vf;
        pd.fragmentFunction = ff;
        pd.vertexDescriptor = create_vertex_descriptor();
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        /* Enable alpha blending: src*srcAlpha + dst*(1-srcAlpha) */
        pd.colorAttachments[0].blendingEnabled = YES;
        pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        ctx.pipelineState = [device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!ctx.pipelineState) {
            NSLog(@"Metal pipeline error: %@", error);
            return NULL;
        }

        MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
        dd.depthCompareFunction = MTLCompareFunctionLess;
        dd.depthWriteEnabled = YES;
        ctx.depthState = [device newDepthStencilStateWithDescriptor:dd];
        /* Depth state for transparent draws: test ON, write OFF */
        dd.depthWriteEnabled = NO;
        ctx.depthStateNoWrite = [device newDepthStencilStateWithDescriptor:dd];

        /* Depth texture */
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
            width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        ctx.depthTexture = [device newTextureWithDescriptor:td];

        /* Backend initialized successfully */
        return (__bridge_retained void *)ctx;
    }
}

static void metal_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr) return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge_transfer VGFXMetalContext *)ctx_ptr;
        (void)ctx; /* ARC releases all properties */
    }
}

static void metal_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    ctx.clearR = r; ctx.clearG = g; ctx.clearB = b;
}

static void metal_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx) return;

        float vp[16];
        mat4f_mul(cam->projection, cam->view, vp);
        memcpy(ctx->_vp, vp, sizeof(float) * 16);
        memcpy(ctx->_camPos, cam->position, sizeof(float) * 3);

        ctx.drawable = [ctx.metalLayer nextDrawable];
        if (!ctx.drawable) return;

        ctx.cmdBuf = [ctx.commandQueue commandBuffer];
        if (!ctx.cmdBuf) return;

        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = ctx.drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(ctx.clearR, ctx.clearG, ctx.clearB, 1.0);
        rp.depthAttachment.texture = ctx.depthTexture;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.depthAttachment.clearDepth = 1.0;

        ctx.encoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
        [ctx.encoder setRenderPipelineState:ctx.pipelineState];
        [ctx.encoder setDepthStencilState:ctx.depthState];
        /* Metal tests winding in screen space (Y-down after viewport transform).
         * Our projection produces OpenGL NDC (Y-up), so CCW in clip space = CW
         * in Metal screen space. MTLWindingClockwise (default) is correct. */
        [ctx.encoder setFrontFacingWinding:MTLWindingClockwise];
        [ctx.encoder setCullMode:MTLCullModeBack];
        ctx.inFrame = YES;
    }
}

static void metal_submit_draw(void *ctx_ptr, vgfx_window_t win,
                               const vgfx3d_draw_cmd_t *cmd,
                               const vgfx3d_light_params_t *lights, int32_t light_count,
                               const float *ambient, int8_t wireframe, int8_t backface_cull) {
    @autoreleasepool {
        (void)win; (void)wireframe;
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder || !ctx.inFrame) return;

        [ctx.encoder setCullMode:backface_cull ? MTLCullModeBack : MTLCullModeNone];

        /* Switch depth state: no Z-write for transparent draws */
        if (cmd->alpha < 1.0f)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];

        id<MTLBuffer> vb = [ctx.device newBufferWithBytes:cmd->vertices
            length:cmd->vertex_count * sizeof(vgfx3d_vertex_t)
            options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib = [ctx.device newBufferWithBytes:cmd->indices
            length:cmd->index_count * sizeof(uint32_t)
            options:MTLResourceStorageModeShared];
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];

        /* Per-object uniforms */
        mtl_per_object_t obj;
        transpose4x4(cmd->model_matrix, obj.m);
        float vp_t[16];
        transpose4x4(ctx->_vp, vp_t);
        memcpy(obj.vp, vp_t, sizeof(float) * 16);
        memcpy(obj.nm, obj.m, sizeof(float) * 16); /* normal matrix = model (transposed) */
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];

        /* Per-scene */
        mtl_per_scene_t scene;
        memset(&scene, 0, sizeof(scene));
        memcpy(scene.cp, ctx->_camPos, sizeof(float) * 3);
        scene.ac[0] = ambient[0]; scene.ac[1] = ambient[1]; scene.ac[2] = ambient[2];
        scene.lc = light_count;
        [ctx.encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:0];

        /* Per-material */
        mtl_per_material_t mat;
        memset(&mat, 0, sizeof(mat));
        memcpy(mat.dc, cmd->diffuse_color, sizeof(float) * 4);
        mat.sc[0] = cmd->specular[0]; mat.sc[1] = cmd->specular[1]; mat.sc[2] = cmd->specular[2];
        mat.sc[3] = cmd->shininess;
        mat.ec[0] = cmd->emissive_color[0]; mat.ec[1] = cmd->emissive_color[1]; mat.ec[2] = cmd->emissive_color[2]; mat.ec[3] = 0;
        mat.alpha = cmd->alpha;
        mat.ht = cmd->texture ? 1 : 0;
        mat.unlit = cmd->unlit;
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        /* Lights — always set buffer 2, even if empty (prevents validation warnings) */
        {
            mtl_light_t ml[8];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < 8; i++) {
                ml[i].type = lights[i].type;
                ml[i].dir[0] = lights[i].direction[0]; ml[i].dir[1] = lights[i].direction[1]; ml[i].dir[2] = lights[i].direction[2];
                ml[i].pos[0] = lights[i].position[0]; ml[i].pos[1] = lights[i].position[1]; ml[i].pos[2] = lights[i].position[2];
                ml[i].col[0] = lights[i].color[0]; ml[i].col[1] = lights[i].color[1]; ml[i].col[2] = lights[i].color[2];
                ml[i].intensity = lights[i].intensity;
                ml[i].attenuation = lights[i].attenuation;
            }
            int32_t buf_count = light_count > 0 ? light_count : 1;
            [ctx.encoder setFragmentBytes:ml length:sizeof(mtl_light_t) * buf_count atIndex:2];
        }

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
            indexCount:cmd->index_count indexType:MTLIndexTypeUInt32
            indexBuffer:ib indexBufferOffset:0];
    }
}

static void metal_end_frame(void *ctx_ptr) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder || !ctx.inFrame) return;

        [ctx.encoder endEncoding];
        if (ctx.drawable)
            [ctx.cmdBuf presentDrawable:ctx.drawable];
        [ctx.cmdBuf commit];
        ctx.encoder = nil;
        ctx.cmdBuf = nil;
        ctx.drawable = nil;
        ctx.inFrame = NO;
    }
}

//=============================================================================
// Exported backend
//=============================================================================

static void metal_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt)
{
    (void)ctx_ptr; (void)rt;
    /* TODO: Metal render-to-texture via MTLTexture render target */
}

/* Called from rt_rendertarget3d.c to hide the Metal layer during software RTT */
void vgfx3d_hide_gpu_layer(void *backend_ctx)
{
    if (!backend_ctx) return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer)
            ctx.metalLayer.hidden = YES;
    }
}

void vgfx3d_show_gpu_layer(void *backend_ctx)
{
    if (!backend_ctx) return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer)
            ctx.metalLayer.hidden = NO;
    }
}

const vgfx3d_backend_t vgfx3d_metal_backend = {
    .name = "metal",
    .create_ctx = metal_create_ctx,
    .destroy_ctx = metal_destroy_ctx,
    .clear = metal_clear,
    .begin_frame = metal_begin_frame,
    .submit_draw = metal_submit_draw,
    .end_frame = metal_end_frame,
    .set_render_target = metal_set_render_target,
};

#endif /* __APPLE__ && VIPER_ENABLE_GRAPHICS */
