#pragma once
#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#define VIPERTUI_POSIX 1
#include <termios.h>
#include <unistd.h>
#else
#define VIPERTUI_POSIX 0
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace viper::tui
{

class TerminalSession
{
  public:
    TerminalSession();
    ~TerminalSession();

    TerminalSession(const TerminalSession &) = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;

    bool active() const;

  private:
    bool active_{false};
#if VIPERTUI_POSIX
    termios orig_{};
#endif
#if defined(_WIN32)
    DWORD orig_out_mode_{0};
#endif
};

} // namespace viper::tui
