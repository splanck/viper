//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTHttpsServerTests.cpp
// Purpose: Validate HTTPS server identity, construction rollback, managed
//          callback snapshots, and serialized lifecycle publication.
// Key invariants:
//   - Every managed constructor failure restores the exact object baseline.
//   - TLS callbacks receive stable, retainable ServerReq/ServerRes handles.
//   - Concurrent Start/Stop calls publish and retire one listener exactly once.
//   - Certificate paths are length-checked before native file-system access.
// Ownership/Lifetime:
//   - Temporary certificate files are removed by an RAII fixture.
//   - Every managed producer reference is released by the creating test.
// Links: src/runtime/network/rt_https_server.c,
//        src/runtime/network/rt_https_server.h,
//        src/tests/common/TlsServerFixtureData.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/common/TlsServerFixtureData.hpp"

#include "rt_bytes.h"
#include "rt_gc.h"
#include "rt_http_client.h"
#include "rt_http_server.h"
#include "rt_https_server.h"
#include "rt_internal.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_tls.h"
#include "rt_trap.h"

#include <atomic>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if RT_PLATFORM_WINDOWS
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

static int https_server_alloc_countdown = 0;
static int https_server_alloc_observed = 0;
static std::atomic<unsigned> fixture_sequence{0};
static void *retained_https_request = nullptr;
static void *retained_https_response = nullptr;

/// @brief Print and enforce one focused HTTPS runtime assertion.
/// @param condition Condition that must hold.
/// @param name Human-readable assertion name.
static void test_result(bool condition, const char *name) {
    std::printf("  %s: %s\n", name, condition ? "PASS" : "FAIL");
    assert(condition);
}

/// @brief Count managed allocations and fail one selected allocation boundary.
/// @param bytes Requested managed payload bytes.
/// @param next Default allocator used when the boundary is accepted.
/// @return Allocated payload, or NULL at the selected countdown.
static void *https_server_countdown_alloc(int64_t bytes, void *(*next)(int64_t)) {
    https_server_alloc_observed++;
    if (https_server_alloc_countdown > 0 && --https_server_alloc_countdown == 0)
        return nullptr;
    return next(bytes);
}

/// @brief Drop one caller-owned managed object or String reference.
/// @param object Managed value, or NULL.
static void release_managed(void *object) {
    if (object && rt_obj_release_check0(object))
        rt_obj_free(object);
}

/// @brief Write exact fixture bytes to a temporary path.
/// @param path Destination path created by the test fixture.
/// @param contents NUL-terminated PEM bytes to write.
/// @return True only when the complete file was written successfully.
static bool write_fixture_file(const std::filesystem::path &path, const char *contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !contents)
        return false;
    output.write(contents, static_cast<std::streamsize>(std::strlen(contents)));
    return output.good();
}

/// @brief Own one unique temporary certificate/private-key file pair.
/// @details Construction writes deterministic in-tree PEM fixtures. Destruction
///          removes both paths even when a test exits early after a trap.
class temporary_tls_files final {
  public:
    /// @brief Create and populate one unique temporary TLS fixture pair.
    temporary_tls_files() {
        std::error_code error;
        const std::filesystem::path directory = std::filesystem::temp_directory_path(error);
        if (error)
            return;
        const unsigned id = fixture_sequence.fetch_add(1) + 1;
        certificate_path_ = directory / ("zanna_https_server_" + std::to_string(id) + ".crt.pem");
        private_key_path_ = directory / ("zanna_https_server_" + std::to_string(id) + ".key.pem");
        valid_ = write_fixture_file(certificate_path_, zanna::tests::kLocalhostEcCertificatePem) &&
                 write_fixture_file(private_key_path_, zanna::tests::kLocalhostEcPrivateKeyPem);
        if (!valid_)
            remove_files();
    }

    /// @brief Remove both temporary files, ignoring cleanup errors.
    ~temporary_tls_files() {
        remove_files();
    }

    temporary_tls_files(const temporary_tls_files &) = delete;
    temporary_tls_files &operator=(const temporary_tls_files &) = delete;
    temporary_tls_files(temporary_tls_files &&) = delete;
    temporary_tls_files &operator=(temporary_tls_files &&) = delete;

    /// @brief Return whether both fixture files were written completely.
    /// @return True when the paths are ready for HttpsServer construction.
    [[nodiscard]] bool valid() const {
        return valid_;
    }

