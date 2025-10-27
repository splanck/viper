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

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#    define NOMINMAX
#    include <windows.h>
#else
#    include <cerrno>
#    include <climits>
#    include <unistd.h>
#endif

#ifndef _WIN32
#    include <sys/wait.h>
#endif

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
///          while recording the exit status when available.  The @p env parameter
///          is honoured by temporarily adjusting the current process environment
///          for the duration of the child invocation.  The @p cwd parameter is
///          presently ignored, matching the previous behaviour of the helper.
namespace
{
class ScopedEnvironmentAssignment
{
public:
    ScopedEnvironmentAssignment(std::string name, std::string value)
        : name_(std::move(name))
        , previous_(capture_existing())
    {
        apply(std::move(value));
    }

    ScopedEnvironmentAssignment(const ScopedEnvironmentAssignment &) = delete;
    ScopedEnvironmentAssignment &operator=(const ScopedEnvironmentAssignment &) = delete;

    ScopedEnvironmentAssignment(ScopedEnvironmentAssignment &&) = default;
    ScopedEnvironmentAssignment &operator=(ScopedEnvironmentAssignment &&) = default;

    ~ScopedEnvironmentAssignment()
    {
        restore();
    }

private:
    std::optional<std::string> capture_existing() const
    {
        const char *existing = std::getenv(name_.c_str());
        if (existing == nullptr)
        {
            return std::nullopt;
        }
        return std::string(existing);
    }

    void apply(std::string value) const
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void restore() const
    {
        if (previous_.has_value())
        {
#ifdef _WIN32
            _putenv_s(name_.c_str(), previous_->c_str());
#else
            setenv(name_.c_str(), previous_->c_str(), 1);
#endif
            return;
        }

#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

class ScopedWorkingDirectory
{
public:
    explicit ScopedWorkingDirectory(const std::optional<std::string> &target)
    {
        if (!target.has_value())
        {
            return;
        }

        if (!capture_current_directory())
        {
            return;
        }

        if (!apply_new_directory(*target))
        {
            previous_.reset();
            return;
        }

        active_ = true;
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
    ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

    ScopedWorkingDirectory(ScopedWorkingDirectory &&other) noexcept
        : active_(other.active_)
        , previous_(std::move(other.previous_))
    {
        other.active_ = false;
    }

    ScopedWorkingDirectory &operator=(ScopedWorkingDirectory &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        if (active_)
        {
            restore();
        }

        active_ = other.active_;
        previous_ = std::move(other.previous_);
        other.active_ = false;
        return *this;
    }

    ~ScopedWorkingDirectory()
    {
        if (active_)
        {
            restore();
        }
    }

private:
#ifdef _WIN32
    using PathBuffer = std::wstring;
#else
    using PathBuffer = std::string;
#endif

    bool capture_current_directory()
    {
#ifdef _WIN32
        const DWORD required = GetCurrentDirectoryW(0, nullptr);
        if (required == 0)
        {
            return false;
        }

        PathBuffer buffer(required, L'\0');
        const DWORD written = GetCurrentDirectoryW(required, buffer.data());
        if (written == 0 || written >= required)
        {
            return false;
        }

        buffer.resize(written);
        previous_ = std::move(buffer);
        return true;
#else
        std::size_t size = 256;
#    ifdef PATH_MAX
        size = std::max<std::size_t>(size, PATH_MAX);
#    endif
        PathBuffer buffer(size, '\0');

        while (true)
        {
            if (::getcwd(buffer.data(), buffer.size()) != nullptr)
            {
                buffer.erase(std::find(buffer.begin(), buffer.end(), '\0'), buffer.end());
                previous_ = std::move(buffer);
                return true;
            }

            if (errno == ERANGE)
            {
                size *= 2;
                buffer.assign(size, '\0');
                continue;
            }

            return false;
        }
#endif
    }

#ifdef _WIN32
    static std::wstring utf8_to_wide(const std::string &utf8)
    {
        if (utf8.empty())
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring wide(static_cast<std::size_t>(required - 1), L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), required - 1) == 0)
        {
            return std::wstring();
        }

        return wide;
    }
#endif

    bool apply_new_directory(const std::string &path)
    {
#ifdef _WIN32
        const std::wstring wide = utf8_to_wide(path);
        if (wide.empty() && !path.empty())
        {
            return false;
        }

        if (SetCurrentDirectoryW(wide.c_str()) == 0)
        {
            return false;
        }
#else
        if (::chdir(path.c_str()) != 0)
        {
            return false;
        }
#endif
        return true;
    }

    void restore()
    {
        if (!previous_.has_value())
        {
            return;
        }

#ifdef _WIN32
        SetCurrentDirectoryW(previous_->c_str());
#else
        ::chdir(previous_->c_str());
#endif
    }

    bool active_ = false;
    std::optional<PathBuffer> previous_;
};
} // namespace

RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd,
                      const std::vector<std::pair<std::string, std::string>> &env)
{
    ScopedWorkingDirectory scoped_cwd(cwd);
    std::vector<ScopedEnvironmentAssignment> scoped_env;
    scoped_env.reserve(env.size());
    for (const auto &pair : env)
    {
        scoped_env.emplace_back(pair.first, pair.second);
    }

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

    const int status = PCLOSE(pipe);
#ifdef _WIN32
    rr.exit_code = status;
#else
    if (WIFEXITED(status))
    {
        rr.exit_code = WEXITSTATUS(status);
    }
    else
    {
        rr.exit_code = status;
    }
    // When stderr is redirected to stdout the captured text lives in `out`.
    rr.err = rr.out;
#endif
    return rr;
}

#undef POPEN
#undef PCLOSE
