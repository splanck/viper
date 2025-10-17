//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the minimal out-of-line utilities for the SourceLoc value type.  A
// location is considered valid when it refers to a registered file identifier
// and carries 1-based line and column information.  Keeping the helper here
// avoids inlining the check into every translation unit that includes the
// header while preserving a central explanation of the validity contract.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements validity queries for `SourceLoc` and `SourceRange`.
/// @details The majority of `SourceLoc` operations are inline, but the validity
///          predicates live out-of-line to centralise their documentation and
///          keep translation units lightweight.  These helpers are heavily used
///          when printing diagnostics or deciding whether to attach source
///          locations to serialized IL entities.

#include "support/source_location.hpp"

namespace il::support
{
/// @brief Determine whether the location carries a real source attachment.
///
/// @details SourceManager dispenses monotonically increasing identifiers for
///          every file registered with the compiler.  The default-constructed
///          location uses zero to mark "unknown".  By testing the stored
///          identifier against zero, the helper distinguishes between genuine,
///          user-authored locations and synthesized values, enabling
///          diagnostics and serializers to elide missing information.
///
/// @return True when the location originated from a tracked source file.
bool SourceLoc::isValid() const
{
    return file_id != 0;
}

/// @brief Determine whether the range refers to a concrete span of source.
///
/// @details The range is considered valid when both endpoints identify tracked
///          source locations, originate from the same file identifier, and the
///          @ref begin endpoint does not follow @ref end.  The ordering check
///          first compares lines before columns, enabling zero-width ranges to
///          remain valid so long as they lie within the same file.  The stricter
///          validation matches the invariants documented on @ref SourceRange and
///          prevents diagnostics from attempting to print inverted spans.
///
/// @return True when @ref begin and @ref end reference an ordered span inside a
///         single tracked file.
bool SourceRange::isValid() const
{
    if (!begin.isValid() || !end.isValid())
        return false;

    if (begin.file_id != end.file_id)
        return false;

    if (begin.line > end.line)
        return false;

    if (begin.line == end.line && begin.column > end.column)
        return false;

    return true;
}
} // namespace il::support

