//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/NetworkRuntime.cpp
// Purpose: VM-aware runtime helpers for Viper.Network.HttpServer handler binding.
//
//===----------------------------------------------------------------------===//

#include "vm/RuntimeBridge.hpp"

#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt.hpp"
#include "rt_http_server.h"
#include "rt_string.h"
#include "support/small_vector.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

namespace il::vm {
namespace {

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

struct VmHttpHandlerPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<VM::ProgramState> program;
    const il::core::Function *entry = nullptr;
};

extern "C" void vm_http_handler_payload_destroy(void *raw) {
    delete static_cast<VmHttpHandlerPayload *>(raw);
}

extern "C" void vm_http_handler_dispatch(void *raw, void *req, void *res) {
    auto *payload = static_cast<VmHttpHandlerPayload *>(raw);
    if (!payload || !payload->module || !payload->entry)
        rt_abort("HttpServer.BindHandler: invalid handler payload");

    try {
        VM vm(*payload->module, payload->program);
        il::support::SmallVector<Slot, 2> args;
        Slot reqSlot{};
        reqSlot.ptr = req;
        args.push_back(reqSlot);
        Slot resSlot{};
        resSlot.ptr = res;
        args.push_back(resSlot);
        detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (...) {
        rt_abort("HttpServer.BindHandler: unhandled exception");
    }
}

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

static void validateHttpHandlerSignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void || fn.params.size() != 2 ||
        fn.params[0].type.kind != Kind::Ptr || fn.params[1].type.kind != Kind::Ptr) {
        rt_trap("HttpServer.BindHandler: invalid handler signature");
    }
}

static void network_http_server_bind_handler_handler(void **args, void *result) {
    (void)result;

    void *server = nullptr;
    rt_string tag = nullptr;
    void *entry = nullptr;
    if (args && args[0])
        server = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        tag = *reinterpret_cast<rt_string *>(args[1]);
    if (args && args[2])
        entry = *reinterpret_cast<void **>(args[2]);

    if (!entry)
        rt_trap("HttpServer.BindHandler: null entry");

    VM *parentVm = activeVMInstance();
    if (!parentVm) {
        rt_http_server_bind_handler(server, tag, entry);
        return;
    }

    std::shared_ptr<VM::ProgramState> program = parentVm->programState();
    if (!program)
        rt_trap("HttpServer.BindHandler: invalid runtime state");

    const il::core::Module &module = parentVm->module();
    const il::core::Function *entryFn = resolveEntryFunction(module, entry);
    if (!entryFn)
        rt_trap("HttpServer.BindHandler: invalid entry");
    validateHttpHandlerSignature(*entryFn);

    auto *payload = new VmHttpHandlerPayload{&module, std::move(program), entryFn};
    rt_http_server_bind_handler_dispatch(
        server,
        tag,
        reinterpret_cast<void *>(&vm_http_handler_dispatch),
        payload,
        reinterpret_cast<void *>(&vm_http_handler_payload_destroy));
}

} // namespace

void registerNetworkRuntimeExternals() {
    ExternDesc ext;
    ext.name = "Viper.Network.HttpServer.BindHandler";
    ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Str, SigParam::Ptr});
    ext.fn = reinterpret_cast<void *>(&network_http_server_bind_handler_handler);
    RuntimeBridge::registerExtern(ext);
}

} // namespace il::vm
