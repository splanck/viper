//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/Semantic_OOP_Builder.cpp
//
// Summary:
//   Implements the OopIndexBuilder class which constructs the OOP index
//   from a parsed BASIC program. The builder performs multiple phases:
//   - Phase 1: Scan classes and interfaces from the AST
//   - Phase 2: Resolve base classes and implemented interfaces
//   - Phase 3: Detect inheritance cycles
//   - Phase 4: Build vtables and validate overrides
//   - Phase 5: Check interface conformance
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/detail/Semantic_OOP_Internal.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/SemanticDiagUtil.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/TypeSuffix.hpp"

namespace il::frontends::basic::detail
{

//===----------------------------------------------------------------------===//
// OopIndexBuilder Implementation
//===----------------------------------------------------------------------===//

std::string OopIndexBuilder::joinNamespace() const
{
    if (nsStack_.empty())
        return {};
    std::string prefix;
    std::size_t size = 0;
    for (const auto &s : nsStack_)
        size += s.size() + 1;
    if (size)
        size -= 1;
    prefix.reserve(size);
    for (std::size_t i = 0; i < nsStack_.size(); ++i)
    {
        if (i)
            prefix.push_back('.');
        prefix += nsStack_[i];
    }
    return prefix;
}

void OopIndexBuilder::processPropertyDecl(const PropertyDecl &prop, ClassInfo &info)
{
    auto rank = [](Access a) { return a == Access::Public ? 1 : 0; };

    // Validate accessor access levels
    if (prop.get.present && rank(prop.get.access) > rank(prop.access))
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B2113",
                           prop.loc,
                           1,
                           "getter access cannot be more permissive than property access");
        }
    }
    if (prop.set.present && rank(prop.set.access) > rank(prop.access))
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B2114",
                           prop.loc,
                           1,
                           "setter access cannot be more permissive than property access");
        }
    }

    // Synthesize getter
    if (prop.get.present)
    {
        ClassInfo::MethodInfo mi;
        mi.sig.access = prop.get.access;
        mi.sig.returnType = prop.type;
        mi.isStatic = prop.isStatic;
        mi.isPropertyAccessor = true;
        mi.isGetter = true;
        std::string mname = std::string("get_") + prop.name;
        info.methods[mname] = std::move(mi);
        info.methodLocs[mname] = prop.loc;

        if (prop.isStatic)
        {
            checkMeInStaticContext(
                prop.get.body, emitter_, "B2103", "'ME' is not allowed in static method");
        }
    }

    // Synthesize setter
    if (prop.set.present)
    {
        ClassInfo::MethodInfo mi;
        mi.sig.access = prop.set.access;
        mi.sig.paramTypes = {prop.type};
        mi.isStatic = prop.isStatic;
        mi.isPropertyAccessor = true;
        mi.isGetter = false;
        std::string mname = std::string("set_") + prop.name;
        info.methods[mname] = std::move(mi);
        info.methodLocs[mname] = prop.loc;

        if (prop.isStatic)
        {
            checkMeInStaticContext(
                prop.set.body, emitter_, "B2103", "'ME' is not allowed in static method");
        }
    }
}

void OopIndexBuilder::processConstructorDecl(const ConstructorDecl &ctor,
                                             ClassInfo &info,
                                             const ClassDecl &classDecl,
                                             const std::unordered_set<std::string> &fieldNames)
{
    if (ctor.isStatic)
    {
        if (info.hasStaticCtor && emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B2104",
                           ctor.loc,
                           1,
                           "multiple static constructors not allowed");
        }
        info.hasStaticCtor = true;

        if (!ctor.params.empty() && emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B2105",
                           ctor.loc,
                           1,
                           "static constructor cannot have parameters");
        }

        checkMeInStaticContext(
            ctor.body, emitter_, "B2106", "'ME' is not allowed in static constructor");
    }
    else
    {
        info.hasConstructor = true;
        info.ctorParams.clear();
        info.ctorParams.reserve(ctor.params.size());
        for (const auto &param : ctor.params)
        {
            ClassInfo::CtorParam sigParam;
            sigParam.type = param.type;
            sigParam.isArray = param.is_array;
            info.ctorParams.push_back(sigParam);
        }
        checkMemberShadowing(ctor.body, classDecl, fieldNames, emitter_);
    }
}

