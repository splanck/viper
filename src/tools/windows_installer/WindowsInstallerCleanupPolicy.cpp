//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerCleanupPolicy.cpp
// Purpose: Validate exact paths accepted by the detached Windows installer
//          cleanup helper without invoking the filesystem.
//
// Key invariants:
//   - Namespace parsing is explicit; arbitrary NT device paths are never
//     accepted through a broad "\\?\" prefix check.
//   - Every accepted path names an object below a drive or UNC share root.
//   - Components have one stable Win32 interpretation.
//
// Ownership/Lifetime:
//   - The implementation allocates no storage and retains no input views.
//
// Links: WindowsInstallerCleanupPolicy.hpp, WindowsInstallerCleanup.cpp
//
//===----------------------------------------------------------------------===//

#include "WindowsInstallerCleanupPolicy.hpp"

#include <cstddef>

namespace zanna::installer::cleanup {
namespace {

constexpr bool isSeparator(wchar_t ch) noexcept {
    return ch == L'\\' || ch == L'/';
}

constexpr bool isAsciiAlpha(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

constexpr wchar_t asciiUpper(wchar_t ch) noexcept {
    return ch >= L'a' && ch <= L'z' ? static_cast<wchar_t>(ch - (L'a' - L'A')) : ch;
}

bool startsWithInsensitive(std::wstring_view text, std::wstring_view prefix) noexcept {
    if (text.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const wchar_t left = isSeparator(text[i]) ? L'\\' : asciiUpper(text[i]);
        const wchar_t right = isSeparator(prefix[i]) ? L'\\' : asciiUpper(prefix[i]);
        if (left != right)
            return false;
    }
    return true;
}

bool isReservedDeviceBase(std::wstring_view component) noexcept {
    const size_t dot = component.find(L'.');
    const std::wstring_view base = component.substr(0, dot);
    auto equals = [base](std::wstring_view expected) noexcept {
        if (base.size() != expected.size())
            return false;
        for (size_t i = 0; i < base.size(); ++i) {
            if (asciiUpper(base[i]) != expected[i])
                return false;
        }
        return true;
    };

    if (equals(L"CON") || equals(L"PRN") || equals(L"AUX") || equals(L"NUL") || equals(L"CLOCK$")) {
        return true;
    }
    if (base.size() == 4) {
        const wchar_t first = asciiUpper(base[0]);
        const wchar_t second = asciiUpper(base[1]);
        const wchar_t third = asciiUpper(base[2]);
        const wchar_t digit = base[3];
        const bool numberedDevice = (first == L'C' && second == L'O' && third == L'M') ||
                                    (first == L'L' && second == L'P' && third == L'T');
        if (numberedDevice && ((digit >= L'1' && digit <= L'9') || digit == L'\u00b9' ||
                               digit == L'\u00b2' || digit == L'\u00b3')) {
            return true;
        }
    }
    return false;
}

bool isSafeComponent(std::wstring_view component) noexcept {
    if (component.empty() || component == L"." || component == L"..")
        return false;
    if (component.back() == L'.' || component.back() == L' ')
        return false;
    for (const wchar_t ch : component) {
        if (ch < 0x20 || ch == 0x7f || ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' ||
            ch == L'|' || ch == L'?' || ch == L'*' || isSeparator(ch)) {
            return false;
        }
    }
    return !isReservedDeviceBase(component);
}

bool consumeUncRoot(std::wstring_view path, size_t start, size_t &cursor) noexcept {
    cursor = start;
    for (unsigned rootPart = 0; rootPart < 2; ++rootPart) {
        const size_t componentStart = cursor;
        while (cursor < path.size() && !isSeparator(path[cursor]))
            ++cursor;
        if (cursor == componentStart ||
            !isSafeComponent(path.substr(componentStart, cursor - componentStart))) {
            return false;
        }
        if (cursor == path.size())
            return false;
        ++cursor;
    }
    return true;
}

} // namespace

bool isSafeAbsolutePath(std::wstring_view path) noexcept {
    if (path.size() < 4 || path.size() >= 32760 || path.find(L'\0') != std::wstring_view::npos)
        return false;

    size_t cursor = 0;
    if (path.size() >= 7 && isSeparator(path[0]) && isSeparator(path[1]) && path[2] == L'?' &&
        isSeparator(path[3]) && isAsciiAlpha(path[4]) && path[5] == L':' && isSeparator(path[6])) {
        cursor = 7;
    } else if (path.size() >= 8 && startsWithInsensitive(path, L"\\\\?\\UNC\\")) {
        if (!consumeUncRoot(path, 8, cursor))
            return false;
    } else if (isAsciiAlpha(path[0]) && path[1] == L':' && isSeparator(path[2])) {
        cursor = 3;
    } else if (isSeparator(path[0]) && isSeparator(path[1]) && path[2] != L'.' && path[2] != L'?') {
        if (!consumeUncRoot(path, 2, cursor))
            return false;
    } else {
        return false;
    }

    if (cursor >= path.size())
        return false;
    while (cursor < path.size()) {
        const size_t componentStart = cursor;
        while (cursor < path.size() && !isSeparator(path[cursor]))
            ++cursor;
        if (!isSafeComponent(path.substr(componentStart, cursor - componentStart)))
            return false;
        if (cursor < path.size()) {
            ++cursor;
            if (cursor == path.size())
                return false;
        }
    }
    return true;
}

bool pathsEqual(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i) {
        const wchar_t leftCh = isSeparator(left[i]) ? L'\\' : asciiUpper(left[i]);
        const wchar_t rightCh = isSeparator(right[i]) ? L'\\' : asciiUpper(right[i]);
        if (leftCh != rightCh)
            return false;
    }
    return true;
}

} // namespace zanna::installer::cleanup
