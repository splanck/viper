//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/Diagnostics.cpp
// Purpose: Implementation of the target-independent diagnostic sink.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/Diagnostics.hpp"

#include <ostream>
#include <utility>

namespace viper::codegen::common
{

void Diagnostics::error(std::string message)
{
    errors_.push_back(std::move(message));
}

void Diagnostics::warning(std::string message)
{
    warnings_.push_back(std::move(message));
}

bool Diagnostics::hasErrors() const noexcept
{
    return !errors_.empty();
}

bool Diagnostics::hasWarnings() const noexcept
{
    return !warnings_.empty();
}

const std::vector<std::string> &Diagnostics::errors() const noexcept
{
    return errors_;
}

const std::vector<std::string> &Diagnostics::warnings() const noexcept
{
    return warnings_;
}

void Diagnostics::flush(std::ostream &err, std::ostream *warn) const
{
    for (const auto &msg : errors_)
    {
        err << msg;
        if (!msg.empty() && msg.back() != '\n')
        {
            err << '\n';
        }
    }
    if (warn != nullptr)
    {
        for (const auto &msg : warnings_)
        {
            *warn << msg;
            if (!msg.empty() && msg.back() != '\n')
            {
                *warn << '\n';
            }
        }
    }
}

} // namespace viper::codegen::common
