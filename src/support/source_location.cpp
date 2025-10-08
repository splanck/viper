//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the trivial inline utilities for the SourceLoc value type.  A
// location is considered valid when it refers to a registered file identifier
// and carries 1-based line and column information.  Utilities are implemented
// out-of-line to keep the header lightweight for inclusion across the project.
//
//===----------------------------------------------------------------------===//

#include "support/source_location.hpp"

namespace il::support
{
/// @brief Determine whether the location carries a meaningful file reference.
///
/// Source identifiers are allocated by SourceManager starting at one.  The
/// sentinel identifier zero represents an unknown origin.  The helper provides
/// a convenient way for callers to guard formatting logic on the presence of a
/// resolved location.
///
/// @return True when the location originated from a tracked source file.
bool SourceLoc::isValid() const
{
    return file_id != 0;
}
} // namespace il::support

