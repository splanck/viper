//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lower_OOP_Scan.cpp
// Purpose: Analyse BASIC OOP constructs to compute class layouts and runtime
//          feature requirements before lowering begins.
// Invariants:
//   * Field offsets remain 8-byte aligned to satisfy runtime allocation
//     expectations.
//   * Every runtime helper request is recorded prior to emission so the
//     lowerer can gate helper generation on demand.
// Ownership: The scanner borrows AST nodes and Lowerer state; no persistent
//            resources are retained after the pass finishes.
// Links: docs/codemap.md
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

/// @brief Round a byte offset up to the requested alignment.
///
/// @details Uses standard power-of-two alignment math to compute the next
///          aligned address greater than or equal to @p value.  The helper lives
///          in an anonymous namespace so both field layout and class size
///          calculations share the same semantics.
///
/// @param value Current byte offset.
/// @param alignment Alignment requirement expressed as a power of two.
/// @return Smallest aligned offset not less than @p value.
[[nodiscard]] std::size_t alignTo(std::size_t value, std::size_t alignment) noexcept
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

/// @brief Determine the storage size of a class field expressed in BASIC types.
///
/// @details Strings and object references occupy a pointer-sized slot while
///          numeric types are sized explicitly according to their runtime
///          representation.  The sizing logic mirrors the runtime bridge to keep
///          layout calculations consistent across components.
///
/// @param type BASIC field type to measure.
/// @return Byte width reserved for the field.
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
/// @brief Construct a @ref Lowerer::ClassLayout from a sequence of field nodes.
///
/// @details Iterates through the provided range, aligns each field to
///          @c kFieldAlignment, computes offsets, and records both the ordered
///          field vector and name-to-index map.  The final class size is rounded
///          up to the same alignment so runtime allocations have predictable
///          padding.
///
/// @tparam FieldRange Iterable type exposing `name` and `type` members.
/// @param fields Source range describing the class fields.
/// @return Populated layout ready for storage on the lowerer.
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

/// @brief AST walker that records class layouts and runtime feature usage.
class OopScanWalker final : public BasicAstWalker<OopScanWalker>
{
  public:
    /// @brief Create a walker bound to the lowering state it will populate.
    ///
    /// @param lowerer Lowerer that owns class layout caches and feature flags.
    explicit OopScanWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Traverse the full BASIC program to discover OOP requirements.
    ///
    /// @details Visits all procedure declarations and main-program statements,
    ///          collecting class layouts and runtime feature requests as it
    ///          encounters relevant constructs.  Null AST pointers are skipped to
    ///          guard against partially constructed trees.
    ///
    /// @param prog Parsed BASIC program root.
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

    /// @brief Finalise a class declaration by recording its computed layout.
    ///
    /// @details Builds a layout for the class fields, assigns a monotonically
    ///          increasing class identifier, and stores the result for the
    ///          caller to consume after traversal completes.
    ///
    /// @param decl Class declaration AST node being exited.
    void after(const ClassDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(decl.name, std::move(layout));
    }

    /// @brief Record the layout associated with a BASIC @c TYPE declaration.
    ///
    /// @details Mirrors @ref after(const ClassDecl &) so user-defined type
    ///          aliases participate in the same runtime metadata table as class
    ///          declarations.  Layouts reuse the class identifier sequence so
    ///          runtime helpers can treat both uniformly.
    ///
    /// @param decl Type declaration node.
    void after(const TypeDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(decl.name, std::move(layout));
    }

    /// @brief Note that object allocations require the runtime allocation helper.
    ///
    /// @details Requests @ref il::runtime::RuntimeFeature::ObjNew so the
    ///          lowering phase emits the appropriate runtime declarations prior
    ///          to code generation.
    ///
    /// @param unused AST node for the allocation (unused).
    void after(const NewExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjNew);
    }

    /// @brief Track runtime helpers necessary for method invocations.
    ///
    /// @details Method calls retain the receiver and release the returned
    ///          object if necessary.  The walker therefore marks both retain and
    ///          release helpers as required so the emitter can insert the calls
    ///          later.
    ///
    /// @param unused Method call expression (unused).
    void after(const MethodCallExpr &)
    {
        using Feature = il::runtime::RuntimeFeature;
        lowerer_.requestRuntimeFeature(Feature::ObjRetainMaybe);
        lowerer_.requestRuntimeFeature(Feature::ObjReleaseChk0);
    }

    /// @brief Ensure member access emits the retain helper when needed.
    ///
    /// @details Field reads may return object references that require balanced
    ///          retains when exposed to user code.  Marking the retain helper
    ///          keeps the lowering phase consistent with runtime expectations.
    ///
    /// @param unused Member access expression (unused).
    void after(const MemberAccessExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjRetainMaybe);
    }

    /// @brief Flag the runtime free helper when @c DELETE statements appear.
    ///
    /// @details Deletion of objects requires a call into the runtime so the
    ///          walker records the dependency ahead of time.
    ///
    /// @param unused Delete statement node (unused).
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

/// @brief Compute class layouts and runtime feature flags for a BASIC program.
///
/// @details Clears any previously cached layouts, runs an @ref OopScanWalker to
///          gather information from the AST, and installs the resulting layout
///          table on the lowerer.  Subsequent lowering stages consult this cache
///          when emitting object operations.
///
/// @param prog Program AST to analyse.
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

