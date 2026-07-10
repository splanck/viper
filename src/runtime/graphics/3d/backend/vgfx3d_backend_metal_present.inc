//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_metal_present.inc
//   (#included from vgfx3d_backend_metal.m; not a standalone translation unit)
// Purpose: Layer sizing, encoder/commit lifecycle, transient buffer ring, depth probes, drawable presentation, shared/transient buffers, and geometry caching
//===----------------------------------------------------------------------===//

/// @brief Sync the CAMetalLayer's drawable size and scale to the current backing view.
static void metal_update_layer_size(VGFXMetalContext *ctx) {
    if (!ctx)
        return;
    if (ctx.metalLayer) {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(ctx.vgfxWin);
        if (view) {
            ctx.metalLayer.frame = view.bounds;
            ctx.metalLayer.drawableSize = CGSizeMake((CGFloat)(ctx.width > 0 ? ctx.width : 1),
                                                     (CGFloat)(ctx.height > 0 ? ctx.height : 1));
        }
    }
}

/// @brief End the active scene render-command encoder if one is open.
static void metal_finish_encoding(VGFXMetalContext *ctx) {
    if (!ctx)
        return;
    if (ctx.encoder) {
        [ctx.encoder endEncoding];
        ctx.encoder = nil;
    }
}

/* Transient-buffer ring depth and the frames-in-flight cap. A single MTLCommandQueue
 * completes command buffers in FIFO order, so capping unfinished buffers at RING-1
 * guarantees the previous user of a reused ring slot has already completed. */
#define VGFX3D_METAL_TRANSIENT_RING 4u
#define VGFX3D_METAL_MAX_INFLIGHT 3

/// @brief Fetch the MTLBuffer stored in a transient pool slot (NSNull → nil).
static id<MTLBuffer> metal_transient_pool_buffer(NSMutableArray *pool, NSUInteger slot) {
    if (!pool || slot >= pool.count)
        return nil;
    id obj = pool[slot];
    return obj == (id)[NSNull null] ? nil : (id<MTLBuffer>)obj;
}

/// @brief Lazily create a transient pool with one NSNull placeholder per ring slot.
static NSMutableArray *metal_transient_pool_ensure(NSMutableArray *pool) {
    if (pool)
        return pool;
    NSMutableArray *created = [NSMutableArray arrayWithCapacity:VGFX3D_METAL_TRANSIENT_RING];
    for (NSUInteger i = 0; i < VGFX3D_METAL_TRANSIENT_RING; i++)
        [created addObject:[NSNull null]];
    return created;
}

/// @brief Rotate to the next transient ring slot and reset its write offsets.
/// @details Called whenever a new context command buffer is created; the frames-in-flight
///          semaphore guarantees the slot's previous user has finished on the GPU.
static void metal_rotate_transient_ring(VGFXMetalContext *ctx) {
    NSUInteger slot;
    if (!ctx)
        return;
    ctx.dynamicVertexPool = metal_transient_pool_ensure(ctx.dynamicVertexPool);
    ctx.dynamicIndexPool = metal_transient_pool_ensure(ctx.dynamicIndexPool);
    ctx.instancePool = metal_transient_pool_ensure(ctx.instancePool);
    slot = ctx.transientRingCursor % VGFX3D_METAL_TRANSIENT_RING;
    ctx.transientRingCursor = ctx.transientRingCursor + 1u;
    ctx.transientRingSlot = slot;
    ctx.dynamicVertexBuf = metal_transient_pool_buffer(ctx.dynamicVertexPool, slot);
    ctx.dynamicIndexBuf = metal_transient_pool_buffer(ctx.dynamicIndexPool, slot);
    ctx.instanceBuf = metal_transient_pool_buffer(ctx.instancePool, slot);
    ctx.dynamicVertexBufLength = ctx.dynamicVertexBuf ? ctx.dynamicVertexBuf.length : 0;
    ctx.dynamicIndexBufLength = ctx.dynamicIndexBuf ? ctx.dynamicIndexBuf.length : 0;
    ctx.instanceBufLength = ctx.instanceBuf ? ctx.instanceBuf.length : 0;
    ctx.dynamicVertexWriteOffset = 0;
    ctx.dynamicIndexWriteOffset = 0;
    ctx.instanceWriteOffset = 0;
}

