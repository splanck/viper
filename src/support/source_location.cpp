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

#include "support/source_location.hpp"

namespace il::support
{
/// @brief Determine whether the location carries a real source attachment.
///
/// SourceManager dispenses monotonically increasing identifiers for every file
/// registered with the compiler.  The default-constructed location uses zero to
/// mark "unknown".  By testing the stored identifier against zero, the helper
/// distinguishes between genuine, user-authored locations and synthesized
/// values, enabling diagnostics and serializers to elide missing information.
///
/// @return True when the location originated from a tracked source file.
bool SourceLoc::isValid() const
{
    return file_id != 0;
}
} // namespace il::support

