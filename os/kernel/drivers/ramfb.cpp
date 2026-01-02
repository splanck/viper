#include "ramfb.hpp"
#include "../console/serial.hpp"
#include "../include/constants.hpp"
#include "fwcfg.hpp"

/**
 * @file ramfb.cpp
 * @brief QEMU RAM framebuffer configuration and drawing primitives.
 *
 * @details
 * Configures the QEMU `ramfb` device by writing a `RAMFBCfg` structure via the
 * fw_cfg DMA interface. The framebuffer memory is placed at a fixed address in
 * guest RAM and is cleared before enabling the device.
 *
 * The module provides minimal drawing helpers used by the graphics console.
 */
namespace ramfb
{

namespace
{
// RAMFBCfg structure for configuring the framebuffer
// All fields are big-endian
struct RAMFBCfg
{
    u64 addr;   // Physical address of framebuffer
    u32 fourcc; // Pixel format FourCC code
    u32 flags;  // Flags (must be 0)
    u32 width;  // Width in pixels
    u32 height; // Height in pixels
    u32 stride; // Bytes per line
} __attribute__((packed));

// DRM FourCC codes for pixel formats (from constants.hpp)
constexpr u32 DRM_FORMAT_XRGB8888 = kc::magic::DRM_FORMAT_XRGB8888;

// Byte swap helpers (ramfb uses big-endian)
/**
 * @brief Convert a 32-bit value from host order to big-endian.
 */
static inline u32 cpu_to_be32(u32 v)
{
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) |
           ((v << 24) & 0xFF000000);
}

/**
 * @brief Convert a 64-bit value from host order to big-endian.
 */
static inline u64 cpu_to_be64(u64 v)
{
    return (static_cast<u64>(cpu_to_be32(v & 0xFFFFFFFF)) << 32) | cpu_to_be32(v >> 32);
}

// Framebuffer memory (from constants.hpp)
constexpr uintptr FB_BASE = kc::mem::FB_BASE;
constexpr u32 FB_MAX_SIZE = kc::mem::FB_SIZE;

// Framebuffer state
FramebufferInfo fb_info = {0, 0, 0, 0, 0};
u32 *fb_ptr = nullptr;
bool initialized = false;
} // namespace

/** @copydoc ramfb::init */
bool init(u32 width, u32 height)
{
    serial::puts("[ramfb] Initializing framebuffer...\n");

    // Find ramfb configuration file
    u16 selector = 0;
    u32 size = fwcfg::find_file("etc/ramfb", &selector);

    if (size == 0)
    {
        serial::puts("[ramfb] Error: etc/ramfb not found in fw_cfg\n");
        serial::puts("[ramfb] Make sure QEMU is started with -device ramfb\n");
        return false;
    }

    serial::puts("[ramfb] Found etc/ramfb, selector=");
    serial::put_hex(selector);
    serial::puts("\n");

    // Calculate framebuffer parameters
    u32 bpp = 32; // 32 bits per pixel (XRGB8888)
    u32 stride = width * (bpp / 8);
    u32 fb_size = stride * height;

    if (fb_size > FB_MAX_SIZE)
    {
        serial::puts("[ramfb] Error: Requested resolution too large\n");
        return false;
    }

    // Set up framebuffer info
    fb_info.address = FB_BASE;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.pitch = stride;
    fb_info.bpp = bpp;
    fb_ptr = reinterpret_cast<u32 *>(FB_BASE);

    // Clear framebuffer memory first
    for (u32 i = 0; i < (fb_size / 4); i++)
    {
        fb_ptr[i] = 0;
    }

    // Configure ramfb
    RAMFBCfg cfg;
    cfg.addr = cpu_to_be64(FB_BASE);
    cfg.fourcc = cpu_to_be32(DRM_FORMAT_XRGB8888);
    cfg.flags = 0;
    cfg.width = cpu_to_be32(width);
    cfg.height = cpu_to_be32(height);
    cfg.stride = cpu_to_be32(stride);

    serial::puts("[ramfb] Writing config via DMA: addr=");
    serial::put_hex(FB_BASE);
    serial::puts(" fourcc=");
    serial::put_hex(DRM_FORMAT_XRGB8888);
    serial::puts(" size=");
    serial::put_dec(sizeof(cfg));
    serial::puts(" bytes\n");

    // Write configuration using DMA (required for ramfb)
    fwcfg::dma_write(selector, &cfg, sizeof(cfg));

    serial::puts("[ramfb] Config written via DMA\n");

    serial::puts("[ramfb] Framebuffer configured: ");
    serial::put_dec(width);
    serial::puts("x");
    serial::put_dec(height);
    serial::puts(" at ");
    serial::put_hex(FB_BASE);
    serial::puts("\n");

    initialized = true;
    return true;
}

/** @copydoc ramfb::init_external */
bool init_external(u64 address, u32 width, u32 height, u32 pitch, u32 bpp)
{
    serial::puts("[ramfb] Using external framebuffer...\n");

    // Validate parameters
    if (address == 0 || width == 0 || height == 0)
    {
        serial::puts("[ramfb] Error: Invalid framebuffer parameters\n");
        return false;
    }

    // Set up framebuffer info
    fb_info.address = address;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.pitch = pitch;
    fb_info.bpp = bpp;
    fb_ptr = reinterpret_cast<u32 *>(address);

    serial::puts("[ramfb] External framebuffer: ");
    serial::put_dec(width);
    serial::puts("x");
    serial::put_dec(height);
    serial::puts(" at ");
    serial::put_hex(address);
    serial::puts("\n");

    initialized = true;
    return true;
}

/** @copydoc ramfb::get_info */
const FramebufferInfo &get_info()
{
    return fb_info;
}

/** @copydoc ramfb::get_framebuffer */
u32 *get_framebuffer()
{
    return fb_ptr;
}

/** @copydoc ramfb::put_pixel */
void put_pixel(u32 x, u32 y, u32 color)
{
    if (!initialized || x >= fb_info.width || y >= fb_info.height)
    {
        return;
    }
    u32 offset = y * (fb_info.pitch / 4) + x;
    fb_ptr[offset] = color;
}

/** @copydoc ramfb::fill_rect */
void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    if (!initialized)
        return;

    // Clamp to screen bounds
    if (x >= fb_info.width || y >= fb_info.height)
        return;
    if (x + w > fb_info.width)
        w = fb_info.width - x;
    if (y + h > fb_info.height)
        h = fb_info.height - y;

    u32 stride = fb_info.pitch / 4;
    for (u32 dy = 0; dy < h; dy++)
    {
        u32 *row = fb_ptr + (y + dy) * stride + x;
        for (u32 dx = 0; dx < w; dx++)
        {
            row[dx] = color;
        }
    }
}

/** @copydoc ramfb::clear */
void clear(u32 color)
{
    if (!initialized)
        return;
    fill_rect(0, 0, fb_info.width, fb_info.height, color);
}

} // namespace ramfb