/// @brief Create a context command buffer, throttled to the frames-in-flight budget.
/// @details Waits on the in-flight semaphore (signalled by each committed buffer's completion)
///          so the CPU can never run more than VGFX3D_METAL_MAX_INFLIGHT command buffers ahead
///          of the GPU, then rotates the transient ring for the new buffer's dynamic writes.
static id<MTLCommandBuffer> metal_acquire_command_buffer(VGFXMetalContext *ctx) {
    id<MTLCommandBuffer> buf;
    if (!ctx || !ctx.commandQueue)
        return nil;
    if (ctx.inflightSemaphore)
        dispatch_semaphore_wait(ctx.inflightSemaphore, DISPATCH_TIME_FOREVER);
    buf = [ctx.commandQueue commandBuffer];
    if (!buf) {
        if (ctx.inflightSemaphore)
            dispatch_semaphore_signal(ctx.inflightSemaphore);
        return nil;
    }
    metal_rotate_transient_ring(ctx);
    return buf;
}

/// @brief Commit the pending command buffer, optionally blocking until the GPU finishes it.
/// @brief Encode this frame's queued scene-depth probe blits into the command buffer.
/// @details Copies one texel per probe from the scene depth texture into the shared
///   probe buffer; the completed handler publishes the results. Returns the number of
///   probes encoded (0 when there is nothing to do or no depth texture exists).
static int32_t metal_encode_depth_probes(VGFXMetalContext *ctx, id<MTLCommandBuffer> cmdBuf) {
    id<MTLTexture> depth;
    id<MTLBlitCommandEncoder> blit;
    int32_t encoded;

    if (!ctx || !cmdBuf || ctx->_depthProbeRequestCount <= 0)
        return 0;
    depth = ctx.depthTexture;
    if (!depth || depth.width == 0 || depth.height == 0) {
        ctx->_depthProbeRequestCount = 0;
        return 0;
    }
    if (!ctx.depthProbeBuffer) {
        ctx.depthProbeBuffer =
            [ctx.device newBufferWithLength:VGFX3D_DEPTH_PROBE_MAX * sizeof(float)
                                    options:MTLResourceStorageModeShared];
        if (!ctx.depthProbeBuffer) {
            ctx->_depthProbeRequestCount = 0;
            return 0;
        }
    }
    blit = [cmdBuf blitCommandEncoder];
    if (!blit) {
        ctx->_depthProbeRequestCount = 0;
        return 0;
    }
    encoded = ctx->_depthProbeRequestCount;
    for (int32_t i = 0; i < encoded; i++) {
        float ndc_x = ctx->_depthProbeRequests[i][0];
        float ndc_y = ctx->_depthProbeRequests[i][1];
        NSUInteger px = 0;
        NSUInteger py = 0;
        float fx = (ndc_x * 0.5f + 0.5f) * (float)depth.width;
        float fy = (0.5f - ndc_y * 0.5f) * (float)depth.height;
        if (fx > 0.0f)
            px = (NSUInteger)fx;
        if (fy > 0.0f)
            py = (NSUInteger)fy;
        if (px >= depth.width)
            px = depth.width - 1;
        if (py >= depth.height)
            py = depth.height - 1;
        [blit copyFromTexture:depth
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(px, py, 0)
                      sourceSize:MTLSizeMake(1, 1, 1)
                        toBuffer:ctx.depthProbeBuffer
               destinationOffset:(NSUInteger)i * sizeof(float)
          destinationBytesPerRow:sizeof(float)
        destinationBytesPerImage:sizeof(float)];
    }
    [blit endEncoding];
    ctx->_depthProbeRequestCount = 0;
    return encoded;
}

