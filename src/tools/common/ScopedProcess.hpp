//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/ScopedProcess.hpp
// Purpose: RAII helpers for process-wide command-line side effects.
// Key invariants:
//   - File-descriptor redirection restores the original descriptor on scope exit.
//   - Environment overrides restore the prior value on scope exit.
// Ownership/Lifetime:
//   - Helpers own duplicated file descriptors and copied environment strings.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace viper::tools {

#ifdef _WIN32
inline int scopedDup(int fd) {
    return _dup(fd);
}

inline int scopedDup2(int oldFd, int newFd) {
    return _dup2(oldFd, newFd);
}

inline int scopedClose(int fd) {
    return _close(fd);
}

inline int scopedOpenRead(const char *path) {
    return _open(path, _O_RDONLY | _O_BINARY);
}

inline int scopedOpenWriteTruncate(const char *path) {
    return _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
}

inline std::string scopedLastError() {
    return std::strerror(errno);
}

inline int scopedSetEnv(const char *name, const char *value) {
    return _putenv_s(name, value ? value : "");
}

inline int scopedUnsetEnv(const char *name) {
    return _putenv_s(name, "");
}
#else
inline int scopedDup(int fd) {
    return dup(fd);
}

inline int scopedDup2(int oldFd, int newFd) {
    return dup2(oldFd, newFd);
}

inline int scopedClose(int fd) {
    return close(fd);
}

inline int scopedOpenRead(const char *path) {
    return open(path, O_RDONLY);
}

inline int scopedOpenWriteTruncate(const char *path) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
}

inline std::string scopedLastError() {
    return std::strerror(errno);
}

inline int scopedSetEnv(const char *name, const char *value) {
    return setenv(name, value ? value : "", 1);
}

inline int scopedUnsetEnv(const char *name) {
    return unsetenv(name);
}
#endif

/// @brief Temporarily replace one process file descriptor with another file.
/// @details The constructor duplicates @p targetFd, opens @p path, and then uses
///          dup2/_dup2 to make the target descriptor reference that file. The
///          original descriptor is restored by finish() or the destructor. The
///          helper is intentionally descriptor-based instead of freopen-based so a
///          failed open cannot leave stdin/stdout closed.
class ScopedFdRedirect {
  public:
    /// @brief Redirect @p targetFd to @p path.
    /// @param targetFd Descriptor to replace, for example fileno(stdin).
    /// @param path File to open.
    /// @param writeMode When true, open for truncating output; otherwise open for input.
    ScopedFdRedirect(int targetFd, const std::string &path, bool writeMode)
        : targetFd_(targetFd), path_(path), writeMode_(writeMode) {
        savedFd_ = scopedDup(targetFd_);
        if (savedFd_ < 0) {
            error_ = "failed to duplicate descriptor for " + path_ + ": " + scopedLastError();
            return;
        }

        const int replacementFd =
            writeMode_ ? scopedOpenWriteTruncate(path_.c_str()) : scopedOpenRead(path_.c_str());
        if (replacementFd < 0) {
            error_ = "failed to open " + path_ + ": " + scopedLastError();
            scopedClose(savedFd_);
            savedFd_ = -1;
            return;
        }
        if (scopedDup2(replacementFd, targetFd_) < 0) {
            error_ = "failed to redirect descriptor to " + path_ + ": " + scopedLastError();
            scopedClose(replacementFd);
            scopedClose(savedFd_);
            savedFd_ = -1;
            return;
        }
        scopedClose(replacementFd);
        active_ = true;
    }

    /// @brief Restore the original descriptor if still active.
    ~ScopedFdRedirect() {
        (void)finish();
    }

    ScopedFdRedirect(const ScopedFdRedirect &) = delete;
    ScopedFdRedirect &operator=(const ScopedFdRedirect &) = delete;

    /// @brief Return true when redirection succeeded and has not reported restore errors.
    [[nodiscard]] bool ok() const {
        return error_.empty();
    }

