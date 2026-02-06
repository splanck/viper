//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/gui.cpp
// Purpose: GUI/Display syscall handlers (0x110-0x11F).
//
//===----------------------------------------------------------------------===//

#include "../../cap/handle.hpp"
#include "../../console/gcon.hpp"
#include "../../drivers/ramfb.hpp"
#include "../../drivers/virtio/gpu.hpp"
#include "../../input/input.hpp"
#include "../../mm/pmm.hpp"
#include "../../viper/address_space.hpp"
#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

namespace syscall {

/// @brief Copy the current mouse state (position and buttons) to user memory.
SyscallResult sys_get_mouse_state(u64 a0, u64, u64, u64, u64, u64) {
    input::MouseState *out = reinterpret_cast<input::MouseState *>(a0);

    VALIDATE_USER_WRITE(out, sizeof(input::MouseState));

    *out = input::get_mouse_state();
    return SyscallResult::ok();
}

/// @brief Map the physical framebuffer into the calling process's address space.
/// @details Scans the 0x6000000000-0x7000000000 virtual range for a free slot,
///   then maps the ramfb physical memory with RW permissions. Returns the
///   virtual address and packed framebuffer info (width, height, bpp, pitch).
///   Requires a Device capability for non-privileged processes (id > 10).
SyscallResult sys_map_framebuffer(u64, u64, u64, u64, u64, u64) {
    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }

    // Security check: only allow framebuffer mapping for privileged processes
    if (v->id > 10) {
        cap::Table *ct = v->cap_table;
        bool has_device_cap = false;
        if (ct) {
            for (usize i = 0; i < ct->capacity(); i++) {
                cap::Entry *e = ct->entry_at(i);
                if (e && e->kind == cap::Kind::Device) {
                    has_device_cap = true;
                    break;
                }
            }
        }
        if (!has_device_cap) {
            return err_permission();
        }
    }

    const ramfb::FramebufferInfo &fb = ramfb::get_info();
    if (fb.address == 0 || fb.width == 0 || fb.height == 0) {
        return err_not_found();
    }

    u64 fb_size = static_cast<u64>(fb.pitch) * fb.height;
    fb_size = (fb_size + 0xFFF) & ~0xFFFULL;

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as) {
        return err_not_found();
    }

    u64 virt_base = 0x6000000000ULL;
    u64 user_virt = 0;

    for (u64 try_addr = virt_base; try_addr < 0x7000000000ULL; try_addr += fb_size) {
        if (as->translate(try_addr) == 0) {
            user_virt = try_addr;
            break;
        }
    }

    if (user_virt == 0) {
        return err_out_of_memory();
    }

    u64 phys_addr = fb.address;
    if (!as->map(user_virt, phys_addr, fb_size, viper::prot::RW)) {
        return err_out_of_memory();
    }

    SyscallResult result;
    result.verr = 0;
    result.res0 = user_virt;
    result.res1 = (static_cast<u64>(fb.height) << 16) | fb.width;
    result.res2 = (static_cast<u64>(fb.bpp) << 32) | fb.pitch;
    return result;
}

/// @brief Set the mouse coordinate clamping bounds (max width and height).
SyscallResult sys_set_mouse_bounds(u64 a0, u64 a1, u64, u64, u64, u64) {
    u32 width = static_cast<u32>(a0);
    u32 height = static_cast<u32>(a1);

    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return err_invalid_arg();
    }

    input::set_mouse_bounds(width, height);
    return SyscallResult::ok();
}

/// @brief Check whether an input event is available (non-blocking poll).
SyscallResult sys_input_has_event(u64, u64, u64, u64, u64, u64) {
    bool has = input::has_event();
    return SyscallResult::ok(has ? 1ULL : 0ULL);
}

/// @brief Dequeue the next input event into a user-supplied buffer.
/// @details Returns VERR_WOULD_BLOCK if the event queue is empty.
SyscallResult sys_input_get_event(u64 a0, u64, u64, u64, u64, u64) {
    input::Event *out = reinterpret_cast<input::Event *>(a0);

    VALIDATE_USER_WRITE(out, sizeof(input::Event));

    input::Event ev;
    if (input::get_event(&ev)) {
        *out = ev;
        return SyscallResult::ok();
    }

    return err_would_block();
}

/// @brief Toggle the graphics console between text and GUI mode.
SyscallResult sys_gcon_set_gui_mode(u64 a0, u64, u64, u64, u64, u64) {
    gcon::set_gui_mode(a0 != 0);
    return SyscallResult::ok();
}

/// @brief Upload a custom hardware cursor image to the VirtIO GPU.
/// @details a1 packs width (high 16 bits) and height (low 16 bits).
///   a2 packs hotspot x (high 16 bits) and y (low 16 bits). Max 64x64 pixels.
SyscallResult sys_set_cursor_image(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    const u32 *pixels = reinterpret_cast<const u32 *>(a0);
    u32 width = static_cast<u32>(a1 >> 16);
    u32 height = static_cast<u32>(a1 & 0xFFFF);
    u32 hot_x = static_cast<u32>(a2 >> 16);
    u32 hot_y = static_cast<u32>(a2 & 0xFFFF);

    if (!pixels || width == 0 || height == 0 || width > 64 || height > 64)
        return err_invalid_arg();

    VALIDATE_USER_READ(pixels, width * height * 4);

    virtio::GpuDevice *gpu = virtio::gpu_device();
    if (!gpu)
        return err_not_found();

    if (!gpu->setup_cursor(pixels, width, height, hot_x, hot_y))
        return err_io();

    return SyscallResult::ok();
}

/// @brief Move the hardware cursor to the specified screen coordinates.
SyscallResult sys_move_cursor(u64 a0, u64 a1, u64, u64, u64, u64) {
    u32 x = static_cast<u32>(a0);
    u32 y = static_cast<u32>(a1);

    virtio::GpuDevice *gpu = virtio::gpu_device();
    if (!gpu || !gpu->has_cursor())
        return err_not_found();

    if (!gpu->move_cursor(x, y))
        return err_io();

    return SyscallResult::ok();
}

/// @brief Return the number of connected displays (currently always 1).
SyscallResult sys_display_count(u64, u64, u64, u64, u64, u64) {
    // Single display only (VirtIO-GPU supports one scanout)
    return SyscallResult::ok(1);
}

} // namespace syscall
