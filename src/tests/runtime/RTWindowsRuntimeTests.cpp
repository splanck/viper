//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTWindowsRuntimeTests.cpp
// Purpose: Focused regression coverage for shared Windows runtime adapters.
//
// Key invariants:
//   - Finite Win32 deadlines never become the INFINITE sentinel.
//   - Concurrent WinSock initialization is idempotent.
//   - WinSock startup remains process-lifetime state and never touches the CRT
//     exit table used by CRT-less native PE executables.
//   - WinSock error classifiers cover documented non-blocking states.
//   - WinSock failure helpers publish deterministic errors and outputs.
//   - Entropy adapters reject invalid outputs without a fallback.
//   - Filesystem adapters reject malformed UTF-8/UTF-16 at Win32 boundaries
//     and recursive-delete protection fails closed.
//   - Machine queries preserve drive roots and long environment-backed paths.
//   - WASAPI source contracts use the CRT thread entry point, strict negotiated
//     format metadata, bounded repeated failures, and paired buffer release.
//   - Windows network workers use CRT-aware threads and download paths stay UTF-16.
//   - SaveData snapshots UTF-16 environment values instead of borrowed ACP strings.
//   - Child-process capture restricts inherited handles, uses CRT-aware threads,
//     and reports wait, pipe, and exit-query failures.
//
// Ownership/Lifetime:
//   - The test owns all worker threads and joins them before exit.
//
// Links: src/runtime/rt_win32_wait.h,
//        src/runtime/network/rt_socket_platform.h,
//        src/runtime/network/rt_entropy_platform.h,
//        src/runtime/io/rt_file_path.h, src/runtime/io/rt_dir_internal.h,
//        src/lib/audio/src/vaud_platform_win32.c, src/common/RunProcess.cpp
//
//===----------------------------------------------------------------------===//

// WinSock2 must precede any adapter that includes windows.h.
#include "rt_socket_platform.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "rt_dir_internal.h"
#include "rt_entropy_platform.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_machine.h"
#include "rt_win32_wait.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

#ifndef ZANNA_SOURCE_DIR
#define ZANNA_SOURCE_DIR "."
#endif

static void test_finite_wait_deadlines() {
    assert(rt_win32_deadline_after_ms(100, -1) == 100);
    assert(rt_win32_deadline_after_ms(100, 0) == 100);
    assert(rt_win32_deadline_after_ms(100, 25) == 125);
    assert(rt_win32_deadline_after_ms(ULLONG_MAX - 5, 10) == ULLONG_MAX);

    assert(rt_win32_wait_slice_at(100, 100) == 0);
    assert(rt_win32_wait_slice_at(100, 101) == 1);
    assert(rt_win32_wait_slice_at(100, 100 + (ULONGLONG)INFINITE) == RT_WIN32_MAX_FINITE_WAIT_MS);
    assert(rt_win32_wait_slice_at(0, ULLONG_MAX) == RT_WIN32_MAX_FINITE_WAIT_MS);
    assert(RT_WIN32_MAX_FINITE_WAIT_MS != INFINITE);
}

static void test_concurrent_winsock_initialization() {
    std::array<std::thread, 8> workers;
    for (std::thread &worker : workers)
        worker = std::thread([] { rt_net_init_wsa(); });
    for (std::thread &worker : workers)
        worker.join();
    rt_net_init_wsa();
}