/// @brief Publish completed depth-probe values from the shared buffer into the context.
/// @details The probe hook contract is canonical window depth (larger = farther); the
///   scene depth buffer is reversed-Z, so values are flipped on publish.
static void metal_publish_depth_probes(VGFXMetalContext *ctx, int32_t count) {
    if (!ctx || count <= 0 || !ctx.depthProbeBuffer)
        return;
    @synchronized(ctx) {
        const float *src = (const float *)ctx.depthProbeBuffer.contents;
        if (!src)
            return;
        if (count > VGFX3D_DEPTH_PROBE_MAX)
            count = VGFX3D_DEPTH_PROBE_MAX;
        for (int32_t i = 0; i < count; i++)
            ctx->_depthProbeResults[i] = 1.0f - src[i];
        ctx->_depthProbeResultCount = count;
    }
}

static void metal_commit_pending(VGFXMetalContext *ctx, BOOL waitUntilCompleted) {
    if (!ctx)
        return;
    metal_finish_encoding(ctx);
    if (ctx.cmdBuf) {
        id<MTLCommandBuffer> cmdBuf = ctx.cmdBuf;
        NSMutableArray *retainedFrameBuffers = ctx.frameBuffers;
        id<CAMetalDrawable> retainedDrawable = ctx.drawable;
        dispatch_semaphore_t inflight = ctx.inflightSemaphore;
        int32_t probe_count = metal_encode_depth_probes(ctx, cmdBuf);
        if (!waitUntilCompleted) {
            VGFXMetalContext *probeCtx = probe_count > 0 ? ctx : nil;
            [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
              (void)buffer;
              (void)retainedFrameBuffers;
              (void)retainedDrawable;
              if (probeCtx)
                  metal_publish_depth_probes(probeCtx, probe_count);
              if (inflight)
                  dispatch_semaphore_signal(inflight);
            }];
        }
        [cmdBuf commit];
        if (waitUntilCompleted) {
            [cmdBuf waitUntilCompleted];
            metal_publish_depth_probes(probe_count > 0 ? ctx : nil, probe_count);
            if (inflight)
                dispatch_semaphore_signal(inflight);
        }
        ctx.cmdBuf = nil;
        ctx.frameBuffers = nil;
        ctx.drawable = nil;
        ctx.postfxCompositedToDrawable = 0;
    }
}

/// @brief Detach the active offscreen render target, optionally syncing its color back to Pixels
/// first.
static void metal_detach_render_target(VGFXMetalContext *ctx, BOOL syncColor) {
    vgfx3d_rendertarget_t *target;
    BOOL hadPendingFrame;
    if (!ctx)
        return;
    target = ctx.rttTarget;
    hadPendingFrame = ctx.inFrame || ctx.encoder || ctx.cmdBuf;
    if (hadPendingFrame) {
        metal_commit_pending(ctx, syncColor);
        ctx.inFrame = NO;
        if (target && syncColor)
            target->color_dirty = 1;
    }
    if (target) {
        int sync_ok = 1;
        if (syncColor && target->color_dirty)
            sync_ok = metal_sync_render_target_color((__bridge void *)ctx, target);
        if (sync_ok || !target->color_dirty) {
            vgfx3d_rendertarget_clear_sync(target);
        } else {
            target->sync_color = metal_sync_render_target_color;
            target->sync_color_userdata = (__bridge void *)ctx;
        }
    }
    ctx.rttActive = NO;
    ctx.rttColorTexture = nil;
    ctx.rttMotionTexture = nil;
    ctx.rttDepthTexture = nil;
    ctx.rttTarget = NULL;
    ctx.displayTexture = nil;
    ctx.postfxEncodedThisFrame = 0;
    ctx.postfxCompositedToDrawable = 0;
}

/// @brief Blit a finished texture into the window's CPU framebuffer (software-present fallback
/// path).
static void metal_present_texture_to_framebuffer(VGFXMetalContext *ctx, id<MTLTexture> texture) {
    vgfx_framebuffer_t fb;
    if (!ctx || !texture || !ctx.vgfxWin)
        return;
    if (!vgfx_get_framebuffer(ctx.vgfxWin, &fb))
        return;
    (void)metal_copy_texture_to_rgba(ctx, texture, fb.pixels, fb.width, fb.height, fb.stride, NULL);
    ctx.displayTexture = texture;
}