void OopIndexBuilder::processMethodDecl(const MethodDecl &method,
                                        ClassInfo &info,
                                        const ClassDecl &classDecl,
                                        const std::unordered_set<std::string> &fieldNames)
{
    MethodSig sig;
    sig.paramTypes.reserve(method.params.size());
    for (const auto &param : method.params)
        sig.paramTypes.push_back(param.type);

    if (method.ret.has_value())
        sig.returnType = method.ret;
    else if (auto suffixType = inferAstTypeFromSuffix(method.name))
        sig.returnType = suffixType;

    sig.access = method.access;

    // BUG-099 fix: Store return class name for object-returning methods
    if (!method.explicitClassRetQname.empty())
    {
        std::string qualifiedClassName;
        for (size_t i = 0; i < method.explicitClassRetQname.size(); ++i)
        {
            if (i > 0)
                qualifiedClassName += ".";
            qualifiedClassName += method.explicitClassRetQname[i];
        }
        sig.returnClassName = qualifiedClassName;
    }

    emitMissingReturn(classDecl, method, emitter_);
    checkMemberShadowing(method.body, classDecl, fieldNames, emitter_);

    ClassInfo::MethodInfo mi;
    mi.sig = std::move(sig);
    mi.isStatic = method.isStatic;
    mi.isVirtual = method.isVirtual || method.isOverride;
    mi.isAbstract = method.isAbstract;
    mi.isFinal = method.isFinal;
    mi.slot = -1;
    info.methods[method.name] = std::move(mi);
    info.methodLocs[method.name] = method.loc;

    if (method.isStatic)
    {
        checkMeInStaticContext(
            method.body, emitter_, "B2103", "'ME' is not allowed in static method");
    }
}

void OopIndexBuilder::checkFieldMethodCollisions(ClassInfo &info,
                                                 const ClassDecl &classDecl,
                                                 const std::unordered_set<std::string> &fieldNames)
{
    for (const auto &[methodName, methodInfo] : info.methods)
    {
        for (const auto &fieldName : fieldNames)
        {
            if (string_utils::iequals(methodName, fieldName))
            {
                if (emitter_)
                {
                    auto locIt = info.methodLocs.find(methodName);
                    il::support::SourceLoc loc =
                        locIt != info.methodLocs.end() ? locIt->second : classDecl.loc;
                    std::string msg = "method '" + methodName + "' conflicts with field '" +
                                      fieldName + "' (names are case-insensitive); " +
                                      "rename one to avoid runtime errors";
                    emitter_->emit(il::support::Severity::Error,
                                   "B2017",
                                   loc,
                                   static_cast<uint32_t>(methodName.size()),
                                   std::move(msg));
                }
                break;
            }
        }
    }
}

void OopIndexBuilder::processClassDecl(const ClassDecl &classDecl)
{
    ClassInfo info;
    info.name = classDecl.name;
    info.loc = classDecl.loc;
    info.isAbstract = classDecl.isAbstract;
    info.isFinal = classDecl.isFinal;

    std::string prefix = joinNamespace();
    if (!prefix.empty())
        info.qualifiedName = prefix + "." + classDecl.name;
    else
        info.qualifiedName = classDecl.name;

    if (classDecl.baseName)
        rawBases_.emplace(info.qualifiedName, std::make_pair(*classDecl.baseName, classDecl.loc));

    // Collect fields
    info.fields.reserve(classDecl.fields.size());
    std::unordered_set<std::string> classFieldNames;
    classFieldNames.reserve(classDecl.fields.size());

    for (const auto &field : classDecl.fields)
    {
        ClassInfo::FieldInfo fi{field.name,
                                field.type,
                                field.access,
                                field.isArray,
                                field.arrayExtents,
                                field.objectClassName};
        if (field.isStatic)
            info.staticFields.push_back(std::move(fi));
        else
        {
            info.fields.push_back(std::move(fi));
            classFieldNames.insert(field.name);
        }
    }

    // Process members
    for (const auto &member : classDecl.members)
    {
        if (!member)
            continue;

        switch (member->stmtKind())
        {
            case Stmt::Kind::PropertyDecl:
                processPropertyDecl(static_cast<const PropertyDecl &>(*member), info);
                break;
            case Stmt::Kind::ConstructorDecl:
                processConstructorDecl(static_cast<const ConstructorDecl &>(*member),
                                       info,
                                       classDecl,
                                       classFieldNames);
                break;
            case Stmt::Kind::DestructorDecl:
            {
                info.hasDestructor = true;
                const auto &dtor = static_cast<const DestructorDecl &>(*member);
                checkMemberShadowing(dtor.body, classDecl, classFieldNames, emitter_);
                break;
            }
            case Stmt::Kind::MethodDecl:
                processMethodDecl(
                    static_cast<const MethodDecl &>(*member), info, classDecl, classFieldNames);
                break;
            default:
                break;
        }
    }

    if (!info.hasConstructor)
        info.hasSynthCtor = true;

    // BUG-106 fix: Check field/method collisions
    checkFieldMethodCollisions(info, classDecl, classFieldNames);

    // Capture raw implements list
    for (const auto &implQN : classDecl.implementsQualifiedNames)
    {
        std::string dotted = joinQualified(implQN);
        if (!dotted.empty())
            info.rawImplements.push_back(std::move(dotted));
    }

    index_.classes()[info.qualifiedName] = std::move(info);
}