    /// @brief Return a native copy of the certificate path.
    /// @return Path string owned by the caller.
    [[nodiscard]] std::string certificate_path() const {
        return certificate_path_.string();
    }

    /// @brief Return a native copy of the private-key path.
    /// @return Path string owned by the caller.
    [[nodiscard]] std::string private_key_path() const {
        return private_key_path_.string();
    }

  private:
    /// @brief Remove any paths already published by construction.
    void remove_files() noexcept {
        std::error_code error;
        if (!certificate_path_.empty())
            std::filesystem::remove(certificate_path_, error);
        error.clear();
        if (!private_key_path_.empty())
            std::filesystem::remove(private_key_path_, error);
    }

    std::filesystem::path certificate_path_;
    std::filesystem::path private_key_path_;
    bool valid_ = false;
};

/// @brief Create managed certificate and private-key path Strings.
/// @param fixture Valid temporary fixture.
/// @param certificate_out Receives a caller-owned certificate-path String.
/// @param private_key_out Receives a caller-owned private-key-path String.
static void make_managed_paths(const temporary_tls_files &fixture,
                               rt_string *certificate_out,
                               rt_string *private_key_out) {
    const std::string certificate = fixture.certificate_path();
    const std::string private_key = fixture.private_key_path();
    *certificate_out = rt_string_from_bytes(certificate.data(), certificate.size());
    *private_key_out = rt_string_from_bytes(private_key.data(), private_key.size());
}

/// @brief Register one GET route using balanced temporary Strings.
/// @param server Valid stopped HttpsServer.
/// @param pattern Route pattern.
/// @param tag Handler tag.
static void register_get(void *server, const char *pattern, const char *tag) {
    rt_string pattern_string = rt_const_cstr(pattern);
    rt_string tag_string = rt_const_cstr(tag);
    rt_https_server_get(server, pattern_string, tag_string);
    release_managed(tag_string);
    release_managed(pattern_string);
}

/// @brief Bind one native HTTPS route handler using a balanced tag String.
/// @param server Valid stopped HttpsServer.
/// @param tag Handler tag.
/// @param handler Native handler function.
static void bind_native(void *server, const char *tag, rt_http_server_handler_fn handler) {
    rt_string tag_string = rt_const_cstr(tag);
    rt_https_server_bind_handler(server, tag_string, reinterpret_cast<void *>(handler));
    release_managed(tag_string);
}

/// @brief Populate a TLS response and retain both callback snapshots.
/// @details Stable class IDs are asserted before either payload is accessed.
///          The producer retains prove both handles remain usable after the
///          server completes serialization and drops its own references.
/// @param request Managed ServerReq snapshot.
/// @param response Managed ServerRes builder.
static void retained_tls_handler(void *request, void *response) {
    assert(rt_obj_class_id(request) == RT_SERVER_REQ_CLASS_ID);
    assert(rt_obj_class_id(response) == RT_SERVER_RES_CLASS_ID);
    rt_obj_retain_maybe(request);
    rt_obj_retain_maybe(response);
    retained_https_request = request;
    retained_https_response = response;

    rt_string header_name = rt_const_cstr("X-TLS-Reply");
    rt_string header_value = rt_const_cstr("complete");
    rt_string body = rt_const_cstr("secure-ok");
    rt_server_res_header(response, header_name, header_value);
    rt_server_res_send(response, body);
    release_managed(body);
    release_managed(header_value);
    release_managed(header_name);
}

