#include "tui/term/term_io.hpp"
#include <cstdio>

namespace tui::term
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

} // namespace tui::term
