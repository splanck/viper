//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTTlsWrapperTests.cpp
// Purpose: Validate stable TLS session/wrapper identity, pointer-width socket
//          ownership, exact argument handling, and allocation-safe Result paths.
// Key invariants:
//   - Low-level entry points never dereference unrelated managed objects.
//   - A TLS session owns its native socket only after successful construction.
//   - Explicit close and managed finalization both close the owned descriptor.
//   - Result construction failures restore the exact managed-object baseline.
// Ownership/Lifetime:
//   - Native loopback pairs are RAII-owned until transferred into a TLS session.
//   - Every managed String, Bytes, Result, and TLS producer reference is released.
// Links: src/runtime/network/rt_tls.c, src/runtime/network/rt_tls_api.c,
//        src/runtime/network/rt_tls.h
//
//===----------------------------------------------------------------------===//

#include "tests/common/NetworkTestCompat.hpp"

#include "rt_bytes.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_result.h"
#include "rt_socket_platform.h"
#include "rt_string.h"
#include "rt_tls.h"
#include "rt_trap.h"

#include <cassert>
#include <chrono>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

static int tls_alloc_countdown = 0;
static int tls_alloc_observed = 0;

/// @brief Print and enforce one focused TLS runtime assertion.
/// @param condition Condition that must hold.
/// @param name Human-readable assertion name.
static void test_result(bool condition, const char *name) {
    std::printf("  %s: %s\n", name, condition ? "PASS" : "FAIL");
    assert(condition);
}

/// @brief Drop one caller-owned managed object or String reference.
/// @param object Managed value, or NULL.
static void release_managed(void *object) {
    if (object && rt_obj_release_check0(object))
        rt_obj_free(object);
}

/// @brief Count managed allocations and fail one selected boundary.
/// @param bytes Requested payload bytes.
/// @param next Default runtime allocator.
/// @return Allocated payload, or NULL at the selected countdown.
static void *tls_countdown_alloc(int64_t bytes, void *(*next)(int64_t)) {
    tls_alloc_observed++;
    if (tls_alloc_countdown > 0 && --tls_alloc_countdown == 0)
        return nullptr;
    return next(bytes);
}

/// @brief Own a connected native loopback TCP pair for descriptor-lifetime tests.
/// @details The constructor binds an ephemeral IPv4 listener before connecting,
///          avoiding a server-thread startup race. Either endpoint can be
///          transferred exactly once to a TLS session with `release_client` or
///          `release_peer`; remaining endpoints are closed by the destructor.
class native_socket_pair final {
  public:
    /// @brief Create one connected loopback pair, leaving `valid()` false on failure.
    native_socket_pair() {
        rt_net_init_wsa();
        socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCK)
            return;
        suppress_sigpipe(listener);

        int reuse = 1;
        (void)setsockopt(listener,
                         SOL_SOCKET,
                         SO_REUSEADDR,
                         reinterpret_cast<const char *>(&reuse),
                         sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(zanna::tests::kIpv4LoopbackHostOrder);
        address.sin_port = 0;
        if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
            listen(listener, 1) != 0) {
            CLOSE_SOCKET(listener);
            return;
        }

#if RT_PLATFORM_WINDOWS
        int address_size = sizeof(address);
#else
        socklen_t address_size = sizeof(address);
#endif
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) != 0) {
            CLOSE_SOCKET(listener);
            return;
        }

        client_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_ == INVALID_SOCK) {
            CLOSE_SOCKET(listener);
            return;
        }
        suppress_sigpipe(client_);
        if (connect(client_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
            CLOSE_SOCKET(client_);
            client_ = INVALID_SOCK;
            CLOSE_SOCKET(listener);
            return;
        }
        peer_ = accept(listener, nullptr, nullptr);
        CLOSE_SOCKET(listener);
        if (peer_ == INVALID_SOCK) {
            CLOSE_SOCKET(client_);
            client_ = INVALID_SOCK;
            return;
        }
        suppress_sigpipe(peer_);
    }

    /// @brief Close every endpoint not previously transferred.
    ~native_socket_pair() {
        if (client_ != INVALID_SOCK)
            CLOSE_SOCKET(client_);
        if (peer_ != INVALID_SOCK)
            CLOSE_SOCKET(peer_);
    }

    native_socket_pair(const native_socket_pair &) = delete;
    native_socket_pair &operator=(const native_socket_pair &) = delete;
    native_socket_pair(native_socket_pair &&) = delete;
    native_socket_pair &operator=(native_socket_pair &&) = delete;

    /// @brief Return whether both endpoints were connected successfully.
    /// @return True only when the pair is ready for use.
    [[nodiscard]] bool valid() const {
        return client_ != INVALID_SOCK && peer_ != INVALID_SOCK;
    }

    /// @brief Return the client endpoint without transferring ownership.
    /// @return Native client socket, or `INVALID_SOCK`.
    [[nodiscard]] socket_t client() const {
        return client_;
    }

    /// @brief Return the peer endpoint without transferring ownership.
    /// @return Native peer socket, or `INVALID_SOCK`.
    [[nodiscard]] socket_t peer() const {
        return peer_;
    }

    /// @brief Transfer the client endpoint to a TLS session.
    /// @return Previously owned client socket.
    [[nodiscard]] socket_t release_client() {
        socket_t result = client_;
        client_ = INVALID_SOCK;
        return result;
    }

  private:
    socket_t client_ = INVALID_SOCK;
    socket_t peer_ = INVALID_SOCK;
};

