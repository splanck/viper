//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace
{
#if defined(_WIN32)
/// @brief Check if an argument needs quoting for cmd.exe.
/// @details Returns true when the argument contains whitespace, quotes, or other
///          characters that require escaping in the Windows command shell.
///          Also returns true for forward slashes in paths, which cmd.exe may
///          interpret as option markers.
/// @param arg Argument to check.
/// @return True if quoting is necessary.
bool needs_quoting(const std::string &arg)
{
    if (arg.empty())
        return true;

    for (const char ch : arg)
    {
        if (ch == ' ' || ch == '\t' || ch == '"' || ch == '&' || ch == '|' || ch == '<' ||
            ch == '>' || ch == '^' || ch == '(' || ch == ')' || ch == '/')
        {
            return true;
        }
    }
    return false;
}

/// @brief Quote a single argument for safe consumption by the Windows command shell.
///
/// @details Windows applies bespoke parsing rules for backslashes that precede
///          quotes.  The helper tracks runs of backslashes and doubles them
///          before emitting a literal quote so that the CRT reconstructs the
///          original argument byte-for-byte.  All other characters pass through
///          unchanged, and the argument is wrapped in double quotes to preserve
///          embedded whitespace.  For simple arguments without special characters,
///          no quoting is applied to avoid cmd.exe parsing issues.
///
/// @param arg Individual argv element to quote.
/// @return Quoted argument ready for concatenation into a command string.
std::string quote_windows_argument(const std::string &arg)
{
    // For simple arguments, don't quote to avoid cmd.exe parsing complexity
    if (!needs_quoting(arg))
    {
        return arg;
    }

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
/// @brief Quote a single argument for POSIX-compatible shells.
///
/// @details The helper wraps the argument in double quotes and escapes only the
///          characters that POSIX shells treat specially inside that context:
///          backslash, double quote, dollar sign, and backtick.  Everything else
///          is forwarded unchanged so the final command line is both safe to
///          pass to `/bin/sh` and human readable when emitted for debugging.
///
/// @param arg Individual argv element to quote.
/// @return Quoted argument ready for concatenation into a command string.
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
    /// @brief Construct a scoped override for the specified environment variable.
    /// @details Captures the existing value, installs @p value, and restores the
    ///          previous state when the guard is destroyed.
    /// @param name Environment variable to override.
    /// @param value Temporary value applied for the guard's lifetime.
    ScopedEnvironmentAssignment(std::string name, std::string value)
        : name_(std::move(name)), previous_(capture_existing()), override_(std::move(value))
    {
        apply_override();
    }

    ScopedEnvironmentAssignment(const ScopedEnvironmentAssignment &) = delete;
    ScopedEnvironmentAssignment &operator=(const ScopedEnvironmentAssignment &) = delete;

    /// @brief Transfer ownership of the override to a new guard instance.
    /// @details Moves the tracked variable name and any captured prior value
    ///          into @p other while leaving it inert so the destructor performs
    ///          no additional work.
    /// @param other Guard supplying the override to adopt.
    ScopedEnvironmentAssignment(ScopedEnvironmentAssignment &&other) noexcept
        : name_(std::move(other.name_)), previous_(std::move(other.previous_)),
          override_(std::move(other.override_))
    {
        other.name_.clear();
        other.previous_.reset();
        other.override_.reset();
    }

    /// @brief Replace the active override with state moved from @p other.
    /// @details Restores any currently-active override before adopting
    ///          @p other 's environment state to keep mutations balanced.
    /// @param other Guard providing the override to adopt.
    /// @return Reference to this guard after the transfer.
    ScopedEnvironmentAssignment &operator=(ScopedEnvironmentAssignment &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        restore();

        name_ = std::move(other.name_);
        previous_ = std::move(other.previous_);
        override_ = std::move(other.override_);

        other.name_.clear();
        other.previous_.reset();
        other.override_.reset();

        apply_override();

        return *this;
    }

    /// @brief Restore the environment when the guard leaves scope.
    /// @details Calls @ref restore so RAII users observe deterministic cleanup.
    ~ScopedEnvironmentAssignment()
    {
        restore();
    }

  private:
    /// @brief Snapshot the pre-existing environment value for @ref name_.
    /// @details Returns `std::nullopt` when the variable was unset so the guard
    ///          knows to erase it during restoration rather than reassign a
    ///          value.
    std::optional<std::string> capture_existing() const
    {
        const char *existing = std::getenv(name_.c_str());
        if (existing == nullptr)
        {
            return std::nullopt;
        }
        return std::string(existing);
    }

    /// @brief Apply the temporary environment value.
    /// @details Delegates to the platform-specific API without error checking,
    ///          mirroring the helper's historical behaviour.  The guard assumes
    ///          callers provide valid inputs because failures would only occur
    ///          under catastrophic conditions (for example, running out of
    ///          memory while duplicating the string).
    /// @param value Environment value to install.
    void apply(const std::string &value) const
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void apply_override() const
    {
        if (!override_.has_value() || name_.empty())
        {
            return;
        }

        apply(*override_);
    }

    /// @brief Reinstate the captured environment state.
    /// @details Restores the previous value when one existed or removes the
    ///          variable entirely.  The method is const to allow guards stored in
    ///          containers of const elements used purely for cleanup.
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
    std::optional<std::string> override_;
};
} // namespace

