//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lower_OOP_Scan.cpp
// Purpose: Implements optional OOP scanning for class layouts and runtime helpers.
// Key invariants: Field offsets are 8-byte aligned and runtime features are recorded before
//                 emission.
// Ownership/Lifetime: Borrows AST nodes and Lowerer state; owns no persistent resources.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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

/// @brief Align a byte offset to the requested boundary.
/// @details Applies the standard power-of-two rounding formula so that class
///          fields respect the runtime's pointer alignment requirements.
/// @param value Offset to adjust.
/// @param alignment Desired alignment, expected to be a power of two.
/// @return @p value rounded up to the next multiple of @p alignment.
[[nodiscard]] std::size_t alignTo(std::size_t value, std::size_t alignment) noexcept
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

/// @brief Compute the number of bytes required for a class field type.
/// @details Consolidates the BASIC scalar types into ABI-compatible sizes so
///          layout calculations can remain in a single, well-tested location.
/// @param type BASIC type whose storage size is required.
/// @return Byte width for the supplied type.
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

/// @brief Build a class layout from the provided field descriptors.
/// @details Iterates each field, aligning offsets, computing sizes, and filling
///          the reverse lookup table used during lowering.  The helper also
///          calculates the total storage size rounded up to the alignment
///          boundary so runtime descriptors can be emitted directly from the
///          result.
/// @tparam FieldRange Iterable range whose elements expose `name` and `type` members.
/// @param fields Sequence of fields to lay out.
/// @return Populated layout describing every field and the overall class size.
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

/// @brief AST walker that records layouts and runtime features for OOP usage.
class OopScanWalker final : public BasicAstWalker<OopScanWalker>
{
  public:
    /// @brief Create a walker bound to the ongoing lowering session.
    /// @param lowerer Lowerer instance whose runtime feature tracking will be updated.
    explicit OopScanWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Walk every procedure and top-level statement in the program.
    /// @details The traversal skips null nodes and delegates into the `after`
    ///          callbacks, which collect layouts and register runtime features.
    /// @param prog Program to inspect for object-oriented constructs.
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

    /// @brief Capture the layout for a BASIC `CLASS` declaration.
    /// @details Builds a class layout, assigns it a unique class identifier, and
    ///          stores the pair for later transfer into the lowering state.
    /// @param decl Class declaration encountered during traversal.
    void after(const ClassDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(decl.name, std::move(layout));
    }

    /// @brief Capture the layout for a BASIC `TYPE` declaration.
    /// @details Shares the same representation rules as @ref ClassDecl, so the
    ///          same layout builder can be reused to populate the cache.
    /// @param decl Type declaration encountered during traversal.
    void after(const TypeDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(decl.name, std::move(layout));
    }

    /// @brief Record that object allocation support is required.
    /// @details Requests the runtime feature responsible for `NEW` operations
    ///          so that lowering can emit the requisite helper call.
    void after(const NewExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjNew);
    }

    /// @brief Record retain/release helpers needed for method calls.
    /// @details Method invocations may produce new object references or release
    ///          existing ones; tracking both helpers ensures reference counts
    ///          remain balanced in emitted code.
    void after(const MethodCallExpr &)
    {
        using Feature = il::runtime::RuntimeFeature;
        lowerer_.requestRuntimeFeature(Feature::ObjRetainMaybe);
        lowerer_.requestRuntimeFeature(Feature::ObjReleaseChk0);
    }

    /// @brief Register retain support required for member access expressions.
    /// @details Accessing object members may produce handles that need to be
    ///          retained lazily; the scan ensures the helper is made available.
    void after(const MemberAccessExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjRetainMaybe);
    }

    /// @brief Record that the `DELETE` runtime helper must be linked.
    /// @details Ensures the lowering pipeline can emit calls that free object
    ///          storage explicitly when encountering delete statements.
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

/// @brief Analyse BASIC program for object-oriented features prior to lowering.
/// @details Clears any previously cached layouts, walks the AST to accumulate
///          class/type descriptions and runtime feature requests, then stores
///          the collected layouts into the lowerer's lookup table for later
///          emission.
/// @param prog Program whose object usage is being inspected.
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