/// @brief Verify that an endpoint observes orderly EOF after its peer is closed.
/// @param endpoint Native socket that remains owned by the caller.
/// @return True when a bounded receive reports zero bytes.
static bool socket_observes_eof(socket_t endpoint) {
    if (endpoint == INVALID_SOCK || !set_socket_timeout(endpoint, 1000, true))
        return false;
    char byte = 0;
    return recv(endpoint, &byte, 1, 0) == 0;
}

/// @brief Verify low-level identity, argument sentinels, and socket ownership.
static void test_low_level_identity_and_lifetime() {
    std::printf("Testing low-level TLS identity and lifetime:\n");
    rt_tls_config_init(nullptr);

    void *forged = rt_bytes_new(1);
    auto *forged_session = reinterpret_cast<rt_tls_session_t *>(forged);
    char byte = 0;
    test_result(rt_tls_handshake(forged_session) == RT_TLS_ERROR_INVALID_ARG,
                "Handshake rejects an unrelated managed object");
    test_result(rt_tls_send(forged_session, &byte, 1) == RT_TLS_ERROR_INVALID_ARG,
                "Send rejects an unrelated managed object");
    test_result(rt_tls_recv(forged_session, &byte, 1) == RT_TLS_ERROR_INVALID_ARG,
                "Recv rejects an unrelated managed object");
    test_result(rt_tls_get_socket(forged_session) == -1,
                "Socket accessor rejects an unrelated managed object");
    test_result(rt_tls_has_buffered_data(forged_session) == 0,
                "Buffered-data accessor rejects an unrelated managed object");
    test_result(std::strcmp(rt_tls_get_negotiated_alpn(forged_session), "") == 0,
                "ALPN accessor rejects an unrelated managed object");
    test_result(std::strcmp(rt_tls_get_error(forged_session), "invalid session") == 0,
                "Error accessor identifies an unrelated managed object");
    rt_tls_close(forged_session);
    test_result(rt_bytes_len(forged) == 1, "Closing a forged session does not consume it");
    release_managed(forged);

    native_socket_pair pair;
    test_result(pair.valid(), "Loopback descriptor pair is available");
    const int64_t baseline = rt_gc_tracked_count();
    socket_t transferred = pair.release_client();
    rt_tls_session_t *session = rt_tls_new(static_cast<intptr_t>(transferred), nullptr);
    test_result(session && rt_obj_class_id(session) == RT_TLS_SESSION_CLASS_ID,
                "Low-level session publishes its stable class identity");
    test_result(session && static_cast<socket_t>(rt_tls_get_socket(session)) == transferred,
                "Socket accessor preserves the complete native handle");
    test_result(session && rt_tls_send(session, nullptr, 0) == 0,
                "Zero-length send is a state-independent no-op");
    test_result(session && rt_tls_recv(session, nullptr, 0) == 0,
                "Zero-length receive is a state-independent no-op");
    test_result(session && rt_tls_send(session, nullptr, 1) == RT_TLS_ERROR_INVALID_ARG,
                "Positive send rejects a NULL byte pointer");
    test_result(session && rt_tls_recv(session, nullptr, 1) == RT_TLS_ERROR_INVALID_ARG,
                "Positive receive rejects a NULL destination");
    rt_tls_close(session);
    test_result(socket_observes_eof(pair.peer()), "Explicit TLS close closes the owned socket");
    test_result(rt_gc_tracked_count() == baseline,
                "Explicit TLS close restores the exact managed baseline");

    native_socket_pair abandoned_pair;
    test_result(abandoned_pair.valid(), "Finalizer descriptor pair is available");
    const int64_t finalizer_baseline = rt_gc_tracked_count();
    rt_tls_session_t *abandoned =
        rt_tls_new(static_cast<intptr_t>(abandoned_pair.release_client()), nullptr);
    test_result(abandoned != nullptr, "Finalizer test constructs a managed TLS session");
    release_managed(abandoned);
    test_result(socket_observes_eof(abandoned_pair.peer()),
                "Managed TLS finalization closes an abandoned socket");
    test_result(rt_gc_tracked_count() == finalizer_baseline,
                "TLS finalization restores the exact managed baseline");

    test_result(rt_tls_new(-1, nullptr) == nullptr, "Invalid native socket is rejected");
    test_result(rt_tls_connect(nullptr, 443, nullptr) == nullptr,
                "NULL low-level connect host is rejected");
    test_result(rt_tls_connect("localhost", 0, nullptr) == nullptr,
                "Port zero is rejected before name resolution");
    std::printf("\n");
}