namespace viper::test_support
{
/// @brief Outcome bundle returned by environment-override move tests.
/// @details Records whether the override remained visible after move
///          construction and move assignment as well as whether the original
///          environment value was restored once the guards exited scope.
struct ScopedEnvironmentAssignmentMoveResult
{
    bool value_visible_after_move_ctor;
    bool value_visible_after_move_assign;
    bool restored;
    std::optional<std::string> move_assigned_value;
};

/// @brief Exercise move construction/assignment semantics of the environment guard.
/// @details Installs a temporary environment override, moves it through various
///          scenarios, and inspects the observable environment to ensure the
///          override propagates and restores as expected.
/// @param name Environment variable manipulated during the test.
/// @param value Temporary value assigned by the guard.
/// @return Structured summary of the observed behaviour.
ScopedEnvironmentAssignmentMoveResult scoped_environment_assignment_move_preserves(
    const std::string &name, const std::string &source_value, const std::string &receiver_value)
{
    const char *original_raw = std::getenv(name.c_str());
    std::optional<std::string> original_value;
    if (original_raw != nullptr)
    {
        original_value = std::string(original_raw);
    }

    ScopedEnvironmentAssignmentMoveResult result{false, false, false, std::nullopt};

    {
        ScopedEnvironmentAssignment guard(name, source_value);
        ScopedEnvironmentAssignment moved(std::move(guard));
        const char *current = std::getenv(name.c_str());
        result.value_visible_after_move_ctor =
            (current != nullptr) && std::string(current) == source_value;

        ScopedEnvironmentAssignment receiver(name, receiver_value);
        receiver = std::move(moved);
        current = std::getenv(name.c_str());
        if (current != nullptr)
        {
            result.move_assigned_value = std::string(current);
            result.value_visible_after_move_assign = *result.move_assigned_value == source_value;
        }
        else
        {
            result.value_visible_after_move_assign = false;
        }
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
    /// @brief Optionally change the process working directory for the guard's lifetime.
    /// @details Captures the original directory and attempts to switch to
    ///          @p target.  Failures record a diagnostic so @ref run_process can
    ///          surface the issue without throwing exceptions.
    /// @param target Destination directory; when absent the guard becomes a no-op.
    explicit ScopedWorkingDirectory(const std::optional<std::string> &target)
    {
        if (!target)
        {
            return;
        }

#ifdef _WIN32
        std::unique_ptr<wchar_t, decltype(&std::free)> previous(_wgetcwd(nullptr, 0), &std::free);
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

    /// @brief Restore the previous working directory on scope exit.
    /// @details Invoked automatically when the guard is destroyed.  If the
    ///          constructor failed, the destructor becomes a no-op.
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

    /// @brief Check whether the constructor encountered an error.
    /// @return True when the working directory change failed.
    bool has_error() const
    {
        return error_.has_value();
    }

    /// @brief Retrieve the human-readable error captured during construction.
    /// @return Reference to the stored diagnostic message.
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

/// @brief Launch a subprocess using the host shell and capture its output.
/// @details Joins @p argv into a quoted command string, temporarily applies any
///          environment overrides, optionally adjusts the working directory, and
///          then streams the child's stdout/stderr back into the returned
///          @ref RunResult.  The helper preserves historical behaviour by
///          ignoring the @p cwd parameter on platforms lacking a portable
///          implementation.
/// @param argv Command fragments to concatenate into a shell invocation.
/// @param cwd Optional working directory requested by the caller.
/// @param env Environment overrides applied for the lifetime of the child process.
/// @return Result bundle containing exit code, stdout, and stderr text.
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

    int status = PCLOSE(pipe);
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
