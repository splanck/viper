//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/oop/MethodDispatchHelpers.cpp
// Purpose: Implementation of OOP method dispatch helpers for BASIC lowering.
// Key invariants: See MethodDispatchHelpers.hpp for documentation.
// Ownership/Lifetime: Non-owning reference to Lowerer context.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "MethodDispatchHelpers.hpp"

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

namespace il::frontends::basic
{

// ============================================================================
// MethodDispatchResolver
// ============================================================================

MethodDispatchResolver::MethodDispatchResolver(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

std::string MethodDispatchResolver::checkAccessControl(const ClassInfo &classInfo,
                                                       const std::string &methodName,
                                                       il::support::SourceLoc loc)
{
    auto it = classInfo.methods.find(methodName);
    if (it != classInfo.methods.end() && it->second.sig.access == Access::Private &&
        lowerer_.currentClass() != classInfo.qualifiedName)
    {
        return "cannot access private member '" + methodName + "' of class '" +
               classInfo.qualifiedName + "'";
    }
    return {};
}

MethodDispatchResolver::Resolution MethodDispatchResolver::resolveStaticCall(
    const std::string &classQName,
    const std::string &methodName,
    const std::vector<Type> &argTypes,
    il::support::SourceLoc loc)
{
    Resolution result;

    // Check user-defined class first
    if (const ClassInfo *ci = lowerer_.oopIndex_.findClass(classQName))
    {
        // Resolve overload
        std::string selected = methodName;
        if (auto resolved = sem::resolveMethodOverload(lowerer_.oopIndex_,
                                                       classQName,
                                                       methodName,
                                                       /*isStatic*/ true,
                                                       argTypes,
                                                       lowerer_.currentClass(),
                                                       lowerer_.diagnosticEmitter(),
                                                       loc))
        {
            selected = resolved->methodName;
        }
        else if (lowerer_.diagnosticEmitter())
        {
            result.kind = Resolution::Kind::Unresolved;
            return result;
        }

        auto it = ci->methods.find(selected);
        if (it != ci->methods.end() && it->second.isStatic)
        {
            result.kind = Resolution::Kind::Direct;
            result.target = mangleMethod(ci->qualifiedName, selected);
            result.hasReceiver = false;

            // Determine return type
            if (auto retType = lowerer_.findMethodReturnType(classQName, selected))
            {
                result.returnKind = type_conv::astToIlType(*retType).kind;
            }
            return result;
        }
    }

    // Try runtime catalog
    if (auto catalogResult = tryRuntimeCatalog(classQName, methodName, argTypes.size()))
    {
        return *catalogResult;
    }

    result.kind = Resolution::Kind::Unresolved;
    return result;
}

MethodDispatchResolver::Resolution MethodDispatchResolver::resolveInstanceCall(
    const std::string &receiverClassQName,
    const std::string &methodName,
    const std::vector<Type> &argTypes,
    bool isBaseQualified,
    il::support::SourceLoc loc)
{
    Resolution result;
    result.hasReceiver = true;

    if (receiverClassQName.empty())
    {
        result.kind = Resolution::Kind::Unresolved;
        return result;
    }

    // Access control check
    if (const ClassInfo *ci = lowerer_.oopIndex_.findClass(receiverClassQName))
    {
        std::string accessError = checkAccessControl(*ci, methodName, loc);
        if (!accessError.empty())
        {
            result.kind = Resolution::Kind::Unresolved;
            result.accessDenied = true;
            result.accessErrorMsg = accessError;
            return result;
        }
    }

    // Resolve overload
    std::string selected = methodName;
    // BUG-OOP-002/003 fix: Track the declaring class for inherited method dispatch
    std::string declaringClass = receiverClassQName;
    if (auto resolved = sem::resolveMethodOverload(lowerer_.oopIndex_,
                                                   receiverClassQName,
                                                   methodName,
                                                   /*isStatic*/ false,
                                                   argTypes,
                                                   lowerer_.currentClass(),
                                                   lowerer_.diagnosticEmitter(),
                                                   loc))
    {
        selected = resolved->methodName;
        declaringClass = resolved->qualifiedClass; // Use declaring class for dispatch
    }
    else if (lowerer_.diagnosticEmitter())
    {
        result.kind = Resolution::Kind::Unresolved;
        return result;
    }

    // Determine virtual slot - use declaring class for inherited methods
    int slot = getVirtualSlot(lowerer_.oopIndex_, declaringClass, selected);

    // Determine target class for direct dispatch - use declaring class for inherited methods
    std::string directQClass = declaringClass;
    if (isBaseQualified)
    {
        const std::string cur = lowerer_.currentClass();
        if (!cur.empty())
        {
            if (const ClassInfo *ci = lowerer_.oopIndex_.findClass(cur))
            {
                if (!ci->baseQualified.empty())
                    directQClass = ci->baseQualified;
            }
        }
    }

    // Virtual dispatch (not BASE-qualified)
    if (slot >= 0 && !isBaseQualified)
    {
        result.kind = Resolution::Kind::Virtual;
        result.slot = slot;
        // BUG-OOP-002/003 fix: Use declaring class for return type lookup
        if (auto retType = lowerer_.findMethodReturnType(declaringClass, selected))
        {
            result.returnKind = type_conv::astToIlType(*retType).kind;
        }
        return result;
    }

    // Direct call
    result.kind = Resolution::Kind::Direct;
    const ClassInfo *targetCI = lowerer_.oopIndex_.findClass(directQClass);
    std::string emitClassName = targetCI ? targetCI->qualifiedName : directQClass;
    result.target = emitClassName.empty() ? selected : mangleMethod(emitClassName, selected);

    // Return type lookup - use base class for BASE-qualified calls
    const std::string retClassLookup = isBaseQualified ? directQClass : receiverClassQName;
    if (auto retType = lowerer_.findMethodReturnType(retClassLookup, selected))
    {
        result.returnKind = type_conv::astToIlType(*retType).kind;
    }

    return result;
}

MethodDispatchResolver::Resolution MethodDispatchResolver::resolveInterfaceCall(
    const std::string &interfaceQName, const std::string &methodName, std::size_t argCount)
{
    Resolution result;
    result.hasReceiver = true;

    // Find interface in OOP index
    const InterfaceInfo *iface = nullptr;
    for (const auto &p : lowerer_.oopIndex_.interfacesByQname())
    {
        if (p.first == interfaceQName)
        {
            iface = &p.second;
            break;
        }
    }

    if (!iface)
    {
        result.kind = Resolution::Kind::Unresolved;
        return result;
    }

    // Find slot by method name and arity
    int slotIndex = -1;
    for (std::size_t idx = 0; idx < iface->slots.size(); ++idx)
    {
        const auto &sig = iface->slots[idx];
        if (sig.name != methodName)
            continue;
        if (sig.paramTypes.size() == argCount)
        {
            slotIndex = static_cast<int>(idx);
            break;
        }
        // Fallback: first name match
        if (slotIndex < 0)
            slotIndex = static_cast<int>(idx);
    }

    if (slotIndex < 0)
    {
        result.kind = Resolution::Kind::Unresolved;
        return result;
    }

    result.kind = Resolution::Kind::Interface;
    result.slot = slotIndex;
    result.ifaceId = iface->ifaceId;

    // Return type from interface signature
    if (slotIndex >= 0 && static_cast<std::size_t>(slotIndex) < iface->slots.size())
    {
        if (iface->slots[static_cast<std::size_t>(slotIndex)].returnType)
        {
            result.returnKind = type_conv::astToIlType(
                                    *iface->slots[static_cast<std::size_t>(slotIndex)].returnType)
                                    .kind;
        }
    }

    return result;
}

std::optional<MethodDispatchResolver::Resolution> MethodDispatchResolver::tryRuntimeCatalog(
    const std::string &classQName, const std::string &methodName, std::size_t argCount)
{
    // Check if this is a runtime class
    auto isRuntimeClass = [](const std::string &qn)
    {
        const auto &rc = il::runtime::runtimeClassCatalog();
        for (const auto &c : rc)
            if (string_utils::iequals(qn, c.qname))
                return true;
        return false;
    };

    if (!isRuntimeClass(classQName))
        return std::nullopt;

    auto &midx = runtimeMethodIndex();
    auto info = midx.find(classQName, methodName, argCount);
    if (!info)
        return std::nullopt;

    Resolution result;
    result.kind = Resolution::Kind::RuntimeCatalog;
    result.target = info->target;
    result.hasReceiver = false; // Static calls don't have receiver
    result.returnKind = basicTypeToILKind(info->ret);
    result.expectedArgs = info->args;

    return result;
}

// ============================================================================
// BoundsCheckEmitter
// ============================================================================

BoundsCheckEmitter::BoundsCheckEmitter(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

void BoundsCheckEmitter::emitBoundsCheck(il::core::Value arrHandle,
                                         il::core::Value index,
                                         Type elemKind,
                                         bool isObjectArray,
                                         il::support::SourceLoc loc,
                                         std::string_view labelPrefix)
{
    lowerer_.curLoc = loc;

    // Get array length using appropriate helper
    il::core::Value len;
    if (elemKind == Type::Str)
    {
        lowerer_.requireArrayStrLen();
        len = lowerer_.emitCallRet(
            il::core::Type(il::core::Type::Kind::I64), "rt_arr_str_len", {arrHandle});
    }
    else if (isObjectArray)
    {
        lowerer_.requireArrayObjLen();
        len = lowerer_.emitCallRet(
            il::core::Type(il::core::Type::Kind::I64), "rt_arr_obj_len", {arrHandle});
    }
    else
    {
        lowerer_.requireArrayI32Len();
        len = lowerer_.emitCallRet(
            il::core::Type(il::core::Type::Kind::I64), "rt_arr_i32_len", {arrHandle});
    }

    // Check index < 0 || index >= len
    il::core::Value isNeg = lowerer_.emitBinary(
        il::core::Opcode::SCmpLT, lowerer_.ilBoolTy(), index, il::core::Value::constInt(0));
    il::core::Value tooHigh =
        lowerer_.emitBinary(il::core::Opcode::SCmpGE, lowerer_.ilBoolTy(), index, len);

    auto emitc = lowerer_.emitCommon(loc);
    il::core::Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
    il::core::Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
    il::core::Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
    il::core::Value oobCond = lowerer_.emitBinary(
        il::core::Opcode::ICmpNe, lowerer_.ilBoolTy(), oobInt, il::core::Value::constInt(0));

    // Create ok/oob blocks
    ProcedureContext &ctx = lowerer_.context();
    il::core::Function *func = ctx.function();
    std::size_t curIdx = static_cast<std::size_t>(ctx.current() - &func->blocks[0]);
    unsigned bcId = ctx.consumeBoundsCheckId();
    BlockNamer *blockNamer = ctx.blockNames().namer();

    std::string prefix(labelPrefix);
    if (prefix.empty())
        prefix = "bc";

    std::size_t okIdx = func->blocks.size();
    std::string okLbl = blockNamer ? blockNamer->tag(prefix + "_ok" + std::to_string(bcId))
                                   : lowerer_.mangler.block(prefix + "_ok" + std::to_string(bcId));
    lowerer_.builder->addBlock(*func, okLbl);

    std::size_t oobIdx = func->blocks.size();
    std::string oobLbl = blockNamer
                             ? blockNamer->tag(prefix + "_oob" + std::to_string(bcId))
                             : lowerer_.mangler.block(prefix + "_oob" + std::to_string(bcId));
    lowerer_.builder->addBlock(*func, oobLbl);

    il::core::BasicBlock *ok = &func->blocks[okIdx];
    il::core::BasicBlock *oob = &func->blocks[oobIdx];

    // Emit conditional branch
    ctx.setCurrent(&func->blocks[curIdx]);
    lowerer_.emitCBr(oobCond, oob, ok);

    // OOB path: panic and trap
    ctx.setCurrent(oob);
    lowerer_.requireArrayOobPanic();
    lowerer_.emitCall("rt_arr_oob_panic", {index, len});
    lowerer_.emitTrap();

    // Continue in ok block
    ctx.setCurrent(ok);
}

il::core::Value BoundsCheckEmitter::computeFlattenedIndex(
    const std::vector<il::core::Value> &indices,
    const std::vector<long long> &extents,
    il::support::SourceLoc loc)
{
    if (indices.empty())
        return il::core::Value::constInt(0);

    if (indices.size() == 1)
        return indices[0];

    if (extents.size() != indices.size())
        return indices[0]; // Fallback

    // Compute lengths from extents (BASIC uses inclusive upper bounds)
    std::vector<long long> lengths;
    lengths.reserve(extents.size());
    for (long long e : extents)
        lengths.push_back(e + 1);

    lowerer_.curLoc = loc;

    // Start with first index * stride for remaining dimensions
    long long stride = 1;
    for (std::size_t i = 1; i < lengths.size(); ++i)
        stride *= lengths[i];

    il::core::Value result = lowerer_.emitBinary(il::core::Opcode::IMulOvf,
                                                 il::core::Type(il::core::Type::Kind::I64),
                                                 indices[0],
                                                 il::core::Value::constInt(stride));

    // Add remaining dimension contributions
    for (std::size_t k = 1; k < indices.size(); ++k)
    {
        stride = 1;
        for (std::size_t i = k + 1; i < lengths.size(); ++i)
            stride *= lengths[i];

        lowerer_.curLoc = loc;
        il::core::Value term = lowerer_.emitBinary(il::core::Opcode::IMulOvf,
                                                   il::core::Type(il::core::Type::Kind::I64),
                                                   indices[k],
                                                   il::core::Value::constInt(stride));

        lowerer_.curLoc = loc;
        result = lowerer_.emitBinary(
            il::core::Opcode::IAddOvf, il::core::Type(il::core::Type::Kind::I64), result, term);
    }

    return result;
}

} // namespace il::frontends::basic