/// @brief Verify invalid configuration is rejected before socket ownership transfers.
static void test_configuration_transaction() {
    std::printf("Testing TLS configuration transaction:\n");
    native_socket_pair pair;
    test_result(pair.valid(), "Configuration descriptor pair is available");

    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = "";
    rt_tls_session_t *session = rt_tls_new(static_cast<intptr_t>(pair.client()), &config);
    test_result(session == nullptr, "Empty verification hostname is rejected");
    const char marker = 'H';
    test_result(send(pair.client(), &marker, 1, SEND_FLAGS) == 1,
                "Rejected hostname leaves socket ownership with the caller");
    char received = 0;
    test_result(recv(pair.peer(), &received, 1, 0) == 1 && received == marker,
                "Caller-owned socket remains usable after rejection");

    native_socket_pair alpn_pair;
    test_result(alpn_pair.valid(), "ALPN descriptor pair is available");
    rt_tls_config_init(&config);
    config.alpn_protocol = "h2,,http/1.1";
    session = rt_tls_new(static_cast<intptr_t>(alpn_pair.client()), &config);
    test_result(session == nullptr, "ALPN lists reject empty protocol elements");
    test_result(send(alpn_pair.client(), &marker, 1, SEND_FLAGS) == 1,
                "Rejected ALPN leaves socket ownership with the caller");
    std::printf("\n");
}

/// @brief Verify Zanna wrapper validation without performing network I/O.
static void test_tls_wrapper_rejects_invalid_args_without_network() {
    std::printf("Testing TLS wrapper invalid arguments:\n");

    rt_string host = rt_const_cstr("example.com");
    test_result(rt_zanna_tls_connect_for(host, 443, std::numeric_limits<int64_t>::max()) == nullptr,
                "ConnectFor rejects overflowing timeout");

    const char raw_host[] = {'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', 0, 'x'};
    rt_string nul_host = rt_string_from_bytes(raw_host, sizeof(raw_host));
    test_result(rt_zanna_tls_connect(nul_host, 443) == nullptr,
                "Connect rejects embedded NUL hostname");
    test_result(rt_zanna_tls_connect_for(nul_host, 443, 1000) == nullptr,
                "ConnectFor rejects embedded NUL hostname");

    const char raw_ca[] = {'/', 't', 'm', 'p', '/', 'c', 'a', 0, 'x'};
    rt_string nul_ca = rt_string_from_bytes(raw_ca, sizeof(raw_ca));
    rt_string empty = rt_const_cstr("");
    test_result(rt_zanna_tls_connect_options(host, 443, nul_ca, empty, 1, 1000) == nullptr,
                "ConnectOptions rejects embedded NUL CA path");

    const char raw_alpn[] = {'h', '2', 0, 'x'};
    rt_string nul_alpn = rt_string_from_bytes(raw_alpn, sizeof(raw_alpn));
    test_result(rt_zanna_tls_connect_options(host, 443, empty, nul_alpn, 1, 1000) == nullptr,
                "ConnectOptions rejects embedded NUL ALPN");

    rt_string protocols = rt_const_cstr("h2,http/1.1");
    test_result(rt_zanna_tls_connect_options(
                    host, 443, empty, protocols, 1, std::numeric_limits<int64_t>::max()) == nullptr,
                "ConnectOptions rejects overflowing timeout");

    void *forged = rt_bytes_new(0);
    test_result(rt_zanna_tls_connect(reinterpret_cast<rt_string>(forged), 443) == nullptr,
                "Connect rejects a forged String without dereferencing it");
    test_result(rt_zanna_tls_connect_options(
                    host, 443, reinterpret_cast<rt_string>(forged), empty, 1, 1000) == nullptr,
                "ConnectOptions rejects a forged CA String");

    test_result(rt_zanna_tls_is_open(nullptr) == 0, "IsOpen(NULL) is false");
    rt_string alpn = rt_zanna_tls_negotiated_alpn(nullptr);
    test_result(alpn && rt_str_len(alpn) == 0, "NegotiatedAlpn(NULL) returns empty string");
    release_managed(alpn);
    test_result(rt_zanna_tls_recv(nullptr, std::numeric_limits<int64_t>::max()) == nullptr,
                "Recv(NULL) returns NULL");
    rt_string received_string = rt_zanna_tls_recv_str(nullptr, std::numeric_limits<int64_t>::max());
    test_result(received_string && rt_str_len(received_string) == 0,
                "RecvStr(NULL) returns empty string");
    release_managed(received_string);
    test_result(rt_zanna_tls_send_str(nullptr, host) == -1, "SendStr(NULL) returns -1");

    release_managed(forged);
    release_managed(protocols);
    release_managed(nul_alpn);
    release_managed(empty);
    release_managed(nul_ca);
    release_managed(nul_host);
    release_managed(host);
    std::printf("\n");
}

