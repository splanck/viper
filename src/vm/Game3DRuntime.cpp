//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Game3DRuntime.cpp
// Purpose: VM-aware Game3D callback-loop bridges for interpreted Viper code.
// Key invariants:
//   - Script callback references are resolved against the active VM module.
//   - Runtime receives only native C-callable trampoline pointers.
// Ownership/Lifetime:
//   - Callback scopes are thread-local and valid only during synchronous Game3D calls.
//   - The VM owns program state and callback functions; this bridge only borrows them.
// Links: src/runtime/graphics/3d/rt_game3d.h, src/il/runtime/runtime.def
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief VM-aware runtime helpers for Game3D callback loops.

#include "vm/RuntimeBridge.hpp"

#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt.hpp"
#include "rt_game3d.h"
#include "rt_platform.h"
#include "support/small_vector.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <string>

namespace il::vm {
namespace {

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

struct VmGame3DCallbackScope {
    VM *vm = nullptr;
    const il::core::Function *update = nullptr;
    const il::core::Function *overlay = nullptr;
    VmGame3DCallbackScope *previous = nullptr;
};

thread_local VmGame3DCallbackScope *tlsGame3DScope = nullptr;

static const il::core::Function *resolveEntryFunction(const il::core::Module &module, void *entry) {
    if (!entry)
        return nullptr;
    const auto *candidate = static_cast<const il::core::Function *>(entry);
    for (const auto &fn : module.functions) {
        if (&fn == candidate)
            return &fn;
    }
    return nullptr;
}

static void validateUpdateSignature(const il::core::Function &fn, const char *api) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind == Kind::Void && fn.params.size() == 1 &&
        fn.params[0].type.kind == Kind::F64) {
        return;
    }
    std::string message(api);
    message += ": update callback must have signature (Float) -> Unit";
    rt_trap(message.c_str());
}

static void validateOverlaySignature(const il::core::Function &fn, const char *api) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind == Kind::Void && fn.params.empty())
        return;
    std::string message(api);
    message += ": overlay callback must have signature () -> Unit";
    rt_trap(message.c_str());
}

static void invokeVmUpdate(VmGame3DCallbackScope *scope, double dt) {
    if (!scope || !scope->vm || !scope->update) {
        rt_trap("Game3D VM callback bridge: invalid update callback scope");
        return;
    }

    il::support::SmallVector<Slot, 1> args;
    Slot dtSlot{};
    dtSlot.f64 = dt;
    args.push_back(dtSlot);
    detail::VMAccess::callFunction(*scope->vm, *scope->update, args);
}

static void invokeVmOverlay(VmGame3DCallbackScope *scope) {
    if (!scope || !scope->vm || !scope->overlay) {
        rt_trap("Game3D VM callback bridge: invalid overlay callback scope");
        return;
    }

    il::support::SmallVector<Slot, 1> args;
    detail::VMAccess::callFunction(*scope->vm, *scope->overlay, args);
}

extern "C" void vm_game3d_update_trampoline(double dt) {
    VmGame3DCallbackScope *scope = tlsGame3DScope;
    try {
        invokeVmUpdate(scope, dt);
    } catch (const RuntimeTrapSignal &signal) {
        rt_trap(signal.message.empty() ? "Game3D.World3D: trapped VM update callback"
                                       : signal.message.c_str());
    } catch (const std::exception &ex) {
        const std::string message =
            std::string("Game3D.World3D: unhandled VM update exception: ") + ex.what();
        rt_trap(message.c_str());
    } catch (...) {
        rt_trap("Game3D.World3D: unhandled VM update exception");
    }
}

extern "C" void vm_game3d_overlay_trampoline(void) {
    VmGame3DCallbackScope *scope = tlsGame3DScope;
    try {
        invokeVmOverlay(scope);
    } catch (const RuntimeTrapSignal &signal) {
        rt_trap(signal.message.empty() ? "Game3D.World3D: trapped VM overlay callback"
                                       : signal.message.c_str());
    } catch (const std::exception &ex) {
        const std::string message =
            std::string("Game3D.World3D: unhandled VM overlay exception: ") + ex.what();
        rt_trap(message.c_str());
    } catch (...) {
        rt_trap("Game3D.World3D: unhandled VM overlay exception");
    }
}

