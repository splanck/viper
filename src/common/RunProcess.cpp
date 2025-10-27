//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the helper used by CLI utilities to launch external processes.
// The routine builds a shell command line from argv fragments, invokes the
// platform's `popen` facility, and collects stdout for diagnostic reporting.
// Centralising the logic keeps process spawning consistent across developer
// tools.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Subprocess launcher shared by Viper developer tools.
/// @details Provides @ref run_process, which constructs a shell command string
///          from argument fragments, captures output, and reports the resulting
///          exit status in a cross-platform manner.

#include "common/RunProcess.hpp"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#    define POPEN _popen
#    define PCLOSE _pclose
#else
#    define POPEN popen
#    define PCLOSE pclose
#endif

namespace
{
#if defined(_WIN32)
std::string quote_windows_argument(const std::string &arg)
{
    std::string quoted;
    quoted.reserve(arg.size() + 2);
    quoted.push_back('"');

    std::size_t backslashCount = 0;
    for (const char ch : arg)
    {
        if (ch == '\\')
        {
            ++backslashCount;
            continue;
        }

        if (ch == '"')
        {
            quoted.append(backslashCount * 2 + 1, '\\');
            quoted.push_back('"');
            backslashCount = 0;
            continue;
        }

        if (backslashCount != 0)
        {
            quoted.append(backslashCount, '\\');
            backslashCount = 0;
        }

        quoted.push_back(ch);
    }

    if (backslashCount != 0)
    {
        quoted.append(backslashCount * 2, '\\');
    }

    quoted.push_back('"');
    return quoted;
}
#else
std::string quote_posix_argument(const std::string &arg)
{
    std::string quoted;
    quoted.reserve(arg.size() + 2);
    quoted.push_back('"');

    for (const char ch : arg)
    {
        if (ch == '\\' || ch == '"')
        {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }

    quoted.push_back('"');
    return quoted;
}
#endif
} // namespace

/// @brief Launch a subprocess using the host shell and capture its output.
/// @details Joins the provided @p argv fragments into a quoted command string,
///          spawns it via @c popen, and streams stdout into @ref RunResult::output
///          while recording the exit status when available.  The @p cwd and
///          @p env parameters are currently ignored, matching the previous
///          behaviour of the helper.
RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd,
                      const std::vector<std::pair<std::string, std::string>> &env)
{
    (void)env; // Environment forwarding is not yet implemented.

    std::string cmd;
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
        if (i != 0)
        {
            cmd += ' ';
        }
#if defined(_WIN32)
        cmd += quote_windows_argument(argv[i]);
#else
        cmd += quote_posix_argument(argv[i]);
#endif
    }
#ifndef _WIN32
    cmd += " 2>&1";
#endif

    if (cwd)
    {
#ifdef _WIN32
        // Windows popen lacks a straightforward chdir hook; callers should adjust when needed.
        (void)cwd;
#else
        (void)cwd;
#endif
    }

    RunResult rr{0, "", ""};
    FILE *pipe = POPEN(cmd.c_str(), "r");
    if (!pipe)
    {
        rr.exit_code = -1;
        rr.err = "failed to popen";
        return rr;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe))
    {
        rr.out += buffer;
    }

    rr.exit_code = PCLOSE(pipe);
#ifndef _WIN32
    // When stderr is redirected to stdout the captured text lives in `out`.
    rr.err = rr.out;
#endif
    return rr;
}

#undef POPEN
#undef PCLOSE
