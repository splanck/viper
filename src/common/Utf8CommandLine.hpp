//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/common/Utf8CommandLine.hpp
// Purpose: Present native process arguments to command-line tools as strict UTF-8.
// Key invariants:
//   - Windows arguments are rebuilt from GetCommandLineW, never the active code page.
//   - Invalid UTF-16 fails tool startup instead of substituting or redirecting a path.
// Ownership/Lifetime: The adapter owns copied strings and argv pointers until destruction.
// Links: src/common/Environment.hpp, src/common/RunProcess.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "PlatformCapabilities.hpp"

#include <climits>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#if ZANNA_HOST_WINDOWS
#include <windows.h>
#endif

namespace zanna::tools {

/// @brief Replace CRT narrow argv with a strict UTF-8 snapshot on Windows.
class Utf8CommandLine {
  public:
    Utf8CommandLine(int argc, char **argv) : argc_(argc), argv_(argv) {
#if ZANNA_HOST_WINDOWS
        argc_ = 0;
        argv_ = nullptr;
        captureWindows();
#endif
    }

    Utf8CommandLine(const Utf8CommandLine &) = delete;
    Utf8CommandLine &operator=(const Utf8CommandLine &) = delete;

    /// @brief Publish the captured argument vector, printing a startup error on failure.
    /// @return True when @p argc and @p argv now reference a valid UTF-8 vector.
    bool applyOrReport(int &argc, char **&argv) const noexcept {
        if (!error_.empty()) {
            std::fprintf(stderr, "error: %s\n", error_.c_str());
            return false;
        }
        argc = argc_;
        argv = argv_;
        return true;
    }

  private:
#if ZANNA_HOST_WINDOWS
    using CommandLineToArgvWFn = LPWSTR *(WINAPI *)(LPCWSTR, int *);

    static HMODULE loadShell32(bool &owned) {
        owned = false;
        if (HMODULE module = GetModuleHandleW(L"shell32.dll"))
            return module;

        wchar_t systemDirectory[32768] = {};
        const UINT length =
            GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        if (length == 0 || length >= std::size(systemDirectory))
            return nullptr;
        std::wstring path(systemDirectory, length);
        if (!path.empty() && path.back() != L'\\')
            path.push_back(L'\\');
        path += L"shell32.dll";
        HMODULE module = LoadLibraryW(path.c_str());
        owned = module != nullptr;
        return module;
    }

    static bool appendUtf8(std::vector<std::string> &storage, const wchar_t *wide) {
        if (!wide)
            return false;
        const std::size_t length = std::wcslen(wide);
        if (length > static_cast<std::size_t>(INT_MAX))
            return false;
        if (length == 0) {
            storage.emplace_back();
            return true;
        }
        const int required = WideCharToMultiByte(CP_UTF8,
                                                 WC_ERR_INVALID_CHARS,
                                                 wide,
                                                 static_cast<int>(length),
                                                 nullptr,
                                                 0,
                                                 nullptr,
                                                 nullptr);
        if (required <= 0)
            return false;
        std::string encoded(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                wide,
                                static_cast<int>(length),
                                encoded.data(),
                                required,
                                nullptr,
                                nullptr) != required) {
            return false;
        }
        storage.push_back(std::move(encoded));
        return true;
    }

    void captureWindows() {
        bool shellOwned = false;
        HMODULE shell32 = loadShell32(shellOwned);
        if (!shell32) {
            error_ = "cannot load the system command-line parser";
            return;
        }
        auto parse =
            reinterpret_cast<CommandLineToArgvWFn>(GetProcAddress(shell32, "CommandLineToArgvW"));
        if (!parse) {
            if (shellOwned)
                FreeLibrary(shell32);
            error_ = "cannot resolve the system command-line parser";
            return;
        }
        int count = 0;
        LPWSTR *wideArgs = parse(GetCommandLineW(), &count);
        if (!wideArgs || count <= 0) {
            if (wideArgs)
                LocalFree(wideArgs);
            if (shellOwned)
                FreeLibrary(shell32);
            error_ = "cannot parse the native Windows command line";
            return;
        }

        storage_.reserve(static_cast<std::size_t>(count));
        bool converted = true;
        for (int index = 0; index < count; ++index) {
            if (!appendUtf8(storage_, wideArgs[index])) {
                converted = false;
                break;
            }
        }
        LocalFree(wideArgs);
        if (shellOwned)
            FreeLibrary(shell32);
        if (!converted) {
            storage_.clear();
            error_ = "the native Windows command line contains invalid Unicode";
            return;
        }

        pointers_.reserve(storage_.size() + 1U);
        for (std::string &argument : storage_)
            pointers_.push_back(argument.data());
        pointers_.push_back(nullptr);
        argc_ = count;
        argv_ = pointers_.data();
    }
#endif

    int argc_{0};
    char **argv_{nullptr};
    std::string error_;
#if ZANNA_HOST_WINDOWS
    std::vector<std::string> storage_;
    std::vector<char *> pointers_;
#endif
};

} // namespace zanna::tools
