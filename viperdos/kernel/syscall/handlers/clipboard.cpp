//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/clipboard.cpp
// Purpose: Clipboard syscall handlers (0x140-0x14F).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"

namespace syscall {

// Kernel clipboard buffer (simple text clipboard)
static constexpr usize CLIPBOARD_MAX = 16384; // 16KB max clipboard
static char g_clipboard[CLIPBOARD_MAX];
static usize g_clipboard_len = 0;

SyscallResult sys_clipboard_set(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *buf = reinterpret_cast<const char *>(a0);
    usize len = static_cast<usize>(a1);

    if (len == 0) {
        g_clipboard_len = 0;
        return SyscallResult::ok();
    }

    if (len > CLIPBOARD_MAX) {
        len = CLIPBOARD_MAX;
    }

    if (!validate_user_read(buf, len, false)) {
        return err_invalid_arg();
    }

    for (usize i = 0; i < len; i++) {
        g_clipboard[i] = buf[i];
    }
    g_clipboard_len = len;

    return SyscallResult::ok(static_cast<u64>(len));
}

SyscallResult sys_clipboard_get(u64 a0, u64 a1, u64, u64, u64, u64) {
    char *buf = reinterpret_cast<char *>(a0);
    usize max_len = static_cast<usize>(a1);

    if (g_clipboard_len == 0) {
        return SyscallResult::ok(0);
    }

    if (max_len == 0) {
        // Query length only
        return SyscallResult::ok(static_cast<u64>(g_clipboard_len));
    }

    if (!validate_user_write(buf, max_len, false)) {
        return err_invalid_arg();
    }

    usize copy_len = g_clipboard_len;
    if (copy_len > max_len) {
        copy_len = max_len;
    }

    for (usize i = 0; i < copy_len; i++) {
        buf[i] = g_clipboard[i];
    }

    return SyscallResult::ok(static_cast<u64>(copy_len));
}

SyscallResult sys_clipboard_has(u64, u64, u64, u64, u64, u64) {
    return SyscallResult::ok(g_clipboard_len > 0 ? 1 : 0);
}

} // namespace syscall