void OopIndexBuilder::scanClasses(const std::vector<StmtPtr> &stmts)
{
    for (const auto &stmtPtr : stmts)
    {
        if (!stmtPtr)
            continue;

        switch (stmtPtr->stmtKind())
        {
            case Stmt::Kind::NamespaceDecl:
            {
                const auto &ns = static_cast<const NamespaceDecl &>(*stmtPtr);
                for (const auto &seg : ns.path)
                    nsStack_.push_back(seg);
                scanClasses(ns.body);
                nsStack_.resize(nsStack_.size() >= ns.path.size() ? nsStack_.size() - ns.path.size()
                                                                  : 0);
                break;
            }
            case Stmt::Kind::ClassDecl:
                processClassDecl(static_cast<const ClassDecl &>(*stmtPtr));
                break;
            default:
                break;
        }
    }
}

void OopIndexBuilder::scanInterfaces(const std::vector<StmtPtr> &stmts)
{
    for (const auto &stmt : stmts)
    {
        if (!stmt)
            continue;

        if (auto *ns = as<const NamespaceDecl>(*stmt))
        {
            scanInterfaces(ns->body);
            continue;
        }

        if (auto *idecl = as<const InterfaceDecl>(*stmt))
        {
            InterfaceInfo ii;
            ii.qualifiedName = joinQualified(idecl->qualifiedName);
            if (ii.qualifiedName.empty())
                continue;

            ii.ifaceId = index_.allocateInterfaceId();

            std::unordered_set<std::string> seen;
            for (const auto &mem : idecl->members)
            {
                if (!mem)
                    continue;

                if (auto *pd = as<const PropertyDecl>(*mem))
                {
                    if (emitter_)
                    {
                        emitter_->emit(il::support::Severity::Error,
                                       "B2115",
                                       pd->loc,
                                       1,
                                       "interfaces cannot declare properties (methods only)");
                    }
                    continue;
                }

                if (auto *md = as<const MethodDecl>(*mem))
                {
                    if (md->isStatic && emitter_)
                    {
                        emitter_->emit(il::support::Severity::Error,
                                       "B2116",
                                       md->loc,
                                       1,
                                       "interfaces cannot declare STATIC methods");
                    }

                    if (seen.contains(md->name))
                    {
                        if (emitter_)
                        {
                            std::string msg = "interface '" + ii.qualifiedName +
                                              "' declares duplicate method '" + md->name + "'.";
                            emitter_->emit(il::support::Severity::Error,
                                           "E_IFACE_DUP_METHOD",
                                           md->loc,
                                           static_cast<uint32_t>(md->name.size()),
                                           std::move(msg));
                        }
                        continue;
                    }
                    seen.insert(md->name);

                    IfaceMethodSig slot;
                    slot.name = md->name;
                    for (const auto &p : md->params)
                        slot.paramTypes.push_back(p.type);
                    if (md->ret)
                        slot.returnType = md->ret;
                    else if (auto suffixType = inferAstTypeFromSuffix(md->name))
                        slot.returnType = suffixType;
                    ii.slots.push_back(std::move(slot));
                }
            }
            index_.interfacesByQname()[ii.qualifiedName] = std::move(ii);
        }
    }
}

