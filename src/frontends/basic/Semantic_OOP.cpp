//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/Semantic_OOP.cpp
//
// Summary:
//   Implements the semantic index used by the BASIC front end to store
//   information about classes declared in the source program.  The translation
//   unit builds and queries the index so later lowering phases can reason about
//   constructors, destructors, fields, and method signatures without repeatedly
//   walking the AST.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief OOP index helpers for the BASIC front end.
/// @details Provides lookup methods over @ref OopIndex and the
///          @ref buildOopIndex routine that populates the index by scanning the
///          parsed BASIC program.  The helpers copy relevant metadata from the
///          AST into stable structures, enabling deterministic queries during
///          lowering passes.

#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/AST.hpp"
#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include "support/diagnostics.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace il::frontends::basic
{

namespace
{
class MemberShadowCheckWalker final : public BasicAstWalker<MemberShadowCheckWalker>
{
  public:
    MemberShadowCheckWalker(const std::string &className,
                            const std::unordered_set<std::string> &fields,
                            DiagnosticEmitter *emitter) noexcept
        : className_(className), fields_(fields), emitter_(emitter)
    {
    }

    void before(const DimStmt &stmt)
    {
        if (!emitter_ || stmt.name.empty())
            return;
        if (!fields_.count(stmt.name))
            return;

        std::string qualifiedField = className_;
        if (!qualifiedField.empty())
            qualifiedField += '.';
        qualifiedField += stmt.name;

        std::string msg = "local '" + stmt.name + "' shadows field '" + qualifiedField +
                          "'; use Me." + stmt.name + " to access the field";
        emitter_->emit(il::support::Severity::Warning,
                       "B2016",
                       stmt.loc,
                       static_cast<uint32_t>(stmt.name.size()),
                       std::move(msg));
    }

  private:
    std::string className_;
    const std::unordered_set<std::string> &fields_;
    DiagnosticEmitter *emitter_;
};

void checkMemberShadowing(const std::vector<StmtPtr> &body,
                          const ClassDecl &klass,
                          const std::unordered_set<std::string> &fieldNames,
                          DiagnosticEmitter *emitter)
{
    if (!emitter || fieldNames.empty())
        return;

    MemberShadowCheckWalker walker(klass.name, fieldNames, emitter);
    for (const auto &stmt : body)
        if (stmt)
            walker.walkStmt(*stmt);
}

[[nodiscard]] bool methodMustReturn(const Stmt &stmt)
{
    if (auto *lst = dynamic_cast<const StmtList *>(&stmt))
        return !lst->stmts.empty() && methodMustReturn(*lst->stmts.back());
    if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt))
        return ret->value != nullptr;
    if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt))
    {
        if (!ifs->then_branch || !methodMustReturn(*ifs->then_branch))
            return false;
        for (const auto &elifBranch : ifs->elseifs)
            if (!elifBranch.then_branch || !methodMustReturn(*elifBranch.then_branch))
                return false;
        if (!ifs->else_branch)
            return false;
        return methodMustReturn(*ifs->else_branch);
    }
    if (dynamic_cast<const WhileStmt *>(&stmt) != nullptr ||
        dynamic_cast<const ForStmt *>(&stmt) != nullptr)
        return false;
    return false;
}

[[nodiscard]] bool methodBodyMustReturn(const std::vector<StmtPtr> &stmts)
{
    if (stmts.empty())
        return false;
    const StmtPtr &tail = stmts.back();
    return tail && methodMustReturn(*tail);
}

void emitMissingReturn(const ClassDecl &klass, const MethodDecl &method, DiagnosticEmitter *emitter)
{
    if (!emitter)
        return;
    if (!method.ret)
        return;
    if (methodBodyMustReturn(method.body))
        return;

    std::string qualified = klass.name;
    if (!qualified.empty())
        qualified += '.';
    qualified += method.name;

    std::string msg = "missing return in FUNCTION " + qualified;
    emitter->emit(il::support::Severity::Error, "B1007", method.loc, 3, std::move(msg));
}
} // namespace