template <typename Fn>
static void invokeGame3DLoopWithScope(VmGame3DCallbackScope &scope, Fn &&fn) {
    char trapMessage[512] = "";
    int trapped = 0;
    jmp_buf recovery;

    scope.previous = tlsGame3DScope;
    tlsGame3DScope = &scope;
    rt_trap_set_recovery(&recovery);
    RT_SUPPRESS_SETJMP_WARNING_BEGIN;
    const int recoveryState = setjmp(recovery);
    RT_SUPPRESS_SETJMP_WARNING_END;
    if (recoveryState != 0) {
        const char *msg = rt_trap_get_error();
        std::snprintf(trapMessage, sizeof(trapMessage), "%s", msg && msg[0] ? msg : "Game3D trap");
        trapped = 1;
    } else {
        fn();
    }
    rt_trap_clear_recovery();
    tlsGame3DScope = scope.previous;
    if (trapped)
        rt_trap(trapMessage);
}

static VM *activeVmOrNative() {
    return activeVMInstance();
}

static const il::core::Function *resolveVmCallback(VM &vm, void *entry, const char *api) {
    const il::core::Function *fn = resolveEntryFunction(vm.module(), entry);
    if (!fn) {
        std::string message(api);
        message += ": callback is not a function in the active VM module";
        rt_trap(message.c_str());
    }
    return fn;
}

static void game3d_run_handler(void **args, void *result) {
    (void)result;
    void *world = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *update = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;

    if (VM *vm = activeVmOrNative()) {
        const il::core::Function *updateFn = resolveVmCallback(*vm, update, "Game3D.World3D.run");
        validateUpdateSignature(*updateFn, "Game3D.World3D.run");
        VmGame3DCallbackScope scope{vm, updateFn, nullptr, nullptr};
        invokeGame3DLoopWithScope(scope, [&]() {
            rt_game3d_world_run(world, reinterpret_cast<void *>(&vm_game3d_update_trampoline));
        });
        return;
    }

    rt_game3d_world_run(world, update);
}

static void game3d_run_with_overlay_handler(void **args, void *result) {
    (void)result;
    void *world = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *update = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;
    void *overlay = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;

    if (VM *vm = activeVmOrNative()) {
        const il::core::Function *updateFn =
            resolveVmCallback(*vm, update, "Game3D.World3D.runWithOverlay");
        const il::core::Function *overlayFn =
            resolveVmCallback(*vm, overlay, "Game3D.World3D.runWithOverlay");
        validateUpdateSignature(*updateFn, "Game3D.World3D.runWithOverlay");
        validateOverlaySignature(*overlayFn, "Game3D.World3D.runWithOverlay");
        VmGame3DCallbackScope scope{vm, updateFn, overlayFn, nullptr};
        invokeGame3DLoopWithScope(scope, [&]() {
            rt_game3d_world_run_with_overlay(
                world,
                reinterpret_cast<void *>(&vm_game3d_update_trampoline),
                reinterpret_cast<void *>(&vm_game3d_overlay_trampoline));
        });
        return;
    }

    rt_game3d_world_run_with_overlay(world, update, overlay);
}

static void game3d_run_fixed_handler(void **args, void *result) {
    (void)result;
    void *world = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    double step = args && args[1] ? *reinterpret_cast<double *>(args[1]) : 0.0;
    void *update = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;

    if (VM *vm = activeVmOrNative()) {
        const il::core::Function *updateFn =
            resolveVmCallback(*vm, update, "Game3D.World3D.runFixed");
        validateUpdateSignature(*updateFn, "Game3D.World3D.runFixed");
        VmGame3DCallbackScope scope{vm, updateFn, nullptr, nullptr};
        invokeGame3DLoopWithScope(scope, [&]() {
            rt_game3d_world_run_fixed(
                world, step, reinterpret_cast<void *>(&vm_game3d_update_trampoline));
        });
        return;
    }

    rt_game3d_world_run_fixed(world, step, update);
}

