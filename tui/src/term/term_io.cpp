// tui/src/term/term_io.cpp
// @brief Implements TermIO backends.
// @invariant RealTermIO writes to std::cout; StringTermIO buffers internally.
// @ownership RealTermIO has no ownership; StringTermIO owns its buffer.

#include "tui/term/term_io.hpp"

#include <iostream>

namespace viper::tui::term
{

void RealTermIO::write(std::string_view data)
{
#ifdef _WIN32
    // Future: ensure stdout is in binary mode.
#endif
    std::cout.rdbuf()->sputn(data.data(), static_cast<std::streamsize>(data.size()));
}

void RealTermIO::flush()
{
    std::cout.flush();
}

void StringTermIO::write(std::string_view data)
{
    buffer_.append(data.data(), data.size());
}

void StringTermIO::flush()
{
    // No-op for in-memory buffer.
}

} // namespace viper::tui::term
