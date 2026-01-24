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

#include "handlers_internal.hpp"
#include "../../cap/handle.hpp"
#include "../../console/gcon.hpp"
#include "../../drivers/ramfb.hpp"
#include "../../input/input.hpp"
#include "../../mm/pmm.hpp"
#include "../../viper/address_space.hpp"
#include "../../viper/viper.hpp"

namespace syscall
{

SyscallResult sys_get_mouse_state(u64 a0, u64, u64, u64, u64, u64)
{
    input::MouseState *out = reinterpret_cast<input::MouseState *>(a0);

    if (!validate_user_write(out, sizeof(input::MouseState)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    *out = input::get_mouse_state();
    return SyscallResult::ok();
}

SyscallResult sys_map_framebuffer(u64, u64, u64, u64, u64, u64)
{
    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Security check: only allow framebuffer mapping for privileged processes
    if (v->id > 10)
    {
        cap::Table *ct = v->cap_table;
        bool has_device_cap = false;
        if (ct)
        {
            for (usize i = 0; i < ct->capacity(); i++)
            {
                cap::Entry *e = ct->entry_at(i);
                if (e && e->kind == cap::Kind::Device)
                {
                    has_device_cap = true;
                    break;
                }
            }
        }
        if (!has_device_cap)
        {
            return SyscallResult::err(error::VERR_PERMISSION);
        }
    }

    const ramfb::FramebufferInfo &fb = ramfb::get_info();
    if (fb.address == 0 || fb.width == 0 || fb.height == 0)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 fb_size = static_cast<u64>(fb.pitch) * fb.height;
    fb_size = (fb_size + 0xFFF) & ~0xFFFULL;

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 virt_base = 0x6000000000ULL;
    u64 user_virt = 0;

    for (u64 try_addr = virt_base; try_addr < 0x7000000000ULL; try_addr += fb_size)
    {
        if (as->translate(try_addr) == 0)
        {
            user_virt = try_addr;
            break;
        }
    }

    if (user_virt == 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    u64 phys_addr = fb.address;
    if (!as->map(user_virt, phys_addr, fb_size, viper::prot::RW))
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    SyscallResult result;
    result.verr = 0;
    result.res0 = user_virt;
    result.res1 = (static_cast<u64>(fb.height) << 16) | fb.width;
    result.res2 = (static_cast<u64>(fb.bpp) << 32) | fb.pitch;
    return result;
}

SyscallResult sys_set_mouse_bounds(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 width = static_cast<u32>(a0);
    u32 height = static_cast<u32>(a1);

    if (width == 0 || height == 0 || width > 8192 || height > 8192)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    input::set_mouse_bounds(width, height);
    return SyscallResult::ok();
}

SyscallResult sys_input_has_event(u64, u64, u64, u64, u64, u64)
{
    bool has = input::has_event();
    return SyscallResult::ok(has ? 1ULL : 0ULL);
}

SyscallResult sys_input_get_event(u64 a0, u64, u64, u64, u64, u64)
{
    input::Event *out = reinterpret_cast<input::Event *>(a0);

    if (!validate_user_write(out, sizeof(input::Event)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    input::Event ev;
    if (input::get_event(&ev))
    {
        *out = ev;
        return SyscallResult::ok();
    }

    return SyscallResult::err(error::VERR_WOULD_BLOCK);
}

SyscallResult sys_gcon_set_gui_mode(u64 a0, u64, u64, u64, u64, u64)
{
    gcon::set_gui_mode(a0 != 0);
    return SyscallResult::ok();
}

} // namespace syscall
