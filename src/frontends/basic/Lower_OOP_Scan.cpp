// File: src/frontends/basic/Lower_OOP_Scan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements optional OOP scanning for class layouts and runtime helpers.
// Key invariants: Field offsets are 8-byte aligned and runtime features are recorded before
// emission. Ownership/Lifetime: Borrows AST nodes and Lowerer state; owns no persistent resources.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace il::frontends::basic
{
namespace
{
constexpr std::size_t kFieldAlignment = 8;
constexpr std::size_t kPointerSize = sizeof(void *);

[[nodiscard]] std::size_t alignTo(std::size_t value, std::size_t alignment) noexcept
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

[[nodiscard]] std::size_t fieldSize(Type type) noexcept
{
    switch (type)
    {
        case Type::Str:
            return kPointerSize;
        case Type::F64:
            return 8;
        case Type::Bool:
            return 1;
        case Type::I64:
        default:
            return 8;
    }
}

template <typename FieldRange>
[[nodiscard]] Lowerer::ClassLayout buildLayout(const FieldRange &fields)
{
    Lowerer::ClassLayout layout;
    std::size_t offset = 0;
    for (const auto &field : fields)
    {
        offset = alignTo(offset, kFieldAlignment);
        Lowerer::ClassLayout::Field info{};
        info.name = field.name;
        info.type = field.type;
        info.offset = offset;
        info.size = fieldSize(field.type);
        layout.fields.push_back(std::move(info));
        layout.fieldIndex.emplace(layout.fields.back().name, layout.fields.size() - 1);
        offset += layout.fields.back().size;
    }
    layout.size = alignTo(offset, kFieldAlignment);
    return layout;
}

class OopScanWalker final : public BasicAstWalker<OopScanWalker>
{
  public:
    explicit OopScanWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void evaluateProgram(const Program &prog)
    {
        for (const auto &decl : prog.procs)
        {
            if (!decl)
                continue;
            decl->accept(*this);
        }
        for (const auto &stmt : prog.main)
        {
            if (!stmt)
                continue;
            stmt->accept(*this);
        }
    }

    void after(const ClassDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(decl.name, std::move(layout));
    }

    void after(const TypeDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(decl.name, std::move(layout));
    }

    void after(const NewExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjNew);
    }

    void after(const MethodCallExpr &)
    {
        using Feature = il::runtime::RuntimeFeature;
        lowerer_.requestRuntimeFeature(Feature::ObjRetainMaybe);
        lowerer_.requestRuntimeFeature(Feature::ObjReleaseChk0);
    }

    void after(const MemberAccessExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjRetainMaybe);
    }

    void after(const DeleteStmt &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjFree);
    }

    std::vector<std::pair<std::string, Lowerer::ClassLayout>> layouts;

  private:
    Lowerer &lowerer_;
    std::int64_t nextClassId_{1};
};
} // namespace

void Lowerer::scanOOP(const Program &prog)
{
    classLayouts_.clear();

    OopScanWalker walker(*this);
    walker.evaluateProgram(prog);

    for (auto &entry : walker.layouts)
    {
        classLayouts_.emplace(std::move(entry.first), std::move(entry.second));
    }
}

} // namespace il::frontends::basic