void OopIndexBuilder::collectUsingDirectives(const std::vector<StmtPtr> &stmts)
{
    for (const auto &stmtPtr : stmts)
    {
        if (!stmtPtr || stmtPtr->stmtKind() != Stmt::Kind::UsingDecl)
            continue;

        const auto &u = static_cast<const UsingDecl &>(*stmtPtr);
        std::string nsPath = joinQualified(u.namespacePath);
        if (nsPath.empty())
            continue;

        if (!u.alias.empty())
            usingCtx_.aliases[CanonicalizeIdent(u.alias)] = nsPath;
        else
            usingCtx_.imports.insert(nsPath);
    }
}

std::string OopIndexBuilder::expandAlias(const std::string &q) const
{
    auto pos = q.find('.');
    if (pos == std::string::npos)
        return q;

    std::string first = q.substr(0, pos);
    std::string firstCanon = CanonicalizeIdent(first);
    auto itAlias = usingCtx_.aliases.find(firstCanon);
    if (itAlias == usingCtx_.aliases.end())
        return q;

    std::string tail = q.substr(pos + 1);
    if (tail.empty())
        return itAlias->second;
    return itAlias->second + "." + tail;
}

std::string OopIndexBuilder::resolveBase(const std::string &classQ, const std::string &raw) const
{
    if (raw.empty())
        return {};

    // Already qualified?
    if (raw.find('.') != std::string::npos)
    {
        if (index_.classes().contains(raw))
            return raw;
    }

    // Try sibling in same namespace
    auto lastDot = classQ.rfind('.');
    std::string prefix = (lastDot == std::string::npos) ? std::string{} : classQ.substr(0, lastDot);
    std::string candidate = prefix.empty() ? raw : (prefix + "." + raw);
    if (index_.classes().contains(candidate))
        return candidate;

    // Fallback to raw as top-level
    if (index_.classes().contains(raw))
        return raw;

    return {};
}

std::string OopIndexBuilder::resolveInterface(const std::string &classQ,
                                              const std::string &raw) const
{
    if (raw.find('.') != std::string::npos)
    {
        if (index_.interfacesByQname().contains(raw))
            return raw;
    }

    auto lastDot = classQ.rfind('.');
    std::string prefix = (lastDot == std::string::npos) ? std::string{} : classQ.substr(0, lastDot);
    std::string candidate = prefix.empty() ? raw : (prefix + "." + raw);
    if (index_.interfacesByQname().contains(candidate))
        return candidate;

    if (index_.interfacesByQname().contains(raw))
        return raw;

    return {};
}

void OopIndexBuilder::resolveBasesAndImplements()
{
    for (auto &entry : index_.classes())
    {
        ClassInfo &ci = entry.second;
        auto it = rawBases_.find(ci.qualifiedName);

        if (it != rawBases_.end())
        {
            const std::string &raw = it->second.first;
            std::string rawMaybeAliased = expandAlias(raw);
            std::string resolved = resolveBase(ci.qualifiedName, rawMaybeAliased);

            if (resolved.empty())
            {
                // Try USING imports for unqualified names
                if (rawMaybeAliased.find('.') == std::string::npos)
                {
                    std::vector<std::string> hits;
                    for (const auto &imp : usingCtx_.imports)
                    {
                        std::string cand = imp + "." + rawMaybeAliased;
                        if (index_.classes().contains(cand))
                            hits.push_back(std::move(cand));
                    }

                    if (hits.size() == 1)
                        resolved = std::move(hits.front());
                    else if (hits.size() > 1 && emitter_)
                    {
                        il::frontends::basic::semutil::emitAmbiguousType(
                            *emitter_, it->second.second, 1, rawMaybeAliased, hits);
                    }
                }
            }

            if (resolved.empty() && emitter_)
            {
                std::string msg = std::string("base class not found: '") + raw + "'";
                emitter_->emit(
                    il::support::Severity::Error, "B2101", it->second.second, 1, std::move(msg));
            }

            ci.baseQualified = std::move(resolved);
        }

        // Resolve implemented interfaces
        for (const auto &raw : ci.rawImplements)
        {
            std::string resolved = resolveInterface(ci.qualifiedName, raw);
            if (!resolved.empty())
            {
                const auto &iface = index_.interfacesByQname().at(resolved);
                ci.implementedInterfaces.push_back(iface.ifaceId);
            }
        }
    }
}