/// @brief Look up a mutable class record by name.
/// @details Searches the internal @c std::unordered_map for the requested class
///          name and returns a pointer to the stored @ref ClassInfo instance
///          when found.  Returning @c nullptr keeps callers explicit about the
///          missing-class case without performing map insertions.
/// @param name Class identifier to locate.
/// @return Pointer to the associated @ref ClassInfo or @c nullptr when absent.
ClassInfo *OopIndex::findClass(const std::string &name)
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Look up an immutable class record by name.
/// @details Const-qualified overload used by read-only consumers.  The method
///          performs the same map probe as the mutable variant but preserves
///          const-correctness so callers cannot mutate the stored metadata.
/// @param name Class identifier to locate.
/// @return Pointer to the stored @ref ClassInfo or @c nullptr when absent.
const ClassInfo *OopIndex::findClass(const std::string &name) const
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Populate the OOP index from a parsed BASIC program.
/// @details Clears any pre-existing entries then walks the top-level statements
///          collecting class declarations.  For each class the helper captures
///          field metadata, method signatures, and constructor/destructor flags
///          so downstream passes can query structural details without revisiting
///          the AST.  When @p emitter is provided the helper reports missing
///          return diagnostics for value-returning methods.
/// @param program Parsed BASIC program supplying class declarations.
/// @param index Index instance that receives the reconstructed metadata.
/// @param emitter Optional diagnostics interface reserved for future checks.
void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter *emitter)
{
    index.clear();

    // Keep a simple namespace stack to form qualified names.
    std::vector<std::string> nsStack;
    auto joinNs = [&]() -> std::string {
        if (nsStack.empty())
            return {};
        std::string prefix;
        std::size_t size = 0;
        for (const auto &s : nsStack)
            size += s.size() + 1;
        if (size)
            size -= 1; // trailing dot not needed
        prefix.reserve(size);
        for (std::size_t i = 0; i < nsStack.size(); ++i)
        {
            if (i)
                prefix.push_back('.');
            prefix += nsStack[i];
        }
        return prefix;
    };

    // Track raw base names by class for later resolution and checking.
    std::unordered_map<std::string, std::pair<std::string, il::support::SourceLoc>> rawBases;

    // Recursive lambda to scan statements and populate the index (phase 1).
    std::function<void(const std::vector<StmtPtr>&)> scan;
    scan = [&](const std::vector<StmtPtr> &stmts) {
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
                        nsStack.push_back(seg);
                    scan(ns.body);
                    nsStack.resize(nsStack.size() >= ns.path.size() ? nsStack.size() - ns.path.size() : 0);
                    break;
                }
                case Stmt::Kind::ClassDecl:
                {
                    const auto &classDecl = static_cast<const ClassDecl &>(*stmtPtr);
                    ClassInfo info;
                    info.name = classDecl.name;
                    info.loc = classDecl.loc;
                    info.isAbstract = classDecl.isAbstract;
                    info.isFinal = classDecl.isFinal;
                    std::string prefix = joinNs();
                    if (!prefix.empty())
                        info.qualifiedName = prefix + "." + classDecl.name;
                    else
                        info.qualifiedName = classDecl.name;

                    if (classDecl.baseName)
                        rawBases.emplace(info.qualifiedName,
                                         std::make_pair(*classDecl.baseName, classDecl.loc));

                    info.fields.reserve(classDecl.fields.size());
                    std::unordered_set<std::string> classFieldNames;
                    classFieldNames.reserve(classDecl.fields.size());
                    for (const auto &field : classDecl.fields)
                    {
                        info.fields.push_back(ClassInfo::FieldInfo{field.name, field.type, field.access});
                        classFieldNames.insert(field.name);
                    }

                    for (const auto &member : classDecl.members)
                    {
                        if (!member)
                            continue;
                        switch (member->stmtKind())
                        {
                            case Stmt::Kind::ConstructorDecl:
                            {
                                const auto &ctor = static_cast<const ConstructorDecl &>(*member);
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
                                checkMemberShadowing(ctor.body, classDecl, classFieldNames, emitter);
                                break;
                            }
                            case Stmt::Kind::DestructorDecl:
                            {
                                info.hasDestructor = true;
                                const auto &dtor = static_cast<const DestructorDecl &>(*member);
                                checkMemberShadowing(dtor.body, classDecl, classFieldNames, emitter);
                                break;
                            }
                            case Stmt::Kind::MethodDecl:
                            {
                                const auto &method = static_cast<const MethodDecl &>(*member);
                                MethodSig sig;
                                sig.paramTypes.reserve(method.params.size());
                                for (const auto &param : method.params)
                                {
                                    sig.paramTypes.push_back(param.type);
                                }
                                if (method.ret.has_value())
                                {
                                    sig.returnType = method.ret;
                                }
                                else if (auto suffixType = inferAstTypeFromSuffix(method.name))
                                {
                                    sig.returnType = suffixType;
                                }
                                sig.access = method.access;
                                emitMissingReturn(classDecl, method, emitter);
                                checkMemberShadowing(method.body, classDecl, classFieldNames, emitter);
                                ClassInfo::MethodInfo mi;
                                mi.sig = std::move(sig);
                                mi.isVirtual = method.isVirtual || method.isOverride;
                                mi.isAbstract = method.isAbstract;
                                mi.isFinal = method.isFinal;
                                mi.slot = -1;
                                info.methods[method.name] = std::move(mi);
                                info.methodLocs[method.name] = method.loc;
                                break;
                            }
                            default:
                                break;
                        }
                    }

                    if (!info.hasConstructor)
                        info.hasSynthCtor = true;

                    // Capture raw implements list from AST when available
                    for (const auto &implQN : classDecl.implementsQualifiedNames)
                    {
                        std::string dotted;
                        for (size_t i = 0; i < implQN.size(); ++i)
                        {
                            if (i) dotted.push_back('.');
                            dotted += implQN[i];
                        }
                        if (!dotted.empty())
                            info.rawImplements.push_back(std::move(dotted));
                    }

                    index.classes()[info.qualifiedName] = std::move(info);
                    break;
                }
                default:
                    break;
            }
        }
    };

    scan(program.main);

    // Phase 1b: scan interfaces and assign stable IDs
    auto joinQualified = [](const std::vector<std::string> &segs) {
        std::string out;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            if (i) out.push_back('.');
            out += segs[i];
        }
        return out;
    };

    // We reuse the same scanner but intercept InterfaceDecl as we recurse bodies
    std::function<void(const std::vector<StmtPtr>&)> scanInterfaces;
    scanInterfaces = [&](const std::vector<StmtPtr> &stmts) {
        for (const auto &stmt : stmts)
        {
            if (!stmt) continue;
            if (auto *ns = dynamic_cast<const NamespaceDecl *>(stmt.get()))
            {
                scanInterfaces(ns->body);
                continue;
            }
            if (auto *idecl = dynamic_cast<const InterfaceDecl *>(stmt.get()))
            {
                InterfaceInfo ii;
                ii.qualifiedName = joinQualified(idecl->qualifiedName);
                if (ii.qualifiedName.empty())
                    continue;
                ii.ifaceId = index.allocateInterfaceId();
                // Collect method slots in declaration order
                std::unordered_set<std::string> seen;
                SemanticDiagnostics sde(*emitter);
                for (const auto &mem : idecl->members)
                {
                    if (!mem) continue;
                    if (auto *md = dynamic_cast<const MethodDecl *>(mem.get()))
                    {
                        if (seen.count(md->name))
                        {
                            sde.emit(diag::BasicDiag::IfaceDupMethod,
                                     md->loc,
                                     static_cast<uint32_t>(md->name.size()),
                                     { {"method", md->name}, {"iface", ii.qualifiedName} });
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
                index.interfacesByQname()[ii.qualifiedName] = std::move(ii);
            }
        }
    };
    scanInterfaces(program.main);

    // Helper to resolve a raw base name against current namespace prefix.
    auto resolveBase = [&](const std::string &classQ, const std::string &raw) -> std::string {
        if (raw.empty())
            return {};
        // Already qualified?
        if (raw.find('.') != std::string::npos)
        {
            if (index.classes().count(raw))
                return raw;
        }
        // Try sibling in same namespace as classQ.
        auto lastDot = classQ.rfind('.');
        std::string prefix = (lastDot == std::string::npos) ? std::string{} : classQ.substr(0, lastDot);
        std::string candidate = prefix.empty() ? raw : (prefix + "." + raw);
        if (index.classes().count(candidate))
            return candidate;
        // Fallback to raw as top-level name.
        if (index.classes().count(raw))
            return raw;
        return {};
    };

    // Phase 2: resolve bases and detect missing bases; collect implements list.
    for (auto &entry : index.classes())
    {
        ClassInfo &ci = entry.second;
        auto it = rawBases.find(ci.qualifiedName);
        if (it != rawBases.end())
        {
            const std::string &raw = it->second.first;
            std::string resolved = resolveBase(ci.qualifiedName, raw);
            if (resolved.empty())
            {
                if (emitter)
                {
                    std::string msg = std::string("base class not found: '") + raw + "'";
                    emitter->emit(il::support::Severity::Error, "B2101", it->second.second, 1, std::move(msg));
                }
            }
            ci.baseQualified = std::move(resolved);
        }

        // Resolve implemented interfaces against class namespace
        if (!ci.rawImplements.empty())
        {
            auto resolveIface = [&](const std::string &raw) -> std::string {
                if (raw.find('.') != std::string::npos)
                {
                    if (index.interfacesByQname().count(raw))
                        return raw;
                }
                auto lastDot = ci.qualifiedName.rfind('.');
                std::string prefix = (lastDot == std::string::npos) ? std::string{} : ci.qualifiedName.substr(0, lastDot);
                std::string candidate = prefix.empty() ? raw : (prefix + "." + raw);
                if (index.interfacesByQname().count(candidate))
                    return candidate;
                if (index.interfacesByQname().count(raw))
                    return raw;
                return {};
            };
            for (const auto &raw : ci.rawImplements)
            {
                std::string resolved = resolveIface(raw);
                if (!resolved.empty())
                {
                    const auto &iface = index.interfacesByQname().at(resolved);
                    ci.implementedInterfaces.push_back(iface.ifaceId);
                }
            }
        }
    }

    // Phase 3: detect inheritance cycles via DFS over baseQualified edges.
    enum State : uint8_t
    {
        kUnvisited = 0,
        kVisiting = 1,
        kVisited = 2,
    };
    std::unordered_map<std::string, State> state;
    state.reserve(index.classes().size());

    std::function<void(const std::string &)> detectCycle;
    detectCycle = [&](const std::string &name) {
        auto it = state.find(name);
        if (it != state.end() && it->second != kUnvisited)
            return; // already processed or in-progress handled by below
        state[name] = kVisiting;
        auto *cls = index.findClass(name);
        if (cls && !cls->baseQualified.empty())
        {
            auto st = state[cls->baseQualified];
            if (st == kVisiting)
            {
                // cycle detected
                if (emitter)
                {
                    std::string msg = std::string("inheritance cycle involving '") + name + "'";
                    emitter->emit(il::support::Severity::Error, "B2102", cls->loc, 1, std::move(msg));
                }
                // Break the cycle to avoid cascading issues.
                cls->baseQualified.clear();
            }
            else if (st == kUnvisited)
            {
                detectCycle(cls->baseQualified);
            }
        }
        state[name] = kVisited;
    };

    for (auto &entry : index.classes())
        detectCycle(entry.first);

    // Phase 4: build vtables and override checks in topological order.
    std::unordered_map<std::string, bool> processed;
    processed.reserve(index.classes().size());

    auto findInBases = [&](const std::string &startClass, const std::string &methodName)
        -> std::pair<ClassInfo *, ClassInfo::MethodInfo *>
    {
        ClassInfo *cur = index.findClass(startClass);
        while (cur && !cur->baseQualified.empty())
        {
            ClassInfo *base = index.findClass(cur->baseQualified);
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
    build = [&](const std::string &name) {
        if (processed[name])
            return;
        ClassInfo *ci = index.findClass(name);
        if (!ci)
            return;
        // Ensure base built first
        if (!ci->baseQualified.empty())
            build(ci->baseQualified);

        // Inherit base vtable
        std::vector<std::string> vtable;
        if (!ci->baseQualified.empty())
        {
            ClassInfo *base = index.findClass(ci->baseQualified);
            if (base)
            {
                vtable = base->vtable; // copy
                // Copy inherited abstractness if not overridden
                for (const auto &mname : base->vtable)
                {
                    auto bit = base->methods.find(mname);
                    if (bit != base->methods.end())
                    {
                        const auto &bm = bit->second;
                        if (bm.isAbstract && ci->methods.find(mname) == ci->methods.end())
                        {
                            ci->isAbstract = true;
                        }
                    }
                }
            }
        }

        // Assign slots and validate overrides
        for (auto &mp : ci->methods)
        {
            const std::string &mname = mp.first;
            auto &mi = mp.second;
            // Non-virtual and non-override
            if (!mi.isVirtual)
                continue;

            if (mi.isAbstract)
                ci->isAbstract = true;

            if (auto [base, bmi] = findInBases(name, mname); bmi != nullptr)
            {
                // Found in base; must be override-compatible.
                if (bmi->slot < 0)
                {
                    if (emitter)
                        emitter->emit(il::support::Severity::Error,
                                      "B2104",
                                      ci->methodLocs[mname],
                                      static_cast<uint32_t>(mname.size()),
                                      std::string("cannot override non-virtual '") + mname + "'");
                }
                else
                {
                    // Check final
                    if (bmi->isFinal && emitter)
                    {
                        emitter->emit(il::support::Severity::Error,
                                      "B2107",
                                      ci->methodLocs[mname],
                                      static_cast<uint32_t>(mname.size()),
                                      std::string("cannot override final '") + mname + "'");
                    }
                    // Signature check
                    const MethodSig &s1 = mi.sig;
                    const MethodSig &s2 = bmi->sig;
                    bool sigOk = (s1.paramTypes == s2.paramTypes) && (s1.returnType == s2.returnType);
                    if (!sigOk && emitter)
                    {
                        emitter->emit(il::support::Severity::Error,
                                      "B2103",
                                      ci->methodLocs[mname],
                                      static_cast<uint32_t>(mname.size()),
                                      std::string("override signature mismatch for '") + mname + "'");
                    }
                    // Reuse slot
                    mi.slot = bmi->slot;
                    if (mi.slot >= 0 && static_cast<std::size_t>(mi.slot) < vtable.size())
                        vtable[mi.slot] = mname;
                }
            }
            else
            {
                // New virtual method; assign fresh slot
                mi.slot = static_cast<int>(vtable.size());
                vtable.push_back(mname);
            }
        }

        ci->vtable = std::move(vtable);
        processed[name] = true;
    };

    for (auto &entry : index.classes())
        build(entry.first);

    // Phase 5: Interface conformance checks
    auto findMethodInClassOrBases = [&](const std::string &classQ,
                                        const std::string &name) -> const ClassInfo::MethodInfo *
    {
        const ClassInfo *cur = index.findClass(classQ);
        if (!cur) return nullptr;
        if (auto it = cur->methods.find(name); it != cur->methods.end())
            return &it->second;
        while (cur && !cur->baseQualified.empty())
        {
            cur = index.findClass(cur->baseQualified);
            if (!cur) break;
            if (auto it2 = cur->methods.find(name); it2 != cur->methods.end())
                return &it2->second;
        }
        return nullptr;
    };

    auto sigsMatch = [](const MethodSig &cls, const IfaceMethodSig &iface) {
        if (cls.paramTypes != iface.paramTypes)
            return false;
        if (cls.returnType.has_value() != iface.returnType.has_value())
            return false;
        if (cls.returnType && iface.returnType && *cls.returnType != *iface.returnType)
            return false;
        return true;
    };

    for (auto &entry2 : index.classes())
    {
        ClassInfo &ci = entry2.second;
        if (ci.implementedInterfaces.empty())
            continue;

        // Build reverse lookup: interface id -> InterfaceInfo
        std::unordered_map<int, const InterfaceInfo *> idToIface;
        for (const auto &p : index.interfacesByQname())
            idToIface[p.second.ifaceId] = &p.second;

        SemanticDiagnostics sde(*emitter);
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
                const ClassInfo::MethodInfo *mi = findMethodInClassOrBases(ci.qualifiedName, slotSig.name);
                if (!mi)
                {
                    ci.isAbstract = true;
                    // If class was not previously abstract, emit error
                    if (!wasAbstract && emitter)
                    {
                        sde.emit(diag::BasicDiag::ClassMissesIfaceMethod,
                                 ci.loc,
                                 static_cast<uint32_t>(ci.name.size()),
                                 { {"class", ci.qualifiedName}, {"iface", iface.qualifiedName}, {"method", slotSig.name} });
                    }
                    continue;
                }
                // signature match
                if (!sigsMatch(mi->sig, slotSig))
                {
                    ci.isAbstract = true;
                    if (!wasAbstract && emitter)
                    {
                        sde.emit(diag::BasicDiag::ClassMissesIfaceMethod,
                                 ci.loc,
                                 static_cast<uint32_t>(ci.name.size()),
                                 { {"class", ci.qualifiedName}, {"iface", iface.qualifiedName}, {"method", slotSig.name} });
                    }
                    continue;
                }
                mapping[slot] = slotSig.name;
            }
            ci.ifaceSlotImpl[ifaceId] = std::move(mapping);
        }
    }
}

} // namespace il::frontends::basic

namespace il::frontends::basic
{

int getVirtualSlot(const OopIndex &index, const std::string &qualifiedClass, const std::string &methodName)
{
    const ClassInfo *ci = index.findClass(qualifiedClass);
    if (!ci)
        return -1;
    auto it = ci->methods.find(methodName);
    if (it == ci->methods.end())
        return -1;
    return it->second.slot;
}

} // namespace il::frontends::basic
