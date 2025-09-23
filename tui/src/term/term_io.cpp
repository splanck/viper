#include "tui/term/term_io.hpp"
#include <cstdio>

namespace viper::tui::term
{

void RealTermIO::write(std::string_view s)
{
    if (!s.empty())
    {
        std::fwrite(s.data(), 1, s.size(), stdout);
    }
}

void RealTermIO::flush()
{
    std::fflush(stdout);
}

/// @brief Append the provided string view to the in-memory buffer.
void StringTermIO::write(std::string_view s)
{
    buf_.append(s.data(), s.size());
}

/// @brief String-backed term IO has no flushing side effects.
void StringTermIO::flush()
{
}

/// @brief Inspect the accumulated terminal output.
const std::string &StringTermIO::buffer() const
{
    return buf_;
}

/// @brief Clear the captured terminal output buffer.
void StringTermIO::clear()
{
    buf_.clear();
}

} // namespace viper::tui::term
