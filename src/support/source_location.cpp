/**
 * @file source_location.cpp
 * @brief Defines helpers for dealing with source file coordinates.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     Source locations are represented as lightweight value types.  A zero
 *     `file_id` denotes an unknown origin; positive identifiers reference paths
 *     stored in the `SourceManager`.
 */

#include "support/source_location.hpp"

namespace il::support
{
/**
 * @brief Reports whether the location refers to a known source file.
 *
 * The check simply tests the stored `file_id` against zero.  When the location
 * was created from a `SourceManager`, the identifier is a 1-based index into the
 * manager's file table; otherwise it remains zero to indicate an unspecified
 * origin.
 *
 * @return `true` when the location carries a valid file identifier.
 */
bool SourceLoc::isValid() const
{
    return file_id != 0;
}
} // namespace il::support