static void game3d_run_fixed_with_overlay_handler(void **args, void *result) {
    (void)result;
    void *world = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    double step = args && args[1] ? *reinterpret_cast<double *>(args[1]) : 0.0;
    void *update = args && args[2] ? *reinterpret_cast<void **>(args[2]) : nullptr;
    void *overlay = args && args[3] ? *reinterpret_cast<void **>(args[3]) : nullptr;

    if (VM *vm = activeVmOrNative()) {
        const il::core::Function *updateFn =
            resolveVmCallback(*vm, update, "Game3D.World3D.runFixedWithOverlay");
        const il::core::Function *overlayFn =
            resolveVmCallback(*vm, overlay, "Game3D.World3D.runFixedWithOverlay");
        validateUpdateSignature(*updateFn, "Game3D.World3D.runFixedWithOverlay");
        validateOverlaySignature(*overlayFn, "Game3D.World3D.runFixedWithOverlay");
        VmGame3DCallbackScope scope{vm, updateFn, overlayFn, nullptr};
        invokeGame3DLoopWithScope(scope, [&]() {
            rt_game3d_world_run_fixed_with_overlay(
                world,
                step,
                reinterpret_cast<void *>(&vm_game3d_update_trampoline),
                reinterpret_cast<void *>(&vm_game3d_overlay_trampoline));
        });
        return;
    }

    rt_game3d_world_run_fixed_with_overlay(world, step, update, overlay);
}

static void game3d_run_frames_handler(void **args, void *result) {
    (void)result;
    void *world = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    int64_t frames = args && args[1] ? *reinterpret_cast<int64_t *>(args[1]) : 0;
    double step = args && args[2] ? *reinterpret_cast<double *>(args[2]) : 0.0;
    void *update = args && args[3] ? *reinterpret_cast<void **>(args[3]) : nullptr;

    if (VM *vm = activeVmOrNative()) {
        const il::core::Function *updateFn =
            resolveVmCallback(*vm, update, "Game3D.World3D.runFrames");
        validateUpdateSignature(*updateFn, "Game3D.World3D.runFrames");
        VmGame3DCallbackScope scope{vm, updateFn, nullptr, nullptr};
        invokeGame3DLoopWithScope(scope, [&]() {
            rt_game3d_world_run_frames(
                world, frames, step, reinterpret_cast<void *>(&vm_game3d_update_trampoline));
        });
        return;
    }

    rt_game3d_world_run_frames(world, frames, step, update);
}

static void game3d_draw_overlay_handler(void **args, void *result) {
    (void)result;
    void *world = args && args[0] ? *reinterpret_cast<void **>(args[0]) : nullptr;
    void *overlay = args && args[1] ? *reinterpret_cast<void **>(args[1]) : nullptr;

    if (VM *vm = activeVmOrNative()) {
        const il::core::Function *overlayFn =
            resolveVmCallback(*vm, overlay, "Game3D.World3D.drawOverlay");
        validateOverlaySignature(*overlayFn, "Game3D.World3D.drawOverlay");
        VmGame3DCallbackScope scope{vm, nullptr, overlayFn, nullptr};
        invokeGame3DLoopWithScope(scope, [&]() {
            rt_game3d_world_draw_overlay(world,
                                         reinterpret_cast<void *>(&vm_game3d_overlay_trampoline));
        });
        return;
    }

    rt_game3d_world_draw_overlay(world, overlay);
}

static void registerExtern(const char *name,
                           std::initializer_list<SigParam::Kind> params,
                           void *handler) {
    ExternDesc ext;
    ext.name = name;
    ext.signature = make_signature(ext.name, params);
    ext.fn = handler;
    RuntimeBridge::registerExtern(ext);
}

} // namespace

void registerGame3DRuntimeExternals() {
    registerExtern("Viper.Game3D.World3D.run",
                   {SigParam::Ptr, SigParam::Ptr},
                   reinterpret_cast<void *>(&game3d_run_handler));
    registerExtern("Viper.Game3D.World3D.runWithOverlay",
                   {SigParam::Ptr, SigParam::Ptr, SigParam::Ptr},
                   reinterpret_cast<void *>(&game3d_run_with_overlay_handler));
    registerExtern("Viper.Game3D.World3D.runFixed",
                   {SigParam::Ptr, SigParam::F64, SigParam::Ptr},
                   reinterpret_cast<void *>(&game3d_run_fixed_handler));
    registerExtern("Viper.Game3D.World3D.runFixedWithOverlay",
                   {SigParam::Ptr, SigParam::F64, SigParam::Ptr, SigParam::Ptr},
                   reinterpret_cast<void *>(&game3d_run_fixed_with_overlay_handler));
    registerExtern("Viper.Game3D.World3D.runFrames",
                   {SigParam::Ptr, SigParam::I64, SigParam::F64, SigParam::Ptr},
                   reinterpret_cast<void *>(&game3d_run_frames_handler));
    registerExtern("Viper.Game3D.World3D.drawOverlay",
                   {SigParam::Ptr, SigParam::Ptr},
                   reinterpret_cast<void *>(&game3d_draw_overlay_handler));
}

} // namespace il::vm