void OopIndexBuilder::detectInheritanceCycles()
{
    enum State : uint8_t
    {
        kUnvisited = 0,
        kVisiting = 1,
        kVisited = 2,
    };

    std::unordered_map<std::string, State> state;
    state.reserve(index_.classes().size());

    std::function<void(const std::string &)> detectCycle;
    detectCycle = [&](const std::string &name)
    {
        auto it = state.find(name);
        if (it != state.end() && it->second != kUnvisited)
            return;

        state[name] = kVisiting;
        auto *cls = index_.findClass(name);
        if (cls && !cls->baseQualified.empty())
        {
            auto st = state[cls->baseQualified];
            if (st == kVisiting)
            {
                if (emitter_)
                {
                    std::string msg = std::string("inheritance cycle involving '") + name + "'";
                    emitter_->emit(
                        il::support::Severity::Error, "B2102", cls->loc, 1, std::move(msg));
                }
                cls->baseQualified.clear();
            }
            else if (st == kUnvisited)
            {
                detectCycle(cls->baseQualified);
            }
        }
        state[name] = kVisited;
    };

    for (auto &entry : index_.classes())
        detectCycle(entry.first);
}

void OopIndexBuilder::buildVtables()
{
    std::unordered_map<std::string, bool> processed;
    processed.reserve(index_.classes().size());

    auto findInBases =
        [&](const std::string &startClass,
            const std::string &methodName) -> std::pair<ClassInfo *, ClassInfo::MethodInfo *>
    {
        ClassInfo *cur = index_.findClass(startClass);
        while (cur && !cur->baseQualified.empty())
        {
            ClassInfo *base = index_.findClass(cur->baseQualified);
            if (!base)
                break;
            auto mit = base->methods.find(methodName);
            if (mit != base->methods.end())
                return {base, &mit->second};
            cur = base;
        }
        return {nullptr, nullptr};
    };

    std::function<void(const std::string &)> build;
    build = [&](const std::string &name)
    {
        if (processed[name])
            return;

        ClassInfo *ci = index_.findClass(name);
        if (!ci)
            return;

        // Ensure base is built first
        if (!ci->baseQualified.empty())
            build(ci->baseQualified);

        // Inherit base vtable
        std::vector<std::string> vtable;
        if (!ci->baseQualified.empty())
        {
            ClassInfo *base = index_.findClass(ci->baseQualified);
            if (base)
            {
                vtable = base->vtable;
                for (const auto &mname : base->vtable)
                {
                    auto bit = base->methods.find(mname);
                    if (bit != base->methods.end())
                    {
                        const auto &bm = bit->second;
                        if (bm.isAbstract && ci->methods.find(mname) == ci->methods.end())
                            ci->isAbstract = true;
                    }
                }
            }
        }

        // Assign slots and validate overrides
        for (auto &mp : ci->methods)
        {
            const std::string &mname = mp.first;
            auto &mi = mp.second;

            if (!mi.isVirtual)
                continue;

            if (mi.isAbstract)
                ci->isAbstract = true;

            if (auto [base, bmi] = findInBases(name, mname); bmi != nullptr)
            {
                if (bmi->slot < 0)
                {
                    if (emitter_)
                        emitter_->emit(il::support::Severity::Error,
                                       "B2104",
                                       ci->methodLocs[mname],
                                       static_cast<uint32_t>(mname.size()),
                                       std::string("cannot override non-virtual '") + mname + "'");
                }
                else
                {
                    if (bmi->isFinal && emitter_)
                    {
                        emitter_->emit(il::support::Severity::Error,
                                       "B2107",
                                       ci->methodLocs[mname],
                                       static_cast<uint32_t>(mname.size()),
                                       std::string("cannot override final '") + mname + "'");
                    }

                    const MethodSig &s1 = mi.sig;
                    const MethodSig &s2 = bmi->sig;
                    bool sigOk =
                        (s1.paramTypes == s2.paramTypes) && (s1.returnType == s2.returnType);
                    if (!sigOk && emitter_)
                    {
                        emitter_->emit(il::support::Severity::Error,
                                       "B2103",
                                       ci->methodLocs[mname],
                                       static_cast<uint32_t>(mname.size()),
                                       std::string("override signature mismatch for '") + mname +
                                           "'");
                    }

                    mi.slot = bmi->slot;
                    if (mi.slot >= 0 && static_cast<std::size_t>(mi.slot) < vtable.size())
                        vtable[mi.slot] = mname;
                }
            }
            else
            {
                mi.slot = static_cast<int>(vtable.size());
                vtable.push_back(mname);
            }
        }

        ci->vtable = std::move(vtable);
        processed[name] = true;
    };

    for (auto &entry : index_.classes())
        build(entry.first);
}