/// @brief Present a finished texture to the layer's next drawable (or framebuffer fallback).
/// @return YES if a drawable was presented, NO if it fell back / had no drawable.
static BOOL metal_present_texture(VGFXMetalContext *ctx,
                                  id<MTLTexture> texture,
                                  BOOL schedulePresent) {
    NSUInteger copy_w;
    NSUInteger copy_h;
    id<CAMetalDrawable> drawable;
    id<MTLBlitCommandEncoder> blit;

    if (!ctx || !texture || ctx.rttActive)
        return NO;
    if (!ctx.cmdBuf) {
        ctx.cmdBuf = metal_acquire_command_buffer(ctx);
        if (!ctx.cmdBuf)
            return NO;
        if (!ctx.frameBuffers)
            ctx.frameBuffers = [NSMutableArray arrayWithCapacity:4];
    }
    metal_update_layer_size(ctx);
    if (!ctx.metalLayer)
        return NO;
    drawable = [ctx.metalLayer nextDrawable];
    if (!drawable)
        return NO;

    copy_w = texture.width < drawable.texture.width ? texture.width : drawable.texture.width;
    copy_h = texture.height < drawable.texture.height ? texture.height : drawable.texture.height;
    if (copy_w == 0 || copy_h == 0)
        return NO;

    ctx.drawable = drawable;
    if (copy_w < drawable.texture.width || copy_h < drawable.texture.height) {
        /* Mid-resize the drawable can outsize the scene texture for a frame; its pool
         * contents outside the blit region are undefined (stale frames or garbage) and
         * would flash along the right/bottom band. Clear the drawable first. */
        MTLRenderPassDescriptor *clear_rp = [MTLRenderPassDescriptor renderPassDescriptor];
        clear_rp.colorAttachments[0].texture = drawable.texture;
        clear_rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        clear_rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        clear_rp.colorAttachments[0].clearColor =
            MTLClearColorMake(ctx.clearR, ctx.clearG, ctx.clearB, 1.0);
        id<MTLRenderCommandEncoder> clear_encoder =
            [ctx.cmdBuf renderCommandEncoderWithDescriptor:clear_rp];
        if (clear_encoder)
            [clear_encoder endEncoding];
    }
    blit = [ctx.cmdBuf blitCommandEncoder];
    if (!blit)
        return NO;
    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(copy_w, copy_h, 1)
                toTexture:drawable.texture
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    if (schedulePresent)
        [ctx.cmdBuf presentDrawable:drawable];
    ctx.displayTexture = texture;
    return YES;
}

/// @brief Allocate a shared-storage MTLBuffer initialized from @p bytes (CPU/GPU-visible).
static id<MTLBuffer> metal_new_shared_buffer(VGFXMetalContext *ctx,
                                             const void *bytes,
                                             size_t length) {
    if (!ctx || !bytes || length == 0)
        return nil;
    return [ctx.device newBufferWithBytes:bytes length:length options:MTLResourceStorageModeShared];
}

/// @brief Allocate an uninitialized shared-storage MTLBuffer of @p length bytes.
static id<MTLBuffer> metal_new_shared_buffer_with_length(VGFXMetalContext *ctx, size_t length) {
    if (!ctx || !ctx.device || length == 0)
        return nil;
    return [ctx.device newBufferWithLength:length options:MTLResourceStorageModeShared];
}

/// @brief Align a Metal dynamic-buffer write offset to a conservative boundary.
/// @details Vertex buffer offsets are generally flexible, while index buffer offsets must at least
///          satisfy the index element size. A 256-byte alignment keeps both paths conservative and
///          avoids backend-specific validation edge cases.
static NSUInteger metal_align_dynamic_offset(NSUInteger value) {
    const NSUInteger align = 256u;
    if (value > ((NSUInteger)-1) - (align - 1u))
        return (NSUInteger)-1;
    return (value + align - 1u) & ~(align - 1u);
}

