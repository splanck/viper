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
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#    include <sys/wait.h>
#    include <unistd.h>
#endif

#ifdef _WIN32
#    include <direct.h>
#    include <wchar.h>
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
        switch (ch)
        {
        case '\\':
        case '"':
        case '$':
        case '`':
            quoted.push_back('\\');
            break;
        default:
            break;
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

    ScopedEnvironmentAssignment(ScopedEnvironmentAssignment &&other) noexcept
        : name_(std::move(other.name_))
        , previous_(std::move(other.previous_))
    {
        other.name_.clear();
        other.previous_.reset();
    }

    ScopedEnvironmentAssignment &operator=(ScopedEnvironmentAssignment &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        std::optional<std::string> current_value;
        if (!other.name_.empty())
        {
            const char *current_raw = std::getenv(other.name_.c_str());
            if (current_raw != nullptr)
            {
                current_value = std::string(current_raw);
            }
        }

        restore();

        name_ = std::move(other.name_);
        previous_ = std::move(other.previous_);

        other.name_.clear();
        other.previous_.reset();

        if (current_value.has_value())
        {
            apply(*current_value);
        }

        return *this;
    }

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
        if (name_.empty())
        {
            return;
        }

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
} // namespace

namespace viper::test_support
{
struct ScopedEnvironmentAssignmentMoveResult
{
    bool value_visible_after_move_ctor;
    bool value_visible_after_move_assign;
    bool restored;
};

ScopedEnvironmentAssignmentMoveResult scoped_environment_assignment_move_preserves(const std::string &name,
                                                                                   const std::string &value)
{
    const char *original_raw = std::getenv(name.c_str());
    std::optional<std::string> original_value;
    if (original_raw != nullptr)
    {
        original_value = std::string(original_raw);
    }

    ScopedEnvironmentAssignmentMoveResult result{false, false, false};

    {
        ScopedEnvironmentAssignment guard(name, value);
        ScopedEnvironmentAssignment moved(std::move(guard));
        const char *current = std::getenv(name.c_str());
        result.value_visible_after_move_ctor = (current != nullptr) && std::string(current) == value;

        ScopedEnvironmentAssignment receiver(name, value);
        receiver = std::move(moved);
        current = std::getenv(name.c_str());
        result.value_visible_after_move_assign = (current != nullptr) && std::string(current) == value;
    }

    const char *restored_raw = std::getenv(name.c_str());
    if (original_value.has_value())
    {
        result.restored = (restored_raw != nullptr) && std::string(restored_raw) == *original_value;
    }
    else
    {
        result.restored = (restored_raw == nullptr) || std::string(restored_raw).empty();
    }

    return result;
}
} // namespace viper::test_support

namespace
{
class ScopedWorkingDirectory
{
public:
    explicit ScopedWorkingDirectory(const std::optional<std::string> &target)
    {
        if (!target)
        {
            return;
        }

#ifdef _WIN32
        std::unique_ptr<wchar_t, decltype(&std::free)> previous(
            _wgetcwd(nullptr, 0), &std::free);
        if (!previous)
        {
            error_.emplace("failed to query current working directory");
            return;
        }

        previous_ = std::wstring(previous.get());
        const std::wstring newDirectory = std::filesystem::u8path(*target).wstring();
        if (_wchdir(newDirectory.c_str()) != 0)
        {
            error_.emplace("failed to change working directory to '" + *target + "'");
            previous_.clear();
            return;
        }
#else
        std::unique_ptr<char, decltype(&std::free)> previous(::getcwd(nullptr, 0), &std::free);
        if (!previous)
        {
            error_.emplace("failed to query current working directory");
            return;
        }

        previous_ = std::string(previous.get());
        if (::chdir(target->c_str()) != 0)
        {
            error_.emplace("failed to change working directory to '" + *target + "'");
            previous_.clear();
            return;
        }
#endif

        active_ = true;
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
    ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

    ~ScopedWorkingDirectory()
    {
        if (!active_)
        {
            return;
        }

#ifdef _WIN32
        _wchdir(previous_.c_str());
#else
        ::chdir(previous_.c_str());
#endif
    }

    bool has_error() const
    {
        return error_.has_value();
    }

    const std::string &error_message() const
    {
        return *error_;
    }

private:
#ifdef _WIN32
    std::wstring previous_;
#else
    std::string previous_;
#endif
    bool active_ = false;
    std::optional<std::string> error_;
};
} // namespace

RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd,
                      const std::vector<std::pair<std::string, std::string>> &env)
{
    std::vector<ScopedEnvironmentAssignment> scoped_env;
    scoped_env.reserve(env.size());
    for (const auto &pair : env)
    {
        scoped_env.emplace_back(pair.first, pair.second);
    }

    RunResult rr{0, "", ""};

    ScopedWorkingDirectory scoped_cwd(cwd);
    if (scoped_cwd.has_error())
    {
        rr.exit_code = -1;
        rr.err = scoped_cwd.error_message();
        return rr;
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
    if (!cmd.empty())
    {
        cmd += " 2>&1";
    }

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
#endif
    // When stderr is redirected to stdout the captured text lives in `out`.
    rr.err = rr.out;
    return rr;
}

#undef POPEN
#undef PCLOSE