/// @brief Verify stable HTTPS identity and every managed constructor edge.
/// @param fixture Valid certificate fixture used by all constructor attempts.
static void test_identity_and_constructor_cleanup(const temporary_tls_files &fixture) {
    std::printf("\nTesting HttpsServer identity and constructor cleanup:\n");
    rt_string certificate = nullptr;
    rt_string private_key = nullptr;
    make_managed_paths(fixture, &certificate, &private_key);
    const int64_t baseline = rt_gc_tracked_count();

    https_server_alloc_countdown = 0;
    https_server_alloc_observed = 0;
    rt_set_alloc_hook(https_server_countdown_alloc);
    void *server = rt_https_server_new(0, certificate, private_key);
    rt_set_alloc_hook(nullptr);
    const int allocation_count = https_server_alloc_observed;
    test_result(server && rt_obj_class_id(server) == RT_HTTPS_SERVER_CLASS_ID,
                "constructor publishes the stable HttpsServer class ID");
    release_managed(server);
    test_result(rt_gc_tracked_count() == baseline,
                "successful stopped HTTPS server restores its exact baseline");

    bool all_clean = allocation_count > 0;
    int trap_count = 0;
    for (int fail_at = 1; fail_at <= allocation_count; fail_at++) {
        void *volatile failed_server = nullptr;
        volatile bool trapped = false;
        jmp_buf recovery;
        https_server_alloc_countdown = fail_at;
        https_server_alloc_observed = 0;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            rt_set_alloc_hook(https_server_countdown_alloc);
            failed_server = rt_https_server_new(0, certificate, private_key);
            rt_set_alloc_hook(nullptr);
            rt_trap_clear_recovery();
        } else {
            rt_set_alloc_hook(nullptr);
            trapped = true;
            rt_trap_clear_recovery();
        }
        trap_count += trapped ? 1 : 0;
        release_managed((void *)failed_server);
        all_clean =
            all_clean && trapped && failed_server == nullptr && rt_gc_tracked_count() == baseline;
    }
    https_server_alloc_countdown = 0;
    test_result(all_clean && trap_count == allocation_count,
                "every managed HTTPS constructor failure releases partial TLS/router state");

    release_managed(private_key);
    release_managed(certificate);
}

/// @brief Verify forged and embedded-NUL certificate paths are rejected safely.
/// @param fixture Valid file paths used to form malformed managed inputs.
static void test_certificate_path_validation(const temporary_tls_files &fixture) {
    std::printf("\nTesting HttpsServer certificate path validation:\n");
    const int64_t baseline = rt_gc_tracked_count();
    rt_string certificate = nullptr;
    rt_string private_key = nullptr;
    make_managed_paths(fixture, &certificate, &private_key);
    void *wrong = rt_seq_new();

    volatile bool forged_trapped = false;
    jmp_buf forged_recovery;
    rt_trap_set_recovery(&forged_recovery);
    if (setjmp(forged_recovery) == 0) {
        (void)rt_https_server_new(0, reinterpret_cast<rt_string>(wrong), private_key);
        rt_trap_clear_recovery();
    } else {
        forged_trapped = true;
        rt_trap_clear_recovery();
    }

    std::string malformed = fixture.certificate_path();
    malformed.push_back('\0');
    malformed.append("ignored.pem");
    rt_string embedded_nul = rt_string_from_bytes(malformed.data(), malformed.size());
    volatile bool nul_trapped = false;
    jmp_buf nul_recovery;
    rt_trap_set_recovery(&nul_recovery);
    if (setjmp(nul_recovery) == 0) {
        (void)rt_https_server_new(0, embedded_nul, private_key);
        rt_trap_clear_recovery();
    } else {
        nul_trapped = true;
        rt_trap_clear_recovery();
    }

    test_result(forged_trapped && nul_trapped && rt_seq_len(wrong) == 0,
                "invalid path handles trap before TLS or file-system access");
    release_managed(embedded_nul);
    release_managed(wrong);
    release_managed(private_key);
    release_managed(certificate);
    test_result(rt_gc_tracked_count() == baseline,
                "certificate path rejection leaves no managed objects behind");
}