/* Transient upload streams: which ring-slot buffer a per-draw slice appends into. */
typedef enum {
    VGFX3D_METAL_TRANSIENT_VERTEX = 0,
    VGFX3D_METAL_TRANSIENT_INDEX = 1,
    VGFX3D_METAL_TRANSIENT_INSTANCE = 2,
} vgfx3d_metal_transient_stream_t;

/// @brief Upload transient per-draw bytes into a reusable shared-storage Metal buffer slice.
/// @details Non-cacheable meshes are produced every frame by skinning/morphing/snapshotting,
///          instanced draws upload a matrix block per batch, and skinned draws a bone palette.
///          Allocating a fresh MTLBuffer for each would churn; overwriting one shared buffer
///          would corrupt earlier encoded draws. This helper appends into the current
///          transient-ring slot at a unique offset (the ring itself protects against
///          frames-in-flight reuse) and grows the slot's buffer on demand, publishing growth
///          back into the pool so the slot keeps its high-water capacity.
static id<MTLBuffer> metal_upload_transient_buffer(VGFXMetalContext *ctx,
                                                   vgfx3d_metal_transient_stream_t stream,
                                                   const void *bytes,
                                                   size_t length,
                                                   NSUInteger *out_offset) {
    id<MTLBuffer> buffer;
    NSUInteger capacity;
    NSUInteger write_offset;
    NSUInteger aligned_offset;
    NSUInteger new_capacity;
    const NSUInteger max_capacity = (NSUInteger)-1;

    if (out_offset)
        *out_offset = 0;
    if (!ctx || !bytes || length == 0 || length > (size_t)max_capacity)
        return nil;
    switch (stream) {
        case VGFX3D_METAL_TRANSIENT_VERTEX:
            buffer = ctx.dynamicVertexBuf;
            capacity = ctx.dynamicVertexBufLength;
            write_offset = ctx.dynamicVertexWriteOffset;
            break;
        case VGFX3D_METAL_TRANSIENT_INDEX:
            buffer = ctx.dynamicIndexBuf;
            capacity = ctx.dynamicIndexBufLength;
            write_offset = ctx.dynamicIndexWriteOffset;
            break;
        case VGFX3D_METAL_TRANSIENT_INSTANCE:
        default:
            buffer = ctx.instanceBuf;
            capacity = ctx.instanceBufLength;
            write_offset = ctx.instanceWriteOffset;
            break;
    }
    aligned_offset = metal_align_dynamic_offset(write_offset);
    if (aligned_offset > max_capacity - (NSUInteger)length)
        return nil;
    if (!buffer || capacity < aligned_offset + (NSUInteger)length) {
        NSUInteger grown_capacity = capacity > 0 ? capacity : 65536u;
        NSUInteger required = aligned_offset + (NSUInteger)length;
        while (grown_capacity < required) {
            if (grown_capacity > max_capacity / 2u) {
                grown_capacity = required;
                break;
            }
            grown_capacity *= 2u;
        }
        new_capacity = grown_capacity;
        buffer = metal_new_shared_buffer_with_length(ctx, (size_t)new_capacity);
        if (!buffer)
            return nil;
        /* Fresh buffer: restart at offset 0 (previous slices live in the old buffer, which
         * stays retained by the encoder and frameBuffers until the GPU is done with it). */
        aligned_offset = 0;
        switch (stream) {
            case VGFX3D_METAL_TRANSIENT_VERTEX:
                ctx.dynamicVertexBuf = buffer;
                ctx.dynamicVertexBufLength = new_capacity;
                ctx.dynamicVertexPool = metal_transient_pool_ensure(ctx.dynamicVertexPool);
                ctx.dynamicVertexPool[ctx.transientRingSlot] = buffer;
                break;
            case VGFX3D_METAL_TRANSIENT_INDEX:
                ctx.dynamicIndexBuf = buffer;
                ctx.dynamicIndexBufLength = new_capacity;
                ctx.dynamicIndexPool = metal_transient_pool_ensure(ctx.dynamicIndexPool);
                ctx.dynamicIndexPool[ctx.transientRingSlot] = buffer;
                break;
            case VGFX3D_METAL_TRANSIENT_INSTANCE:
            default:
                ctx.instanceBuf = buffer;
                ctx.instanceBufLength = new_capacity;
                ctx.instancePool = metal_transient_pool_ensure(ctx.instancePool);
                ctx.instancePool[ctx.transientRingSlot] = buffer;
                break;
        }
        if (ctx.frameBuffers)
            [ctx.frameBuffers addObject:buffer];
    }
    memcpy((uint8_t *)buffer.contents + aligned_offset, bytes, length);
    switch (stream) {
        case VGFX3D_METAL_TRANSIENT_VERTEX:
            ctx.dynamicVertexWriteOffset = aligned_offset + (NSUInteger)length;
            break;
        case VGFX3D_METAL_TRANSIENT_INDEX:
            ctx.dynamicIndexWriteOffset = aligned_offset + (NSUInteger)length;
            break;
        case VGFX3D_METAL_TRANSIENT_INSTANCE:
        default:
            ctx.instanceWriteOffset = aligned_offset + (NSUInteger)length;
            break;
    }
    if (out_offset)
        *out_offset = aligned_offset;
    return buffer;
}