void OopIndexBuilder::checkInterfaceConformance()
{
    auto findMethodInClassOrBases = [&](const std::string &classQ,
                                        const std::string &name) -> const ClassInfo::MethodInfo *
    {
        const ClassInfo *cur = index_.findClass(classQ);
        if (!cur)
            return nullptr;
        if (auto it = cur->methods.find(name); it != cur->methods.end())
            return &it->second;
        while (cur && !cur->baseQualified.empty())
        {
            cur = index_.findClass(cur->baseQualified);
            if (!cur)
                break;
            if (auto it2 = cur->methods.find(name); it2 != cur->methods.end())
                return &it2->second;
        }
        return nullptr;
    };

    auto sigsMatch = [](const MethodSig &cls, const IfaceMethodSig &iface)
    {
        if (cls.paramTypes != iface.paramTypes)
            return false;
        if (cls.returnType.has_value() != iface.returnType.has_value())
            return false;
        if (cls.returnType && iface.returnType && *cls.returnType != *iface.returnType)
            return false;
        return true;
    };

    // Build reverse lookup: interface id -> InterfaceInfo
    std::unordered_map<int, const InterfaceInfo *> idToIface;
    for (const auto &p : index_.interfacesByQname())
        idToIface[p.second.ifaceId] = &p.second;

    for (auto &entry : index_.classes())
    {
        ClassInfo &ci = entry.second;
        if (ci.implementedInterfaces.empty())
            continue;

        bool wasAbstract = ci.isAbstract;
        for (int ifaceId : ci.implementedInterfaces)
        {
            auto itF = idToIface.find(ifaceId);
            if (itF == idToIface.end())
                continue;

            const InterfaceInfo &iface = *itF->second;
            std::vector<std::string> mapping;
            mapping.resize(iface.slots.size());

            for (size_t slot = 0; slot < iface.slots.size(); ++slot)
            {
                const auto &slotSig = iface.slots[slot];
                const ClassInfo::MethodInfo *mi =
                    findMethodInClassOrBases(ci.qualifiedName, slotSig.name);

                if (!mi || !sigsMatch(mi->sig, slotSig))
                {
                    ci.isAbstract = true;
                    if (!wasAbstract && emitter_)
                    {
                        std::string msg = "class '" + ci.qualifiedName + "' does not implement '" +
                                          iface.qualifiedName + "." + slotSig.name + "'.";
                        emitter_->emit(il::support::Severity::Error,
                                       "E_CLASS_MISSES_IFACE_METHOD",
                                       ci.loc,
                                       static_cast<uint32_t>(ci.name.size()),
                                       std::move(msg));
                    }
                    continue;
                }
                mapping[slot] = slotSig.name;
            }
            ci.ifaceSlotImpl[ifaceId] = std::move(mapping);
        }
    }
}

void OopIndexBuilder::build(const Program &program)
{
    index_.clear();

    // Phase 1: Scan classes and collect metadata
    scanClasses(program.main);

    // Phase 1b: Scan interfaces and assign stable IDs
    scanInterfaces(program.main);

    // Collect USING directives for resolution
    collectUsingDirectives(program.main);

    // Phase 2: Resolve bases and implements
    resolveBasesAndImplements();

    // Phase 3: Detect inheritance cycles
    detectInheritanceCycles();

    // Phase 4: Build vtables and validate overrides
    buildVtables();

    // Phase 5: Check interface conformance
    checkInterfaceConformance();
}

} // namespace il::frontends::basic::detail
