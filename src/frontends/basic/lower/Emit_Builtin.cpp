//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements runtime helper emission for BASIC built-ins.
/// @details These utilities manage array ownership by retaining and releasing
/// handles while appending the required calls to the active block. They do not
/// produce terminators and therefore rely on control helpers to manage block
/// lifetimes; temporaries remain owned by the lowerer and follow the standard
/// ProcedureContext tracking.

#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::storeArray(Value slot, Value value)
{
    requireArrayI32Retain();
    emitCall("rt_arr_i32_retain", {value});
    Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
    requireArrayI32Release();
    emitCall("rt_arr_i32_release", {oldValue});
    emitStore(Type(Type::Kind::Ptr), slot, value);
}

void Lowerer::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    bool requested = false;
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

void Lowerer::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;
    bool requested = false;
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (!paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

} // namespace il::frontends::basic