/// @brief Run the texture/cubemap/morph cache pruning passes once per frame.
static void metal_cache_evict_if_needed(VGFXMetalContext *ctx) {
    NSValue *oldestKey = nil;
    VGFXMetalGeometryCacheEntry *oldestEntry = nil;

    if (!ctx || !ctx.geometryCache || ctx.geometryCache.count < 128)
        return;
    for (NSValue *key in ctx.geometryCache) {
        VGFXMetalGeometryCacheEntry *entry = ctx.geometryCache[key];
        if (!oldestEntry || entry.lastUsedFrame < oldestEntry.lastUsedFrame) {
            oldestEntry = entry;
            oldestKey = key;
        }
    }
    if (oldestKey)
        [ctx.geometryCache removeObjectForKey:oldestKey];
}

/// @brief Get (growing as needed) the per-frame vertex and index buffers for a draw of given size.
/// @return 1 when both output buffers are valid, 0 when allocation/upload failed.
static int metal_get_geometry_buffers(VGFXMetalContext *ctx,
                                      const vgfx3d_draw_cmd_t *cmd,
                                      id<MTLBuffer> *outVB,
                                      id<MTLBuffer> *outIB,
                                      NSUInteger *outVertexOffset,
                                      NSUInteger *outIndexOffset) {
    if (!outVB || !outIB) {
        return 0;
    }
    *outVB = nil;
    *outIB = nil;
    if (outVertexOffset)
        *outVertexOffset = 0;
    if (outIndexOffset)
        *outIndexOffset = 0;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return 0;
    if ((size_t)cmd->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)cmd->index_count > SIZE_MAX / sizeof(uint32_t))
        return 0;

    if (cmd->geometry_key && cmd->geometry_revision != 0) {
        BOOL wants_compact = cmd->compact_vertex_stream ? YES : NO;
        NSValue *key = [NSValue valueWithPointer:cmd->geometry_key];
        VGFXMetalGeometryCacheEntry *entry = ctx.geometryCache[key];
        if (!entry || entry.revision != cmd->geometry_revision ||
            entry.vertexCount != cmd->vertex_count || entry.indexCount != cmd->index_count ||
            entry.compactStream != wants_compact || !entry.vertexBuffer || !entry.indexBuffer) {
            VGFXMetalGeometryCacheEntry *new_entry;
            metal_cache_evict_if_needed(ctx);
            new_entry = [[VGFXMetalGeometryCacheEntry alloc] init];
            if (wants_compact) {
                /* R20: encode into the packed 48-byte layout; the compact
                 * pipeline variants decode it via their vertex descriptor. */
                size_t compact_bytes = (size_t)cmd->vertex_count * VGFX3D_COMPACT_VERTEX_STRIDE;
                uint8_t *packed = (uint8_t *)malloc(compact_bytes);
                if (!packed)
                    return 0;
                vgfx3d_encode_compact_vertices(cmd->vertices, cmd->vertex_count, packed);
                new_entry.vertexBuffer = metal_new_shared_buffer(ctx, packed, compact_bytes);
                free(packed);
            } else {
                new_entry.vertexBuffer = metal_new_shared_buffer(
                    ctx, cmd->vertices, (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t));
            }
            new_entry.indexBuffer = metal_new_shared_buffer(
                ctx, cmd->indices, (size_t)cmd->index_count * sizeof(uint32_t));
            if (!new_entry.vertexBuffer || !new_entry.indexBuffer)
                return 0;
            new_entry.revision = cmd->geometry_revision;
            new_entry.vertexCount = cmd->vertex_count;
            new_entry.indexCount = cmd->index_count;
            new_entry.compactStream = wants_compact;
            ctx.geometryCache[key] = new_entry;
            entry = new_entry;
        }
        entry.lastUsedFrame = ctx.frameSerial;
        *outVB = entry.vertexBuffer;
        *outIB = entry.indexBuffer;
        return (*outVB && *outIB) ? 1 : 0;
    }

    *outVB =
        metal_upload_transient_buffer(ctx,
                                      VGFX3D_METAL_TRANSIENT_VERTEX,
                                      cmd->vertices,
                                      (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t),
                                      outVertexOffset);
    *outIB = metal_upload_transient_buffer(ctx,
                                           VGFX3D_METAL_TRANSIENT_INDEX,
                                           cmd->indices,
                                           (size_t)cmd->index_count * sizeof(uint32_t),
                                           outIndexOffset);
    return (*outVB && *outIB) ? 1 : 0;
}

