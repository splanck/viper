//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTlsWrapperTests.cpp
// Purpose: Validate Viper.Crypto.Tls wrapper argument handling.
//
//===----------------------------------------------------------------------===//

#include "rt_tls.h"

#include <cassert>
#include <cstdio>
#include <limits>

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static void test_tls_wrapper_rejects_invalid_args_without_network() {
    printf("Testing TLS wrapper invalid arguments:\n");

    rt_string host = rt_const_cstr("example.com");
    test_result("ConnectFor rejects overflowing timeout",
                rt_viper_tls_connect_for(host, 443, std::numeric_limits<int64_t>::max()) ==
                    nullptr);

    const char raw_host[] = {'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', 0, 'x'};
    rt_string nul_host = rt_string_from_bytes(raw_host, sizeof(raw_host));
    test_result("Connect rejects embedded NUL hostname",
                rt_viper_tls_connect(nul_host, 443) == nullptr);
    test_result("ConnectFor rejects embedded NUL hostname",
                rt_viper_tls_connect_for(nul_host, 443, 1000) == nullptr);

    const char raw_ca[] = {'/', 't', 'm', 'p', '/', 'c', 'a', 0, 'x'};
    rt_string nul_ca = rt_string_from_bytes(raw_ca, sizeof(raw_ca));
    test_result("ConnectOptions rejects embedded NUL CA path",
                rt_viper_tls_connect_options(host, 443, nul_ca, rt_const_cstr(""), 1, 1000) ==
                    nullptr);

    const char raw_alpn[] = {'h', '2', 0, 'x'};
    rt_string nul_alpn = rt_string_from_bytes(raw_alpn, sizeof(raw_alpn));
    test_result("ConnectOptions rejects embedded NUL ALPN",
                rt_viper_tls_connect_options(host, 443, rt_const_cstr(""), nul_alpn, 1, 1000) ==
                    nullptr);

    test_result("ConnectOptions rejects overflowing timeout",
                rt_viper_tls_connect_options(host,
                                             443,
                                             rt_const_cstr(""),
                                             rt_const_cstr("h2,http/1.1"),
                                             1,
                                             std::numeric_limits<int64_t>::max()) == nullptr);

    test_result("IsOpen(NULL) is false", rt_viper_tls_is_open(nullptr) == 0);
    test_result("NegotiatedAlpn(NULL) returns empty string",
                rt_str_len(rt_viper_tls_negotiated_alpn(nullptr)) == 0);
    test_result("Recv(NULL) returns NULL",
                rt_viper_tls_recv(nullptr, std::numeric_limits<int64_t>::max()) == nullptr);
    test_result("RecvStr(NULL) returns empty string",
                rt_str_len(rt_viper_tls_recv_str(nullptr, std::numeric_limits<int64_t>::max())) ==
                    0);
    test_result("SendStr(NULL) returns -1", rt_viper_tls_send_str(nullptr, host) == -1);

    printf("\n");
}

int main() {
    printf("=== RT TLS Wrapper Tests ===\n\n");
    test_tls_wrapper_rejects_invalid_args_without_network();
    printf("All TLS wrapper tests passed!\n");
    return 0;
}
