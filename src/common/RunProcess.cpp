//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: common/RunProcess.cpp
// Purpose: Native subprocess launcher shared by Viper tools and linker helpers.
// Key invariants:
//   - `run_process` launches argv directly without shell interpretation.
//   - stdout and stderr are captured independently.
//   - cwd and environment overrides apply only to the child process.
// Ownership/Lifetime:
//   - Returned output buffers are owned by the caller via RunResult.
//   - Helper-local OS handles/pipes are closed before return.
// Links: common/RunProcess.hpp
// Cross-platform touchpoints: POSIX fork/exec + pipe polling, Windows
//                             CreateProcess handle inheritance, cwd/env
//                             behavior across host toolchains.
//
//===----------------------------------------------------------------------===//

#include "common/RunProcess.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <windows.h>
#include <direct.h>
#include <wchar.h>
#endif

namespace {

#if defined(_WIN32)
bool needs_quoting(const std::string &arg) {
    if (arg.empty()) {
        return true;
    }

    for (const char ch : arg) {
        if (ch == ' ' || ch == '\t' || ch == '"' || ch == '&' || ch == '|' || ch == '<' ||
            ch == '>' || ch == '^' || ch == '(' || ch == ')') {
            return true;
        }
    }
    return false;
}

std::string quote_windows_argument(const std::string &arg) {
    if (!needs_quoting(arg)) {
        return arg;
    }

    std::string quoted;
    quoted.reserve(arg.size() + 2);
    quoted.push_back('"');

    std::size_t backslash_count = 0;
    for (const char ch : arg) {
        if (ch == '\\') {
            ++backslash_count;
            continue;
        }

        if (ch == '"') {
            quoted.append(backslash_count * 2 + 1, '\\');
            quoted.push_back('"');
            backslash_count = 0;
            continue;
        }

        if (backslash_count != 0) {
            quoted.append(backslash_count, '\\');
            backslash_count = 0;
        }

        quoted.push_back(ch);
    }

    if (backslash_count != 0) {
        quoted.append(backslash_count * 2, '\\');
    }

    quoted.push_back('"');
    return quoted;
}

std::string format_windows_error(DWORD error) {
    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size =
        FormatMessageA(flags, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    if (size == 0 || buffer == nullptr) {
        return "Windows error " + std::to_string(static_cast<unsigned long>(error));
    }

    std::string message(buffer, buffer + size);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) {
        message.pop_back();
    }
    return message;
}

std::string join_windows_command_line(const std::vector<std::string> &argv) {
    std::string cmdline;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            cmdline.push_back(' ');
        }
        cmdline += quote_windows_argument(argv[i]);
    }
    return cmdline;
}

struct CaptureThreadContext {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::string *buffer = nullptr;
};

DWORD WINAPI capture_handle_thread_proc(LPVOID param) {
    auto *ctx = static_cast<CaptureThreadContext *>(param);
    char chunk[4096];
    DWORD read = 0;
    while (ReadFile(ctx->handle, chunk, sizeof(chunk), &read, nullptr) && read != 0) {
        ctx->buffer->append(chunk, chunk + read);
    }
    return 0;
}
#endif

class ScopedEnvironmentAssignment {
  public:
    ScopedEnvironmentAssignment(std::string name, std::string value)
        : name_(std::move(name)), previous_(capture_existing()), override_(std::move(value)) {
        apply_override();
    }

    ScopedEnvironmentAssignment(const ScopedEnvironmentAssignment &) = delete;
    ScopedEnvironmentAssignment &operator=(const ScopedEnvironmentAssignment &) = delete;

    ScopedEnvironmentAssignment(ScopedEnvironmentAssignment &&other) noexcept
        : name_(std::move(other.name_)), previous_(std::move(other.previous_)),
          override_(std::move(other.override_)) {
        other.name_.clear();
        other.previous_.reset();
        other.override_.reset();
    }

