// File: src/tools/basic/common.cpp
// Purpose: Implement shared helpers for BASIC command-line tools.
// Key invariants: Diagnostics must match legacy tool output exactly.
// Ownership/Lifetime: Functions operate on caller-provided buffers and managers.
// License: MIT License. See LICENSE in the project root for full license information.
// Links: docs/codemap.md

#include "tools/basic/common.hpp"

#include "support/source_manager.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace il::tools::basic
{

namespace
{
#ifdef VIPER_BASIC_TOOL_USAGE
constexpr const char *kUsageMessage = VIPER_BASIC_TOOL_USAGE;
#else
#error "VIPER_BASIC_TOOL_USAGE must be defined for BASIC tool builds"
#endif
} // namespace

std::optional<std::uint32_t> loadBasicSource(const char *path,
                                             std::string &buffer,
                                             il::support::SourceManager &sm)
{
    if (path == nullptr)
    {
        std::cerr << kUsageMessage;
        return std::nullopt;
    }

    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "cannot open " << path << "\n";
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    buffer = ss.str();

    std::uint32_t fileId = sm.addFile(path);
    return fileId;
}

} // namespace il::tools::basic
