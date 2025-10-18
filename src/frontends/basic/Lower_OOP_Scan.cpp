// File: src/frontends/basic/Lower_OOP_Scan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements OOP-aware scanning to prepare class layouts and runtime helpers.
// Key invariants: Layout computation aligns fields to 8 bytes and preserves field order.
// Ownership/Lifetime: Operates on Lowerer state without owning AST nodes.
// Links: docs/codemap.md
 
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/AstWalker.hpp"

#if VIPER_ENABLE_OOP

#include <cstddef>
#include <utility>

namespace il::frontends::basic
{

namespace
{
constexpr std::size_t kPointerSize = 8;
constexpr std::size_t kPointerAlignment = 8;

[[nodiscard]] std::size_t alignTo(std::size_t value, std::size_t alignment) noexcept
{
    if (alignment == 0)
        return value;
    const std::size_t remainder = value % alignment;
    if (remainder == 0)
        return value;
    return value + (alignment - remainder);
}

[[nodiscard]] std::size_t fieldStorageSize(Type type) noexcept
{
    switch (type)
    {
        case Type::Str:
            return kPointerSize;
        case Type::Bool:
            return 1;
        case Type::I64:
        case Type::F64:
        default:
            return 8;
    }
}
} // namespace

void Lowerer::scanOOP(const Program &prog)
{
    classLayouts_.clear();

    class OopScanWalker final : public BasicAstWalker<OopScanWalker>
    {
      public:
        OopScanWalker(Lowerer &lowerer, std::unordered_map<std::string, ClassLayout> &layouts)
            : lowerer_(lowerer), layouts_(layouts)
        {
        }

        void evaluateProgram(const Program &program)
        {
            for (const auto &decl : program.procs)
            {
                if (decl)
                    decl->accept(*this);
            }
            for (const auto &stmt : program.main)
            {
                if (stmt)
                    stmt->accept(*this);
            }
        }

        void before(const ClassDecl &decl)
        {
            recordLayout(decl, ClassLayout::Kind::Class);
        }

        void before(const TypeDecl &decl)
        {
            recordLayout(decl, ClassLayout::Kind::Type);
        }

        void before(const NewExpr &)
        {
            lowerer_.requestHelper(RuntimeFeature::ObjNew);
        }

        void before(const MethodCallExpr &)
        {
            lowerer_.requestHelper(RuntimeFeature::ObjRetainMaybe);
        }

        void before(const MemberAccessExpr &)
        {
            lowerer_.requestHelper(RuntimeFeature::ObjRetainMaybe);
        }

        void before(const DeleteStmt &)
        {
            lowerer_.requestHelper(RuntimeFeature::ObjReleaseChk0);
            lowerer_.requestHelper(RuntimeFeature::ObjFree);
        }

      private:
        template <typename Decl>
        void recordLayout(const Decl &decl, ClassLayout::Kind kind)
        {
            ClassLayout layout;
            layout.name = decl.name;
            layout.kind = kind;
            if (kind == ClassLayout::Kind::Class)
                layout.classId = nextClassId_++;

            layout.fields.reserve(decl.fields.size());
            std::size_t offset = 0;
            for (const auto &field : decl.fields)
            {
                offset = alignTo(offset, kPointerAlignment);
                ClassLayout::Field fieldLayout;
                fieldLayout.name = field.name;
                fieldLayout.type = field.type;
                fieldLayout.offset = offset;
                fieldLayout.size = fieldStorageSize(field.type);
                layout.fields.push_back(std::move(fieldLayout));
                offset += fieldLayout.size;
            }
            layout.size = alignTo(offset, kPointerAlignment);
            layouts_[layout.name] = std::move(layout);
        }

        Lowerer &lowerer_;
        std::unordered_map<std::string, ClassLayout> &layouts_;
        std::size_t nextClassId_{0};
    };

    OopScanWalker walker(*this, classLayouts_);
    walker.evaluateProgram(prog);
}

} // namespace il::frontends::basic

#endif // VIPER_ENABLE_OOP