/// @brief Sweep every managed allocation in the invalid-argument Result path.
/// @details Each forced failure must trap exactly once into the test recovery
///          frame, publish no partial Result, and restore the baseline that
///          includes only the caller-owned host String.
static void test_result_allocation_transaction() {
    std::printf("Testing TLS Result allocation transaction:\n");
    rt_string host = rt_const_cstr("example.com");
    const int64_t baseline = rt_gc_tracked_count();

    tls_alloc_countdown = 0;
    tls_alloc_observed = 0;
    rt_set_alloc_hook(tls_countdown_alloc);
    void *result = rt_zanna_tls_connect_result(host, 0);
    rt_set_alloc_hook(nullptr);
    const int allocation_count = tls_alloc_observed;
    test_result(result && rt_result_is_err(result), "Invalid port returns Result.ErrStr");
    release_managed(result);
    test_result(rt_gc_tracked_count() == baseline,
                "Successful error Result restores the exact managed baseline");

    int trapped_count = 0;
    bool all_clean = allocation_count > 0;
    for (int fail_at = 1; fail_at <= allocation_count; fail_at++) {
        void *volatile partial_result = nullptr;
        volatile bool trapped = false;
        jmp_buf recovery;
        tls_alloc_countdown = fail_at;
        tls_alloc_observed = 0;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            rt_set_alloc_hook(tls_countdown_alloc);
            partial_result = rt_zanna_tls_connect_result(host, 0);
            rt_set_alloc_hook(nullptr);
            rt_trap_clear_recovery();
        } else {
            rt_set_alloc_hook(nullptr);
            rt_trap_clear_recovery();
            trapped = true;
            trapped_count++;
        }
        release_managed((void *)partial_result);
        all_clean =
            all_clean && trapped && tls_alloc_countdown == 0 && rt_gc_tracked_count() == baseline;
    }
    tls_alloc_countdown = 0;
    rt_set_alloc_hook(nullptr);
    test_result(all_clean, "Every Result allocation failure restores the exact baseline");
    test_result(trapped_count == allocation_count,
                "Every Result allocation boundary propagates one trap");

    release_managed(host);
    std::printf("\n");
}

/// @brief Verify a forged language-visible TLS receiver traps before payload access.
static void test_forged_wrapper_receiver() {
    std::printf("Testing forged TLS wrapper receiver:\n");
    void *forged = rt_bytes_new(0);
    volatile bool trapped = false;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        (void)rt_zanna_tls_port(forged);
        rt_trap_clear_recovery();
    } else {
        rt_trap_clear_recovery();
        trapped = true;
    }
    test_result(trapped, "Forged wrapper traps before TLS payload access");
    test_result(rt_bytes_len(forged) == 0, "Forged receiver remains independently owned");
    release_managed(forged);
    std::printf("\n");
}

/// @brief Verify ConnectFor honors one overall deadline across all addresses.
static void test_tls_connect_for_honors_deadline() {
    std::printf("Testing TLS ConnectFor overall deadline:\n");
    rt_string blackhole = rt_const_cstr("192.0.2.1");
    auto start = std::chrono::steady_clock::now();
    void *session = rt_zanna_tls_connect_for(blackhole, 443, 500);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    test_result(session == nullptr, "Blackhole ConnectFor fails");
    test_result(elapsed < 3000, "ConnectFor returns within its overall deadline");
    release_managed(session);
    release_managed(blackhole);
    std::printf("\n");
}

/// @brief Run all focused TLS runtime regression cases.
/// @return Zero after every assertion passes.
int main() {
    std::printf("=== RT TLS Wrapper Tests ===\n\n");
    test_low_level_identity_and_lifetime();
    test_configuration_transaction();
    test_tls_wrapper_rejects_invalid_args_without_network();
    test_result_allocation_transaction();
    test_forged_wrapper_receiver();
    test_tls_connect_for_honors_deadline();
    std::printf("All TLS wrapper tests passed!\n");
    return 0;
}
