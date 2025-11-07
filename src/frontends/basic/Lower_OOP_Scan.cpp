//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/Lower_OOP_Scan.cpp
//
// Summary:
//   Walks BASIC programs that make use of the optional object-oriented features
//   to precompute class layouts and identify runtime support requirements.  The
//   scan stage separates layout discovery from the lowering pipeline so later
//   phases can simply query cached metadata when emitting IL.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the OOP metadata scan for the BASIC front end.
/// @details Provides helpers to align field offsets, compute layout sizes, and
///          visit the AST to record runtime feature requests.  The resulting
///          @ref Lowerer::ClassLayout table drives later IL emission.

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/IdentifierCase.hpp"
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

/// @brief Round @p value up to the next multiple of @p alignment.
/// @details Uses standard bit-masking to compute the aligned value, assuming the
///          alignment is a power of two.  The helper is marked @c noexcept so it
///          can be used freely during layout computation.
/// @param value Offset being aligned.
/// @param alignment Power-of-two alignment requirement.
/// @return The smallest multiple of @p alignment greater than or equal to @p value.
[[nodiscard]] std::size_t alignTo(std::size_t value, std::size_t alignment) noexcept
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

/// @brief Determine the storage size for a BASIC field type.
/// @details Maps BASIC semantic types to their runtime storage requirements.
///          String fields are treated as pointers to managed buffers, numeric
///          fields use their natural width, and any unrecognised types default
///          to 64 bits so layouts remain conservative.
/// @param type Field type enumerator.
/// @return Size in bytes required to store the field.
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

/// @brief Construct a class layout description from an iterable field range.
/// @details Iterates over the provided field descriptors, aligning each field,
///          computing its offset/size pair, and inserting lookups into the
///          layout's index map.  The resulting layout describes the packed
///          object representation used by the runtime.
/// @tparam FieldRange Iterable range exposing @c name and @c type members.
/// @param fields Sequence of field metadata describing the class.
/// @return Fully populated @ref Lowerer::ClassLayout.
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
        canonicalizeIdentifierInPlace(info.name);
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

/// @brief AST walker that collects class layout data and runtime feature usage.
/// @details Specialises @ref BasicAstWalker to observe class-like declarations
///          and OOP expressions.  As it walks the program it constructs
///          @ref Lowerer::ClassLayout instances and records runtime dependencies
///          so the lowering phase can react accordingly.
class OopScanWalker final : public BasicAstWalker<OopScanWalker>
{
  public:
    /// @brief Create a walker bound to the lowering state being populated.
    /// @param lowerer Lowerer state that accumulates layout and runtime data.
    explicit OopScanWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Traverse a BASIC program to collect OOP metadata.
    /// @details Visits both procedure declarations and main statements so class
    ///          declarations are discovered regardless of placement.  The walker
    ///          delegates to @ref after callbacks to record the actual data.
    /// @param prog Parsed BASIC program.
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

    /// @brief Capture metadata after visiting a class declaration.
    /// @details Builds a @ref Lowerer::ClassLayout from the declaration fields,
    ///          assigns a stable class identifier, and records the result for
    ///          later transfer to the lowering state.
    /// @param decl Class declaration encountered during traversal.
    void after(const ClassDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(canonicalizeIdentifier(decl.name), std::move(layout));
    }

    /// @brief Capture metadata after visiting a type alias that behaves like a class.
    /// @details Treats @ref TypeDecl uniformly with @ref ClassDecl so user-defined
    ///          type declarations participate in object layout computation.
    /// @param decl Type declaration encountered during traversal.
    void after(const TypeDecl &decl)
    {
        auto layout = buildLayout(decl.fields);
        layout.classId = nextClassId_++;
        layouts.emplace_back(canonicalizeIdentifier(decl.name), std::move(layout));
    }

    /// @brief Record the need for object allocation runtime support.
    /// @details Requests the @c ObjNew runtime feature whenever a @c NEW
    ///          expression is observed so the generated program links the
    ///          corresponding runtime helper.
    /// @param unused Ignored expression instance.
    void after(const NewExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjNew);
    }

    /// @brief Record retain/release runtime requirements for method calls.
    /// @details Ensures both conditional retain and checked release helpers are
    ///          linked when the program invokes methods that may manipulate
    ///          object lifetimes.
    /// @param unused Ignored expression instance.
    void after(const MethodCallExpr &)
    {
        using Feature = il::runtime::RuntimeFeature;
        lowerer_.requestRuntimeFeature(Feature::ObjRetainMaybe);
        lowerer_.requestRuntimeFeature(Feature::ObjReleaseChk0);
    }

    /// @brief Track runtime support for member access expressions.
    /// @details Member access can require retaining the receiver object; the
    ///          walker therefore requests the optional retain helper.
    /// @param unused Ignored expression instance.
    void after(const MemberAccessExpr &)
    {
        lowerer_.requestRuntimeFeature(il::runtime::RuntimeFeature::ObjRetainMaybe);
    }

    /// @brief Record runtime support for object destruction statements.
    /// @details Requests the object free helper so generated programs can
    ///          dispose of dynamically allocated instances.
    /// @param unused Ignored statement instance.
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

/// @brief Scan a BASIC program to populate class layout metadata.
/// @details Clears existing layout data, runs the dedicated AST walker to
///          collect new layouts and runtime feature requests, then transfers the
///          gathered layouts into the @ref Lowerer state.  The method forms the
///          bridge between parsing and IL lowering for OOP constructs.
/// @param prog Program to inspect for class and runtime requirements.
void Lowerer::scanOOP(const Program &prog)
{
    classLayouts_.clear();
    oopIndex_.clear();

    buildOopIndex(prog, oopIndex_, nullptr);

    OopScanWalker walker(*this);
    walker.evaluateProgram(prog);

    for (auto &entry : walker.layouts)
    {
        classLayouts_.emplace(std::move(entry.first), std::move(entry.second));
    }
}

} // namespace il::frontends::basic