static void test_winsock_error_contracts() {
    assert(rt_socket_error_is_in_progress(WSAEWOULDBLOCK));
    assert(rt_socket_error_is_in_progress(WSAEINPROGRESS));
    assert(rt_socket_error_is_in_progress(WSAEALREADY));
    assert(!rt_socket_error_is_in_progress(WSAECONNRESET));
    assert(rt_socket_accept_interrupted_by_close(WSAESHUTDOWN));

    WSASetLastError(0);
    assert(wait_socket(INVALID_SOCK, 0, false) == -1);
    assert(WSAGetLastError() == WSAENOTSOCK);
    assert(wait_socket(0, -1, false) == -1);
    assert(WSAGetLastError() == WSAEINVAL);

    WSASetLastError(0);
    assert(rt_socket_shutdown_both(INVALID_SOCK) == SOCKET_ERROR);
    assert(WSAGetLastError() == WSAENOTSOCK);

    int pending_error = 12345;
    assert(!rt_socket_pending_error(INVALID_SOCK, &pending_error));
    assert(pending_error == 0);
    assert(!rt_socket_pending_error(INVALID_SOCK, nullptr));
}

static void test_winsock_startup_source_contract() {
    const std::filesystem::path sourcePath = std::filesystem::path(ZANNA_SOURCE_DIR) / "src" /
                                             "runtime" / "network" / "rt_socket_platform_win.c";
    std::ifstream input(sourcePath, std::ios::binary);
    assert(input);
    const std::string source{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
    assert(source.find("atexit(") == std::string::npos);
    assert(source.find("process lifetime") != std::string::npos);
}

static void test_entropy_argument_contracts() {
    uint64_t random_value = 0;
    assert(rt_entropy_platform_random_bytes(nullptr, 0) == 0);
    assert(rt_entropy_platform_random_bytes(nullptr, 1) == -1);
    assert(rt_entropy_platform_random_u64(nullptr) == -1);
    assert(rt_entropy_platform_random_u64(&random_value) == 0);
}

static void test_strict_windows_path_transcoding() {
    const char valid_utf8[] = "zanna-\xE6\x9D\xB1\xE4\xBA\xAC";
    wchar_t *wide = rt_file_path_utf8_to_wide(valid_utf8);
    assert(wide != nullptr);
    rt_string round_trip = rt_file_path_wide_to_string(wide);
    assert(round_trip != nullptr);
    assert(std::strcmp(rt_string_cstr(round_trip), valid_utf8) == 0);
    rt_str_release_maybe(round_trip);
    std::free(wide);

    const char malformed_utf8[] = "\x78\xC0\xAF";
    assert(rt_file_path_utf8_to_wide(malformed_utf8) == nullptr);
    assert(rt_dir_win_utf8_to_wide(malformed_utf8) == nullptr);

    const wchar_t malformed_utf16[] = {(wchar_t)0xD800, L'\0'};
    rt_string checked_dir = nullptr;
    assert(!rt_dir_win_wide_to_string_checked(malformed_utf16, &checked_dir));
    assert(checked_dir == nullptr);
    rt_string malformed_file = rt_file_path_wide_to_string(malformed_utf16);
    rt_string malformed_dir = rt_dir_win_wide_to_string(malformed_utf16);
    assert(rt_str_len(malformed_file) == 0);
    assert(rt_str_len(malformed_dir) == 0);
    rt_str_release_maybe(malformed_file);
    rt_str_release_maybe(malformed_dir);

    rt_string checked_valid = nullptr;
    assert(rt_dir_win_wide_to_string_checked(L"zanna-\x6771\x4EAC", &checked_valid));
    assert(std::strcmp(rt_string_cstr(checked_valid), valid_utf8) == 0);
    rt_str_release_maybe(checked_valid);
}

static void test_recursive_delete_path_guards() {
    assert(rt_dir_win_path_span_equal(L"C:\\Zanna\\Runtime", L"c:\\zANNA\\runtime", 16));
    assert(!rt_dir_win_path_span_equal(L"C:\\Zanna\\Runtime", L"C:\\Zanna\\Otherxx", 16));

    wchar_t *cwd = rt_dir_win_current_directory_alloc();
    assert(cwd != nullptr);
    rt_string cwd_utf8 = nullptr;
    assert(rt_dir_win_wide_to_string_checked(cwd, &cwd_utf8));
    assert(rt_dir_win_path_matches_cwd_or_ancestor(rt_string_cstr(cwd_utf8)) == 1);
    rt_str_release_maybe(cwd_utf8);
    std::free(cwd);

    const char malformed_utf8[] = "\x78\xC0\xAF";
    assert(rt_dir_win_path_matches_cwd_or_ancestor(malformed_utf8) == 1);
}

class ScopedEnvironmentVariable {
  public:
    explicit ScopedEnvironmentVariable(const wchar_t *name) : name_(name) {
        DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (required == 0)
            return;
        value_.resize(required);
        DWORD length = GetEnvironmentVariableW(name_.c_str(), value_.data(), required);
        if (length > 0 && length < required) {
            value_.resize(length);
            present_ = true;
        } else {
            value_.clear();
        }
    }

    ~ScopedEnvironmentVariable() {
        SetEnvironmentVariableW(name_.c_str(), present_ ? value_.c_str() : nullptr);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
    ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

  private:
    std::wstring name_;
    std::wstring value_;
    bool present_{false};
};

static void test_machine_windows_snapshots() {
    assert(rt_machine_cores() >= 1);

    ScopedEnvironmentVariable savedTmp(L"TMP");
    ScopedEnvironmentVariable savedTemp(L"TEMP");
    assert(SetEnvironmentVariableW(L"TMP", L"C:\\"));
    assert(SetEnvironmentVariableW(L"TEMP", L"C:\\"));
    rt_string temp = rt_machine_temp();
    assert(temp != nullptr);
    assert(std::strcmp(rt_string_cstr(temp), "C:\\") == 0);
    rt_str_release_maybe(temp);

    ScopedEnvironmentVariable savedProfile(L"USERPROFILE");
    std::wstring longProfile = L"C:\\";
    longProfile.append(700, L'x');
    assert(SetEnvironmentVariableW(L"USERPROFILE", longProfile.c_str()));
    rt_string home = rt_machine_home();
    assert(home != nullptr);
    assert(rt_str_len(home) == longProfile.size());
    assert(std::strncmp(rt_string_cstr(home), "C:\\", 3) == 0);
    rt_str_release_maybe(home);
}

static void test_wasapi_backend_source_contracts() {
    const std::filesystem::path sourcePath = std::filesystem::path(ZANNA_SOURCE_DIR) / "src" /
                                             "lib" / "audio" / "src" / "vaud_platform_win32.c";
    std::ifstream input(sourcePath, std::ios::binary);
    assert(input);
    const std::string source{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
    assert(source.find("_beginthreadex") != std::string::npos);
    assert(source.find("CreateThread(NULL") == std::string::npos);
    assert(source.find("fmt->nBlockAlign != expected_block_align") != std::string::npos);
    assert(source.find("fmt->nAvgBytesPerSec != expected_bytes_per_second") != std::string::npos);
    assert(source.find("consecutive_padding_failures >= 8") != std::string::npos);
    assert(source.find("consecutive_buffer_failures >= 8") != std::string::npos);
    assert(source.find("WASAPI failed to release a render buffer") != std::string::npos);
    assert(source.find("valid_bits == 0 || valid_bits > fmt->wBitsPerSample") != std::string::npos);
    assert(source.find("IAudioClient_Reset(plat->client)") != std::string::npos);
    assert(source.find("FAILED(hr) && hr != RPC_E_CHANGED_MODE") == std::string::npos);
    assert(source.find("Cannot resume an inactive WASAPI client") != std::string::npos);
    assert(source.find("InterlockedExchange(&plat->paused, 0)") != std::string::npos);
}

static std::string read_source(std::initializer_list<const char *> components) {
    std::filesystem::path path(ZANNA_SOURCE_DIR);
    for (const char *component : components)
        path /= component;
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static void test_windows_network_worker_source_contracts() {
    constexpr std::array<const char *, 4> files = {
        "rt_ws_server.c", "rt_wss_server.c", "rt_http_server.c", "rt_https_server.c"};
    for (const char *file : files) {
        const std::string source = read_source({"src", "runtime", "network", file});
        assert(source.find("_beginthreadex") != std::string::npos);
        assert(source.find("CreateThread(NULL") == std::string::npos);
        assert(source.find("unsigned __stdcall") != std::string::npos);
    }
}

static void test_windows_unicode_storage_source_contracts() {
    const std::string http = read_source({"src", "runtime", "network", "rt_network_http_api.c"});
    assert(http.find("_wopen(") != std::string::npos);
    assert(http.find("_O_NOINHERIT") != std::string::npos);
    assert(http.find("MoveFileExW(") != std::string::npos);
    assert(http.find("_wstat64(") != std::string::npos);
    assert(http.find("_wchmod(") != std::string::npos);
    assert(http.find("_wremove(") != std::string::npos);
    assert(http.find("MoveFileExA(") == std::string::npos);

    const std::string savedata = read_source({"src", "runtime", "io", "rt_savedata.c"});
    assert(savedata.find("GetEnvironmentVariableW(") != std::string::npos);
    assert(savedata.find("getenv(\"APPDATA\")") == std::string::npos);
    assert(savedata.find("getenv(\"USERPROFILE\")") == std::string::npos);
    assert(savedata.find("getenv(\"HOMEDRIVE\")") == std::string::npos);
    assert(savedata.find("getenv(\"HOMEPATH\")") == std::string::npos);
}

static void test_win32_window_source_contracts() {
    const std::string source =
        read_source({"src", "lib", "graphics", "src", "vgfx_platform_win32.c"});
    assert(source.find("GetClassInfoExW(") != std::string::npos);
    assert(source.find("case WM_UNICHAR:") != std::string::npos);
    assert(source.find("case WM_SYSKEYDOWN:") != std::string::npos);
    assert(source.find("case WM_CAPTURECHANGED:") != std::string::npos);
    assert(source.find("SetCapture(") != std::string::npos);
    assert(source.find("ReleaseCapture(") != std::string::npos);
    assert(source.find("count > (UINT)VGFX_EVENT_QUEUE_SIZE") != std::string::npos);
    assert(source.find("unit_count > (size_t)VGFX_COMPOSITION_TEXT_CAPACITY") != std::string::npos);
    assert(source.find("if (!win32_adjust_window_rect_for_scale") != std::string::npos);
}

static void test_windows_run_process_source_contracts() {
    const std::string source = read_source({"src", "common", "RunProcess.cpp"});
    assert(source.find("PROC_THREAD_ATTRIBUTE_HANDLE_LIST") != std::string::npos);
    assert(source.find("EXTENDED_STARTUPINFO_PRESENT") != std::string::npos);
    assert(source.find("_beginthreadex") != std::string::npos);
    assert(source.find("CreateThread(") == std::string::npos);
    assert(source.find("pathFromUtf8(*cwd)") != std::string::npos);
    assert(source.find("failed while waiting for child process") != std::string::npos);
    assert(source.find("failed while capturing child stdout") != std::string::npos);
    assert(source.find("failed while capturing child stderr") != std::string::npos);
    assert(source.find("failed to query child exit code") != std::string::npos);
}

int main() {
    test_finite_wait_deadlines();
    test_concurrent_winsock_initialization();
    test_winsock_error_contracts();
    test_winsock_startup_source_contract();
    test_entropy_argument_contracts();
    test_strict_windows_path_transcoding();
    test_recursive_delete_path_guards();
    test_machine_windows_snapshots();
    test_wasapi_backend_source_contracts();
    test_windows_network_worker_source_contracts();
    test_windows_unicode_storage_source_contracts();
    test_win32_window_source_contracts();
    test_windows_run_process_source_contracts();
    std::puts("RTWindowsRuntimeTests passed");
    return 0;
}