    ScopedEnvironmentAssignment &operator=(ScopedEnvironmentAssignment &&other) noexcept {
        if (this == &other) {
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

    ~ScopedEnvironmentAssignment() {
        restore();
    }

  private:
    std::optional<std::string> capture_existing() const {
        const char *existing = std::getenv(name_.c_str());
        if (existing == nullptr) {
            return std::nullopt;
        }
        return std::string(existing);
    }

    void apply(const std::string &value) const {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void apply_override() const {
        if (!override_.has_value() || name_.empty()) {
            return;
        }
        apply(*override_);
    }

    void restore() const {
        if (name_.empty()) {
            return;
        }

        if (previous_.has_value()) {
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

bool validate_working_directory(const std::optional<std::string> &cwd, std::string &err) {
    if (!cwd.has_value()) {
        return true;
    }

    std::error_code ec;
    const std::filesystem::path path(*cwd);
    if (!std::filesystem::exists(path, ec) || ec) {
        err = "failed to change working directory to '" + *cwd + "'";
        return false;
    }
    if (!std::filesystem::is_directory(path, ec) || ec) {
        err = "failed to change working directory to '" + *cwd + "'";
        return false;
    }
    return true;
}

#ifndef _WIN32
void append_child_error_and_exit(int fd, const std::string &message) {
    (void)::write(fd, message.c_str(), message.size());
    _exit(127);
}

void apply_env_overrides_in_child(
    const std::vector<std::pair<std::string, std::string>> &env_overrides) {
    for (const auto &entry : env_overrides) {
        if (entry.first.empty()) {
            continue;
        }
        setenv(entry.first.c_str(), entry.second.c_str(), 1);
    }
}

void capture_posix_pipes(int stdout_fd, int stderr_fd, std::string &out, std::string &err) {
    bool stdout_open = stdout_fd >= 0;
    bool stderr_open = stderr_fd >= 0;
    char buffer[4096];

    while (stdout_open || stderr_open) {
        pollfd fds[2];
        nfds_t nfds = 0;

        if (stdout_open) {
            fds[nfds].fd = stdout_fd;
            fds[nfds].events = POLLIN | POLLHUP;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (stderr_open) {
            fds[nfds].fd = stderr_fd;
            fds[nfds].events = POLLIN | POLLHUP;
            fds[nfds].revents = 0;
            ++nfds;
        }

        const int ready = poll(fds, nfds, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (nfds_t i = 0; i < nfds; ++i) {
            if ((fds[i].revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }

            std::string *target = nullptr;
            bool *open_flag = nullptr;
            if (stdout_open && fds[i].fd == stdout_fd) {
                target = &out;
                open_flag = &stdout_open;
            } else {
                target = &err;
                open_flag = &stderr_open;
            }

            const ssize_t nread = ::read(fds[i].fd, buffer, sizeof(buffer));
            if (nread > 0) {
                target->append(buffer, buffer + nread);
                continue;
            }

            ::close(fds[i].fd);
            *open_flag = false;
        }
    }
}
#endif

} // namespace

namespace viper::test_support {
struct ScopedEnvironmentAssignmentMoveResult {
    bool value_visible_after_move_ctor;
    bool value_visible_after_move_assign;
    bool restored;
    std::optional<std::string> move_assigned_value;
};

ScopedEnvironmentAssignmentMoveResult scoped_environment_assignment_move_preserves(
    const std::string &name, const std::string &source_value, const std::string &receiver_value) {
    const char *original_raw = std::getenv(name.c_str());
    std::optional<std::string> original_value;
    if (original_raw != nullptr) {
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
        if (current != nullptr) {
            result.move_assigned_value = std::string(current);
            result.value_visible_after_move_assign = *result.move_assigned_value == source_value;
        } else {
            result.value_visible_after_move_assign = false;
        }
    }

    const char *restored_raw = std::getenv(name.c_str());
    if (original_value.has_value()) {
        result.restored = (restored_raw != nullptr) && std::string(restored_raw) == *original_value;
    } else {
        result.restored = (restored_raw == nullptr) || std::string(restored_raw).empty();
    }

    return result;
}
} // namespace viper::test_support

RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd,
                      const std::vector<std::pair<std::string, std::string>> &env) {
    RunResult rr{0, "", ""};
    if (argv.empty()) {
        rr.exit_code = -1;
        rr.err = "empty argv";
        return rr;
    }

    if (!validate_working_directory(cwd, rr.err)) {
        rr.exit_code = -1;
        return rr;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdout_read = INVALID_HANDLE_VALUE;
    HANDLE stdout_write = INVALID_HANDLE_VALUE;
    HANDLE stderr_read = INVALID_HANDLE_VALUE;
    HANDLE stderr_write = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
        rr.exit_code = -1;
        rr.err = "failed to create process pipes: " + format_windows_error(GetLastError());
        if (stdout_read != INVALID_HANDLE_VALUE)
            CloseHandle(stdout_read);
        if (stdout_write != INVALID_HANDLE_VALUE)
            CloseHandle(stdout_write);
        if (stderr_read != INVALID_HANDLE_VALUE)
            CloseHandle(stderr_read);
        if (stderr_write != INVALID_HANDLE_VALUE)
            CloseHandle(stderr_write);
        return rr;
    }

    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    PROCESS_INFORMATION process = {};
    std::string cmdline = join_windows_command_line(argv);
    std::vector<char> mutable_cmdline(cmdline.begin(), cmdline.end());
    mutable_cmdline.push_back('\0');

    std::vector<ScopedEnvironmentAssignment> scoped_env;
    scoped_env.reserve(env.size());
    for (const auto &entry : env) {
        scoped_env.emplace_back(entry.first, entry.second);
    }

    const BOOL created =
        CreateProcessA(nullptr, mutable_cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr,
                       cwd ? cwd->c_str() : nullptr, &startup, &process);

    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    if (!created) {
        rr.exit_code = -1;
        rr.err = "failed to launch process: " + format_windows_error(GetLastError());
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);
        return rr;
    }

    CaptureThreadContext stdout_ctx{stdout_read, &rr.out};
    CaptureThreadContext stderr_ctx{stderr_read, &rr.err};
    HANDLE stdout_thread = CreateThread(nullptr, 0, capture_handle_thread_proc, &stdout_ctx, 0, nullptr);
    HANDLE stderr_thread = CreateThread(nullptr, 0, capture_handle_thread_proc, &stderr_ctx, 0, nullptr);

    WaitForSingleObject(process.hProcess, INFINITE);
    if (stdout_thread != nullptr) {
        WaitForSingleObject(stdout_thread, INFINITE);
        CloseHandle(stdout_thread);
    }
    if (stderr_thread != nullptr) {
        WaitForSingleObject(stderr_thread, INFINITE);
        CloseHandle(stderr_thread);
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    rr.exit_code = static_cast<int>(exit_code);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    return rr;
#else
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        rr.exit_code = -1;
        rr.err = "failed to create process pipes: " + std::string(std::strerror(errno));
        if (stdout_pipe[0] >= 0)
            ::close(stdout_pipe[0]);
        if (stdout_pipe[1] >= 0)
            ::close(stdout_pipe[1]);
        if (stderr_pipe[0] >= 0)
            ::close(stderr_pipe[0]);
        if (stderr_pipe[1] >= 0)
            ::close(stderr_pipe[1]);
        return rr;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        rr.exit_code = -1;
        rr.err = "failed to fork: " + std::string(std::strerror(errno));
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        return rr;
    }

    if (pid == 0) {
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);

        if (cwd.has_value() && ::chdir(cwd->c_str()) != 0) {
            append_child_error_and_exit(
                stderr_pipe[1],
                "failed to change working directory to '" + *cwd + "': " + std::strerror(errno));
        }

        if (::dup2(stdout_pipe[1], STDOUT_FILENO) < 0 || ::dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            append_child_error_and_exit(stderr_pipe[1],
                                        "failed to redirect child stdio: " +
                                            std::string(std::strerror(errno)));
        }

        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[1]);
        apply_env_overrides_in_child(env);

        std::vector<char *> exec_argv;
        exec_argv.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            exec_argv.push_back(const_cast<char *>(arg.c_str()));
        }
        exec_argv.push_back(nullptr);

        execvp(exec_argv[0], exec_argv.data());
        append_child_error_and_exit(STDERR_FILENO,
                                    "failed to exec '" + argv[0] + "': " +
                                        std::string(std::strerror(errno)));
    }

    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);
    capture_posix_pipes(stdout_pipe[0], stderr_pipe[0], rr.out, rr.err);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            rr.exit_code = -1;
            rr.err += (rr.err.empty() ? "" : "\n");
            rr.err += "failed to wait for child: " + std::string(std::strerror(errno));
            return rr;
        }
    }

    if (WIFEXITED(status)) {
        rr.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        rr.exit_code = 128 + WTERMSIG(status);
    } else {
        rr.exit_code = status;
    }
    return rr;
#endif
}

RunResult run_shell_command(const std::string &command,
                            std::optional<std::string> cwd,
                            const std::vector<std::pair<std::string, std::string>> &env) {
#ifdef _WIN32
    return run_process({"cmd", "/C", command}, std::move(cwd), env);
#else
    return run_process({"sh", "-c", command}, std::move(cwd), env);
#endif
}
