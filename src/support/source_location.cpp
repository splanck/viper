//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the minimal out-of-line utilities for the SourceLoc value type.  A
// location is considered valid when it refers to a registered file identifier;
// line and column components are optional and surfaced through `hasLine()` and
// `hasColumn()` respectively.  Keeping the helper here avoids inlining the
// check into every translation unit that includes the header while preserving a
// central explanation of the validity contract.
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
///          source locations. Clients are responsible for ensuring the
///          begin/end ordering when constructing the range; this helper only
///          checks that both endpoints came from the source manager.  It is used
///          extensively by syntax highlighters and diagnostics to decide whether
///          to underline spans of text.
///
/// @return True when both @ref begin and @ref end carry valid file ids.
bool SourceRange::isValid() const
{
    if (!begin.isValid() || !end.isValid())
    {
        return false;
    }

    if (begin.file_id != end.file_id)
    {
        return false;
    }

    const bool have_line_info = begin.line != 0 && end.line != 0;
    if (have_line_info && begin.line > end.line)
    {
        return false;
    }

    const bool have_column_info =
        have_line_info && begin.line == end.line && begin.column != 0 && end.column != 0;
    if (have_column_info && begin.column > end.column)
    {
        return false;
    }

    return true;
}
} // namespace il::support
