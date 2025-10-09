//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helpers that parse `--break` specifications for the ilc command
// line. These helpers accept either label names or `file:line` breakpoints and
// perform the minimal validation used by the debugger entry point.
//
//===----------------------------------------------------------------------===//

#include "break_spec.hpp"

#include <cctype>

namespace ilc
{

/// @brief Determine whether a breakpoint specification targets a source file.
///
/// Source breakpoints are encoded as `path:line` with a positive integer line
/// number. The helper confirms the colon is present, all characters after the
/// colon are digits, and that the portion before the colon resembles a path so
/// label-style breakpoints are ignored.
///
/// @param spec Command-line argument to analyse.
/// @return True when @p spec names a source breakpoint; false otherwise.
bool isSrcBreakSpec(const std::string &spec)
{
    // Position of the colon separating file and line.
    auto pos = spec.rfind(':');
    if (pos == std::string::npos || pos + 1 >= spec.size())
        return false;

    // Ensure all characters after the colon are digits.
    for (std::string::size_type i = pos + 1; i < spec.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(spec[i])))
            return false;
    }

    // Portion before the colon; treated as a file path candidate.
    std::string left = spec.substr(0, pos);
    return left.find('/') != std::string::npos || left.find('\\') != std::string::npos ||
           left.find('.') != std::string::npos;
}

} // namespace ilc