    /// @brief Human-readable error from construction or restoration.
    [[nodiscard]] const std::string &errorMessage() const {
        return error_;
    }

    /// @brief Flush output when needed and restore the original descriptor.
    /// @details The method is idempotent. For stdout/stderr redirection, callers can
    ///          invoke it before returning so flush/restore failures become visible
    ///          diagnostics instead of destructor-only best effort.
    /// @return True when restore completed without error.
    bool finish() {
        if (!active_)
            return error_.empty();
        if (writeMode_ && std::fflush(nullptr) != 0 && error_.empty())
            error_ = "failed to flush redirected stream for " + path_;
        if (savedFd_ >= 0) {
            if (scopedDup2(savedFd_, targetFd_) < 0 && error_.empty())
                error_ = "failed to restore descriptor after " + path_ + ": " + scopedLastError();
            scopedClose(savedFd_);
            savedFd_ = -1;
        }
        active_ = false;
        return error_.empty();
    }

  private:
    int targetFd_{-1};
    int savedFd_{-1};
    std::string path_;
    bool writeMode_{false};
    bool active_{false};
    std::string error_;
};

/// @brief Redirect stdin from a file for the lifetime of this object.
/// @details This thin wrapper supplies the standard input descriptor to
///          ScopedFdRedirect while preserving the same error-reporting API.
class ScopedStdinRedirect : public ScopedFdRedirect {
  public:
    /// @brief Open @p path for reading and make it the process stdin descriptor.
    explicit ScopedStdinRedirect(const std::string &path)
        : ScopedFdRedirect(fileno(stdin), path, false) {}
};

/// @brief Redirect stdout to a truncating file for the lifetime of this object.
/// @details Call finish() before emitting further terminal output so buffered data
///          is flushed and stdout is restored deterministically.
class ScopedStdoutRedirect : public ScopedFdRedirect {
  public:
    /// @brief Open @p path for writing and make it the process stdout descriptor.
    explicit ScopedStdoutRedirect(const std::string &path)
        : ScopedFdRedirect(fileno(stdout), path, true) {}
};

/// @brief Temporarily set or clear a process environment variable.
/// @details The prior value is copied at construction. If the variable was absent,
///          destruction removes it again. Constructor and restore failures are
///          retained for callers that need to reject silent environment changes.
class ScopedEnvVar {
  public:
    /// @brief Set @p name to @p value, or unset it when @p value is null.
    ScopedEnvVar(const char *name, const char *value) : name_(name ? name : "") {
        if (name_.empty()) {
            error_ = "environment variable name must not be empty";
            return;
        }
        if (const char *current = std::getenv(name_.c_str()))
            oldValue_ = current;
        const int rc = value ? scopedSetEnv(name_.c_str(), value) : scopedUnsetEnv(name_.c_str());
        if (rc != 0)
            error_ = "failed to update environment variable " + name_ + ": " + scopedLastError();
    }

    /// @brief Restore the prior environment state.
    ~ScopedEnvVar() {
        (void)restore();
    }

    ScopedEnvVar(const ScopedEnvVar &) = delete;
    ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

    /// @brief Return true when construction and restoration have not failed.
    [[nodiscard]] bool ok() const {
        return error_.empty();
    }

    /// @brief Return the first environment update failure, if any.
    [[nodiscard]] const std::string &errorMessage() const {
        return error_;
    }

    /// @brief Restore the environment immediately.
    /// @return True when the environment was restored or no change was active.
    bool restore() {
        if (restored_)
            return error_.empty();
        restored_ = true;
        if (name_.empty())
            return false;
        const int rc = oldValue_ ? scopedSetEnv(name_.c_str(), oldValue_->c_str())
                                 : scopedUnsetEnv(name_.c_str());
        if (rc != 0 && error_.empty())
            error_ = "failed to restore environment variable " + name_ + ": " + scopedLastError();
        return error_.empty();
    }

  private:
    std::string name_;
    std::optional<std::string> oldValue_;
    bool restored_{false};
    std::string error_;
};

} // namespace viper::tools