/// @brief The texture that holds the current frame's final image for readback/present.
/// @details Prefers the post-FX output when post-processing ran, else the main scene color texture.
static id<MTLTexture> metal_active_readback_texture(VGFXMetalContext *ctx) {
    if (!ctx)
        return nil;
    if (ctx.rttActive && ctx.rttColorTexture)
        return ctx.rttColorTexture;
    if (ctx.displayTexture)
        return ctx.displayTexture;
    if (ctx.gpuPostfxEnabled && ctx.postfxEncodedThisFrame && ctx.postfxColorTexture)
        return ctx.postfxColorTexture;
    if (ctx.currentTargetKind == VGFX3D_METAL_TARGET_SWAPCHAIN && ctx.drawable)
        return ctx.drawable.texture;
    return ctx.offscreenColor;
}

/// @brief Reset the cached GPU post-processing chain snapshot to empty (without freeing buffers).
static void metal_reset_gpu_postfx_chain(VGFXMetalContext *ctx) {
    vgfx3d_postfx_chain_t chain;
    if (!ctx)
        return;
    chain = ctx.gpuPostfxChain;
    vgfx3d_postfx_chain_reset(&chain);
    ctx.gpuPostfxChain = chain;
    ctx.gpuPostfxChainValid = 0;
}

/// @brief Free the cached GPU post-processing chain snapshot and release its owned memory.
static void metal_free_gpu_postfx_chain(VGFXMetalContext *ctx) {
    vgfx3d_postfx_chain_t chain;
    if (!ctx)
        return;
    chain = ctx.gpuPostfxChain;
    vgfx3d_postfx_chain_free(&chain);
    ctx.gpuPostfxChain = chain;
    ctx.gpuPostfxChainValid = 0;
}

/// @brief Deep-copy a post-FX chain snapshot into the context for GPU post-processing.
/// @details Owns the copy so the chain can be applied across frames independent of the caller's
/// data.
/// @return 1 on success, 0 on allocation failure.
static int metal_copy_gpu_postfx_chain(VGFXMetalContext *ctx, const vgfx3d_postfx_chain_t *src) {
    vgfx3d_postfx_chain_t chain;
    int ok;
    if (!ctx)
        return 0;
    chain = ctx.gpuPostfxChain;
    ok = vgfx3d_postfx_chain_copy(&chain, src);
    ctx.gpuPostfxChain = chain;
    ctx.gpuPostfxChainValid = ok ? 1 : 0;
    return ok;
}

//=============================================================================
// Vertex descriptor (92-byte vgfx3d_vertex_t)
//=============================================================================