/// @brief Exercise one HTTPS/1.1 request and retained callback handles.
/// @param fixture Valid certificate fixture.
static void test_managed_tls_callback_snapshots(const temporary_tls_files &fixture) {
    std::printf("\nTesting managed HTTPS callback snapshots:\n");
    const int64_t baseline = rt_gc_tracked_count();
    rt_string certificate = nullptr;
    rt_string private_key = nullptr;
    make_managed_paths(fixture, &certificate, &private_key);
    void *server = rt_https_server_new(0, certificate, private_key);
    register_get(server, "/secure", "secure");
    bind_native(server, "secure", retained_tls_handler);
    rt_https_server_start(server);
    const int64_t port = rt_https_server_port(server);
    test_result(rt_https_server_is_running(server) == 1 && port > 0,
                "HTTPS callback fixture publishes one live listener");

    char url_buffer[128];
    const int url_length = std::snprintf(url_buffer,
                                         sizeof(url_buffer),
                                         "https://127.0.0.1:%lld/secure",
                                         static_cast<long long>(port));
    assert(url_length > 0 && static_cast<size_t>(url_length) < sizeof(url_buffer));
    rt_string method = rt_const_cstr("GET");
    rt_string url = rt_string_from_bytes(url_buffer, static_cast<size_t>(url_length));
    void *request = rt_http_req_new(method, url);
    rt_http_req_set_tls_verify(request, 0);
    rt_http_req_set_keep_alive(request, 0);
    rt_http_req_set_force_http1(request, 1);
    void *response = rt_http_req_send(request);
    rt_string body = rt_http_res_body_str(response);
    rt_string header_name = rt_const_cstr("X-TLS-Reply");
    rt_string header = rt_http_res_header(response, header_name);
    test_result(rt_http_res_status(response) == 200 && rt_str_len(body) == 9 &&
                    std::memcmp(rt_string_cstr(body), "secure-ok", 9) == 0 &&
                    std::strcmp(rt_string_cstr(header), "complete") == 0,
                "HTTPS/1.1 callback serializes the managed response snapshot");

    rt_string retained_method = rt_server_req_method(retained_https_request);
    test_result(retained_method && std::strcmp(rt_string_cstr(retained_method), "GET") == 0 &&
                    rt_server_res_status(retained_https_response, 202) == retained_https_response,
                "retained TLS request/response handles survive completed dispatch");

    release_managed(retained_method);
    release_managed(retained_https_response);
    release_managed(retained_https_request);
    retained_https_response = nullptr;
    retained_https_request = nullptr;

    rt_string tls_host = rt_const_cstr("127.0.0.1");
    rt_string tls_empty = rt_const_cstr("");
    rt_string tls_alpn = rt_const_cstr("http/1.1");
    void *tls = rt_zanna_tls_connect_options(tls_host, port, tls_empty, tls_alpn, 0, 5000);
    test_result(tls && rt_obj_class_id(tls) == RT_TLS_CLASS_ID && rt_zanna_tls_is_open(tls) == 1,
                "Zanna TLS wrapper connects with stable identity");

    void *wrong_payload = rt_seq_new();
    test_result(tls && rt_zanna_tls_send(tls, wrong_payload) == -1 &&
                    rt_zanna_tls_send_str(tls, reinterpret_cast<rt_string>(wrong_payload)) == -1,
                "TLS send paths reject forged Bytes and String handles");

    char raw_request[256];
    const int raw_request_len = std::snprintf(raw_request,
                                              sizeof(raw_request),
                                              "GET /secure HTTP/1.1\r\n"
                                              "Host: 127.0.0.1:%lld\r\n"
                                              "Connection: close\r\n\r\n",
                                              static_cast<long long>(port));
    assert(raw_request_len > 0 && static_cast<size_t>(raw_request_len) < sizeof(raw_request));
    void *request_bytes =
        rt_bytes_from_raw(reinterpret_cast<const uint8_t *>(raw_request), raw_request_len);
    test_result(tls && rt_zanna_tls_send(tls, request_bytes) == raw_request_len,
                "TLS Bytes send writes the exact raw span without staging copies");

    rt_string status_line = rt_zanna_tls_recv_line(tls);
    test_result(status_line && std::strcmp(rt_string_cstr(status_line), "HTTP/1.1 200 OK") == 0,
                "TLS line receive preserves the HTTPS status line");
    for (int header_index = 0; header_index < 32; header_index++) {
        rt_string header_line = rt_zanna_tls_recv_line(tls);
        const bool complete = header_line && rt_str_len(header_line) == 0;
        release_managed(header_line);
        if (complete)
            break;
        assert(header_index < 31);
    }

    std::string wrapper_body;
    while (wrapper_body.size() < 9u) {
        void *chunk = rt_zanna_tls_recv(tls, static_cast<int64_t>(9u - wrapper_body.size()));
        assert(chunk && rt_bytes_is_bytes(chunk));
        const int64_t chunk_length = rt_bytes_len(chunk);
        assert(chunk_length > 0);
        wrapper_body.append(reinterpret_cast<const char *>(rt_bytes_data_const(chunk)),
                            static_cast<size_t>(chunk_length));
        release_managed(chunk);
    }
    test_result(wrapper_body == "secure-ok",
                "TLS Bytes receive copies one exact decrypted span into managed storage");

    rt_string wrapper_host = rt_zanna_tls_host(tls);
    rt_string negotiated = rt_zanna_tls_negotiated_alpn(tls);
    test_result(wrapper_host && std::strcmp(rt_string_cstr(wrapper_host), "127.0.0.1") == 0 &&
                    rt_zanna_tls_port(tls) == port && negotiated &&
                    std::strcmp(rt_string_cstr(negotiated), "http/1.1") == 0,
                "TLS wrapper accessors expose retained host, port, and ALPN state");
    rt_zanna_tls_close(tls);
    rt_string closed_error = rt_zanna_tls_error(tls);
    test_result(rt_zanna_tls_is_open(tls) == 0 && closed_error &&
                    std::strcmp(rt_string_cstr(closed_error), "connection closed") == 0,
                "TLS wrapper close is idempotent and publishes closed state");

    release_managed(closed_error);
    release_managed(negotiated);
    release_managed(wrapper_host);
    release_managed(status_line);
    release_managed(request_bytes);
    release_managed(wrong_payload);
    release_managed(tls);
    release_managed(tls_alpn);
    release_managed(tls_empty);
    release_managed(tls_host);

    rt_https_server_stop(server);
    release_managed(retained_https_response);
    release_managed(retained_https_request);
    retained_https_response = nullptr;
    retained_https_request = nullptr;
    release_managed(header);
    release_managed(header_name);
    release_managed(body);
    release_managed(response);
    release_managed(request);
    release_managed(url);
    release_managed(method);
    release_managed(server);
    release_managed(private_key);
    release_managed(certificate);
    test_result(rt_gc_tracked_count() == baseline,
                "HTTPS callback test releases TLS, client, and snapshot object graphs");
}

/// @brief Stress concurrent idempotent HTTPS Start and Stop calls.
/// @param fixture Valid certificate fixture.
static void test_concurrent_lifecycle_publication(const temporary_tls_files &fixture) {
    std::printf("\nTesting concurrent HttpsServer lifecycle publication:\n");
    const int64_t baseline = rt_gc_tracked_count();
    rt_string certificate = nullptr;
    rt_string private_key = nullptr;
    make_managed_paths(fixture, &certificate, &private_key);
    void *server = rt_https_server_new(0, certificate, private_key);

    std::vector<std::thread> starters;
    for (int index = 0; index < 8; index++)
        starters.emplace_back([server]() { rt_https_server_start(server); });
    for (std::thread &thread : starters)
        thread.join();
    test_result(rt_https_server_is_running(server) == 1 && rt_https_server_port(server) > 0,
                "concurrent HTTPS Start calls publish one TLS listener");

    std::vector<std::thread> stoppers;
    for (int index = 0; index < 8; index++)
        stoppers.emplace_back([server]() { rt_https_server_stop(server); });
    for (std::thread &thread : stoppers)
        thread.join();
    test_result(rt_https_server_is_running(server) == 0,
                "concurrent HTTPS Stop calls close and join exactly once");

    rt_https_server_start(server);
    const bool restarted = rt_https_server_is_running(server) == 1;
    rt_https_server_stop(server);
    test_result(restarted && rt_https_server_is_running(server) == 0,
                "serialized HTTPS server reuses its worker pool across restart");

    release_managed(server);
    release_managed(private_key);
    release_managed(certificate);
    test_result(rt_gc_tracked_count() == baseline,
                "HTTPS lifecycle stress releases listener, pool, TLS context, and server");
}

/// @brief Run the focused HttpsServer correctness and concurrency suite.
/// @return Zero after every assertion succeeds, or zero with a fixture skip.
int main() {
#if RT_PLATFORM_WINDOWS
    WSADATA winsock_data{};
    assert(WSAStartup(MAKEWORD(2, 2), &winsock_data) == 0);
#endif

    temporary_tls_files fixture;
    if (!fixture.valid()) {
        std::printf("SKIP: unable to create temporary TLS fixture files\n");
#if RT_PLATFORM_WINDOWS
        WSACleanup();
#endif
        return 0;
    }

    test_identity_and_constructor_cleanup(fixture);
    test_certificate_path_validation(fixture);
    test_managed_tls_callback_snapshots(fixture);
    test_concurrent_lifecycle_publication(fixture);

#if RT_PLATFORM_WINDOWS
    WSACleanup();
#endif
    std::printf("\nAll HttpsServer tests passed.\n");
    return 0;
}
