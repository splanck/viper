//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTHighLevelNetworkTests.cpp
// Purpose: Integration-style coverage for higher-level network APIs.
//
//===----------------------------------------------------------------------===//

#include "rt_netutils.h"
#include "rt_bytes.h"
#include "rt_http_server.h"
#include "rt_https_server.h"
#include "rt_http2.h"
#include "rt_http_client.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_smtp.h"
#include "rt_sse.h"
#include "rt_string.h"
#include "rt_tls.h"
#include "rt_websocket.h"
#include "rt_wss_server.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET local_sock_t;
#define LOCAL_SOCK_INVALID INVALID_SOCKET
#define LOCAL_SOCK_CLOSE(s) closesocket(s)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int local_sock_t;
#define LOCAL_SOCK_INVALID (-1)
#define LOCAL_SOCK_CLOSE(s) close(s)
#endif

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static std::atomic<bool> api_server_ready{false};
static std::atomic<bool> api_server_failed{false};
static std::atomic<int> http_keepalive_accept_count{0};
static std::atomic<bool> http_keepalive_reused{false};
static std::string smtp_captured_message;
static std::string http_cookie_headers[3];
static std::string http_keepalive_connection_headers[2];
static std::string sse_resume_header;
static std::string redirect_target_authorization_header;
static std::string redirect_target_api_key_header;
static std::string redirect_source_location;
static std::atomic<unsigned> tls_fixture_counter{0};

static const char *LOCALHOST_TEST_KEY_PEM = R"PEM(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg+Z1xhQRSU9+jKQhH
9R9DeB1DObDrQG6uuJYh2fGU/gOhRANCAATfYC4JF5vgz0f005FgdcIvzq+XWoK2
WkHv9ylmizkXwiiwONBMUiHLJp0aQ5prsy/qG1qvxIA+EemN8nsM73O/
-----END PRIVATE KEY-----
)PEM";

static const char *LOCALHOST_TEST_CERT_PEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIBmTCCAT+gAwIBAgIUMx/aHjSr1BLKVJWLkjEW8tVBwEwwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDQxOTAwNDY0OVoXDTM2MDQxNjAw
NDY0OVowFDESMBAGA1UEAwwJbG9jYWxob3N0MFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAE32AuCReb4M9H9NORYHXCL86vl1qCtlpB7/cpZos5F8IosDjQTFIhyyad
GkOaa7Mv6htar8SAPhHpjfJ7DO9zv6NvMG0wHQYDVR0OBBYEFH8rprP1CxiHqLBg
7tp3in6Op8rZMB8GA1UdIwQYMBaAFH8rprP1CxiHqLBg7tp3in6Op8rZMA8GA1Ud
EwEB/wQFMAMBAf8wGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMAoGCCqGSM49
BAMCA0gAMEUCICfrWIQjaBKJOeHsEFydx3kmB3xZA27GVaokzpkBKShNAiEApv2B
ptOACq7G5MbeXCED94+Klf9Txx0gZ+qg8GckbdA=
-----END CERTIFICATE-----
)PEM";

static const char *LOCALHOST_RSA_ROOT_CERT_PEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIDEzCCAfugAwIBAgICEAEwDQYJKoZIhvcNAQELBQAwGjEYMBYGA1UEAwwPVmlw
ZXIgVGVzdCBSb290MB4XDTI0MDEwMTAwMDAwMFoXDTM4MDEwMTAwMDAwMFowGjEY
MBYGA1UEAwwPVmlwZXIgVGVzdCBSb290MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A
MIIBCgKCAQEAsCvt6uYssvJ6Qlsn8LQAmJ0YUbN2gOYwyj2RV3s8UejGMQPleNIf
eRvoeD/WZuG25Vf35YJ6djd0Vw1Mgt0Uk6M8C8oORoGQ6XRsGlNBOqzDiZbuDqxH
KZ1kP3C69p7Ey5cOmk+tgo3vVJgwFAGPhR2f4540UvJ+rAe255wbs9IJ5uqFkKHI
ccb+lrNZZaFPFGBSmI1Czm6ggPx3RHByOtSBepqB6VZzv05rzDV1WHFCGhlyBClJ
BfG29kPuj5n03MUPxfw8NAMahPFszFPoA71oxan4qzffWyKP7FubAUTzxArI8sf5
3otkyWp1d6snlyHlAI7kAb4vdIrwqZRcMQIDAQABo2MwYTAdBgNVHQ4EFgQUZsaT
+usuVwXWs0SWZvbAykvJFP8wHwYDVR0jBBgwFoAUZsaT+usuVwXWs0SWZvbAykvJ
FP8wDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwDQYJKoZIhvcNAQEL
BQADggEBAI+1UE7O2TCevcQfQJZSPGJw8vNGkzCv9tMh4qhV+zfmtjLkmefvQ+0M
FJr/adXHYGE0WjwQ8VK4pfmd6gs26/PCEDREy0JpSXxrkyEiyei5WZvrNBvGQL4m
+XAngI5eTX7UwL7YBP/z8oUHQZOWXGsRyCNFpKi5zZKMUoR8aHuc1f9JDkyNbeon
by85P8iOkGoFFWGMSSI8+lQqPLAYITEUfzqWsyU/T9yXuGzaeOh+lLnq/sOygumJ
crDCbvYzu1M8VSpiGtQ9tiFSqIyQruau7RM69GFicRtKfWOOnHEafY5cZ+V9Dder
RXrTo5++oZs1QvYkMwOG9dD4/fGdRTY=
-----END CERTIFICATE-----
)PEM";

static const char *LOCALHOST_RSA_LEAF_CERT_PEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIDPTCCAiWgAwIBAgICIAEwDQYJKoZIhvcNAQELBQAwGjEYMBYGA1UEAwwPVmlw
ZXIgVGVzdCBSb290MB4XDTI0MDEwMTAwMDAwMFoXDTM4MDEwMTAwMDAwMFowFDES
MBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC
AQEAhKKdI7etjnO2Jnf/6gT9VrQEFCRmU73zic7zvHXkpyg/5zWy+cz7kdV87Ol6
h+5mvl63sspIpCeQ4DbQenlzhyMyDwO+YCZhkZp1Pu6Tq/XoixFL30JHL8uWyvd+
IJDY/Ihn163GdFHJ5aoiljUeZu9xEsYz8qqTR3hwDBpQpeTL3Bd4qIfVUeD5vazF
nEAjOzQpGv3yTmZVn8p5vPxkwusjOHwhXSrIDjAw/PoffsdHXGutjDxZMBdwviEd
VShtoVWN6L5SQZK/y01P4FXN+YpgAcBNUA7vovJO76iaPeCQR2vnj3R/rxq/vvug
sO7JA04QImdhyZ4qbZoLUHJ69wIDAQABo4GSMIGPMBoGA1UdEQQTMBGCCWxvY2Fs
aG9zdIcEfwAAATAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIFoDATBgNVHSUE
DDAKBggrBgEFBQcDATAdBgNVHQ4EFgQUUrU52T/pEly3nsAxjlpTVltClS0wHwYD
VR0jBBgwFoAUZsaT+usuVwXWs0SWZvbAykvJFP8wDQYJKoZIhvcNAQELBQADggEB
AA1wTY/oNMXbAONfawVKoz+biuEFiSwsy4XHKyZmhsFpOcjGlotWl14hjJp/zRBG
6B4PZwYT7D43/1C6wC3q5AOD+kjcrGik4Ef5WFggSZJUl43Ln51TNm5yhWhCT6nZ
0IVv2vKWRmoy3JMRG4hDfAT3Z+SaiwEZPpXnIvClXOgIl+DAuC+8h8CMRrJQ4mVN
aPCWTHi1eZAIiIcJ7Z55yWyWBmH7wx+y1YtYnDXH5ZeXSsm2EMJJtOeX/MPVStpT
cx+Oj/wMKAolQHbKLlO3Y2cBkuJ/cRM252X3QJfC+wk0T6n5MfF8MBlNj/z2fure
KwmzLhH+CCh6NMsSird2hjw=
-----END CERTIFICATE-----
)PEM";

static const char *LOCALHOST_RSA_LEAF_KEY_PEM = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCEop0jt62Oc7Ym
d//qBP1WtAQUJGZTvfOJzvO8deSnKD/nNbL5zPuR1Xzs6XqH7ma+XreyykikJ5Dg
NtB6eXOHIzIPA75gJmGRmnU+7pOr9eiLEUvfQkcvy5bK934gkNj8iGfXrcZ0Ucnl
qiKWNR5m73ESxjPyqpNHeHAMGlCl5MvcF3ioh9VR4Pm9rMWcQCM7NCka/fJOZlWf
ynm8/GTC6yM4fCFdKsgOMDD8+h9+x0dca62MPFkwF3C+IR1VKG2hVY3ovlJBkr/L
TU/gVc35imABwE1QDu+i8k7vqJo94JBHa+ePdH+vGr+++6Cw7skDThAiZ2HJnipt
mgtQcnr3AgMBAAECggEAAYRuDBpG+9V6tLBJhT037dIoJuYrep3hEGc+gqxcJP7y
tmvMOy85hGln3YG70JxihC1VxMFypy2lXHibK2OCZ6i/eFqc4zMH3tILP27/0ZVl
h1owd/RP+ypFHyz0MAihDmS7+SgqChXWG5tSX6K/JnSjxh5Dj8KAHu7MgOYNR1s1
UHqJ/Ldqm2/XzQHkYvAKC6wM7uBXK/voxqH2ob6R7ksPrI6geJMfIFEVXbxzJvnS
azS2iRu7dr47fO2L5q28OiYb6jFxi7pOb8Fa0Tr/cW9mOECZ8MidOXu7AjP5BNiJ
0sgSTXgZ6LqCk2xZBC6r7YUZ+m0onT7XXpSb9ilXBQKBgQC569FRs+TLMBXhJ4Oa
12TP9kDtqDHACx8hieLxQQpVRQVXpHSaSEcAukaVxmWt5W0xlk/1o5CezAICAKst
8tYqkAQo1YnPPPNkX7ZtlOi+fxOmwLDBlUXpvSGkO0dI719HwDkbIL3Pn7o+rD7Z
IyfO2UKQ/CkWnnu4Puw82mj7AwKBgQC2oQxrT+V+qGcGKTAvK9UNSvMjr17HrGau
RqHq9ljMjjIQ1c83EsCibuk9hm4zTti6C2EuDtxciS3OVQAJwFjR4BcAnt9jQr4l
bfmZFE4xpbF6TEHYY4EuTvZOV+1CYd9uNQS18GzhD45r4zSr539/pcaxcsJsuKC/
NU6GW8sj/QKBgGJxX9MACrwfiOY/8uow9Js8y6JK9ZS3DtPGW9jcVGlT84E1fdwX
OylCeI9jjoEmQswHx+zLn47FfKaszfa1ZvsAaINqld6aalGScFjTiO0dAj3AN5c4
v90EnOSF0rfmry+hs1sO2hIuhAIdV+XHPJPE6/8y1Vq5rc6f2pxaFU4bAoGALs3/
fNExI9DM9os/yhcVtx5qSc78H3hTqH55qNoRz/rxYdcqEBdCP17lb9swCv4+FRAt
i7xLRXvyvVqTc+xT1xXzTzloTuwgBz+0JENL9vVcEtfQWEDILrIV9eYa7FRhCsGT
v30qqlNuUMAeE6B00KYP0hJzOaHnsJlc0ppb6ZECgYAvxGRLH2tM01RHzpVEuMJo
BOEO0CXN4TOLIzzOJ/+U30eEGGLMdTgQvNApbqpEZlErSyVW85Xq5M57kyMce4/K
cgD9SkQcscJsGtN3qe481Cha/aHxF0SpE6ZTrmcVSXCdwPxON7ZGf2KRakvBUMkb
s9ACvsoKGa8Ahrkw0OK3kA==
-----END PRIVATE KEY-----
)PEM";

struct temp_tls_files_t {
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
    bool valid = false;

    temp_tls_files_t() = default;
    temp_tls_files_t(const temp_tls_files_t &) = delete;
    temp_tls_files_t &operator=(const temp_tls_files_t &) = delete;

    temp_tls_files_t(temp_tls_files_t &&other) noexcept
        : cert_path(std::move(other.cert_path)),
          key_path(std::move(other.key_path)),
          ca_path(std::move(other.ca_path)),
          valid(other.valid) {
        other.cert_path.clear();
        other.key_path.clear();
        other.ca_path.clear();
        other.valid = false;
    }

    temp_tls_files_t &operator=(temp_tls_files_t &&other) noexcept {
        if (this == &other)
            return *this;
        std::error_code ec;
        if (!cert_path.empty())
            std::filesystem::remove(cert_path, ec);
        if (!key_path.empty())
            std::filesystem::remove(key_path, ec);
        if (!ca_path.empty())
            std::filesystem::remove(ca_path, ec);
        cert_path = std::move(other.cert_path);
        key_path = std::move(other.key_path);
        ca_path = std::move(other.ca_path);
        valid = other.valid;
        other.cert_path.clear();
        other.key_path.clear();
        other.ca_path.clear();
        other.valid = false;
        return *this;
    }

    ~temp_tls_files_t() {
        std::error_code ec;
        if (!cert_path.empty())
            std::filesystem::remove(cert_path, ec);
        if (!key_path.empty())
            std::filesystem::remove(key_path, ec);
        if (!ca_path.empty())
            std::filesystem::remove(ca_path, ec);
    }
};

static bool ascii_header_name_equals(const char *line, const char *name, size_t name_len) {
    if (!line || !name)
        return false;
    for (size_t i = 0; i < name_len; i++) {
        char a = line[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + ('a' - 'A'));
        if (a != b)
            return false;
    }
    return true;
}

static void send_text(void *client, const char *text) {
    rt_tcp_send_str(client, rt_const_cstr(text));
}

static void http_server_keepalive_handler(void *req_obj, void *res_obj) {
    (void)req_obj;
    rt_server_res_send(res_obj, rt_const_cstr("ok"));
}

static void http_redirect_source_handler(void *req_obj, void *res_obj) {
    (void)req_obj;
    rt_server_res_status(res_obj, 302);
    rt_server_res_header(res_obj, rt_const_cstr("Location"), rt_const_cstr(redirect_source_location.c_str()));
    rt_server_res_send(res_obj, rt_const_cstr(""));
}

static void http_redirect_target_handler(void *req_obj, void *res_obj) {
    rt_string authorization = rt_server_req_header(req_obj, rt_const_cstr("Authorization"));
    rt_string api_key = rt_server_req_header(req_obj, rt_const_cstr("X-API-Key"));
    redirect_target_authorization_header = rt_string_cstr(authorization);
    redirect_target_api_key_header = rt_string_cstr(api_key);
    rt_string_unref(authorization);
    rt_string_unref(api_key);
    rt_server_res_send(res_obj, rt_const_cstr("final"));
}

static void read_http_request_headers(void *client) {
    while (true) {
        rt_string line = rt_tcp_recv_line(client);
        const char *cstr = rt_string_cstr(line);
        const bool done = !cstr || *cstr == '\0';
        rt_string_unref(line);
        if (done)
            break;
    }
}

static std::string read_http_cookie_header(void *client) {
    std::string cookie_header;
    while (true) {
        rt_string line = rt_tcp_recv_line(client);
        const char *cstr = rt_string_cstr(line);
        const bool done = !cstr || *cstr == '\0';
        if (cstr && ascii_header_name_equals(cstr, "Cookie", 6) && cstr[6] == ':') {
            const char *value = cstr + 7;
            while (*value == ' ' || *value == '\t')
                value++;
            cookie_header = value;
        }
        rt_string_unref(line);
        if (done)
            break;
    }
    return cookie_header;
}

static std::string read_http_header_value(void *client, const char *name) {
    std::string header_value;
    const size_t name_len = strlen(name);
    while (true) {
        rt_string line = rt_tcp_recv_line(client);
        const char *cstr = rt_string_cstr(line);
        const bool done = !cstr || *cstr == '\0';
        if (cstr && ascii_header_name_equals(cstr, name, name_len) && cstr[name_len] == ':') {
            const char *value = cstr + name_len + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            header_value = value;
        }
        rt_string_unref(line);
        if (done)
            break;
    }
    return header_value;
}

static bool write_text_file(const std::string &path, const char *contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(contents, (std::streamsize)strlen(contents));
    return out.good();
}

static temp_tls_files_t create_temp_tls_files_with_contents(const char *cert_pem,
                                                            const char *key_pem,
                                                            const char *ca_pem = nullptr) {
    temp_tls_files_t files;
    std::error_code ec;
    const auto temp_dir = std::filesystem::temp_directory_path(ec);
    if (ec)
        return files;

    const unsigned id = ++tls_fixture_counter;
    files.cert_path = (temp_dir / ("viper_tls_fixture_" + std::to_string(id) + "_cert.pem")).string();
    files.key_path = (temp_dir / ("viper_tls_fixture_" + std::to_string(id) + "_key.pem")).string();
    if (ca_pem)
        files.ca_path = (temp_dir / ("viper_tls_fixture_" + std::to_string(id) + "_ca.pem")).string();
    if (!write_text_file(files.cert_path, cert_pem) || !write_text_file(files.key_path, key_pem) ||
        (ca_pem && !write_text_file(files.ca_path, ca_pem))) {
        std::error_code cleanup_ec;
        std::filesystem::remove(files.cert_path, cleanup_ec);
        std::filesystem::remove(files.key_path, cleanup_ec);
        if (!files.ca_path.empty())
            std::filesystem::remove(files.ca_path, cleanup_ec);
        files.cert_path.clear();
        files.key_path.clear();
        files.ca_path.clear();
        return files;
    }

    files.valid = true;
    return files;
}

static temp_tls_files_t create_temp_tls_files() {
    return create_temp_tls_files_with_contents(LOCALHOST_TEST_CERT_PEM, LOCALHOST_TEST_KEY_PEM);
}

static bool wait_for_condition(const std::function<bool()> &predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

static int get_bindable_local_port() {
    local_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == LOCAL_SOCK_INVALID)
        return 0;

    int opt = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOCAL_SOCK_CLOSE(fd);
        return 0;
    }

    socklen_t addr_len = (socklen_t)sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        LOCAL_SOCK_CLOSE(fd);
        return 0;
    }

    const int port = ntohs(addr.sin_port);
    LOCAL_SOCK_CLOSE(fd);
    return port;
}

static bool tls_send_all(rt_tls_session_t *tls, const void *data, size_t len) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    size_t sent = 0;
    while (sent < len) {
        const long rc = rt_tls_send(tls, bytes + sent, len - sent);
        if (rc <= 0)
            return false;
        sent += (size_t)rc;
    }
    return true;
}

static bool tls_recv_exact(rt_tls_session_t *tls, void *data, size_t len) {
    uint8_t *bytes = static_cast<uint8_t *>(data);
    size_t received = 0;
    while (received < len) {
        const long rc = rt_tls_recv(tls, bytes + received, len - received);
        if (rc <= 0)
            return false;
        received += (size_t)rc;
    }
    return true;
}

static std::string tls_recv_line(rt_tls_session_t *tls) {
    std::string line;
    while (true) {
        char ch = '\0';
        const long rc = rt_tls_recv(tls, &ch, 1);
        if (rc <= 0)
            return line;
        if (ch == '\n')
            break;
        if (ch != '\r')
            line.push_back(ch);
    }
    return line;
}

static std::vector<std::pair<std::string, std::string>> tls_read_http_headers(rt_tls_session_t *tls) {
    std::vector<std::pair<std::string, std::string>> headers;
    while (true) {
        const std::string line = tls_recv_line(tls);
        if (line.empty())
            break;
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = line.substr(0, colon);
        for (char &ch : name) {
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch + ('a' - 'A'));
        }
        size_t value_start = colon + 1;
        while (value_start < line.size() && (line[value_start] == ' ' || line[value_start] == '\t'))
            value_start++;
        headers.emplace_back(std::move(name), line.substr(value_start));
    }
    return headers;
}

static std::string tls_find_http_header(const std::vector<std::pair<std::string, std::string>> &headers,
                                        const char *name) {
    std::string key(name ? name : "");
    for (char &ch : key) {
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch + ('a' - 'A'));
    }
    for (const auto &entry : headers) {
        if (entry.first == key)
            return entry.second;
    }
    return std::string();
}

static std::string tls_recv_string(rt_tls_session_t *tls, size_t len) {
    std::string out(len, '\0');
    if (len == 0)
        return out;
    if (!tls_recv_exact(tls, out.data(), len))
        return std::string();
    return out;
}

static rt_tls_session_t *connect_local_tls_server(int port) {
    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = "127.0.0.1";
    config.alpn_protocol = "http/1.1";
    config.verify_cert = 0;
    config.timeout_ms = 5000;
    return rt_tls_connect("127.0.0.1", (uint16_t)port, &config);
}

static rt_tls_session_t *connect_local_tls_server_with_alpn(int port, const char *alpn) {
    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = "127.0.0.1";
    config.alpn_protocol = alpn;
    config.verify_cert = 0;
    config.timeout_ms = 5000;
    return rt_tls_connect("127.0.0.1", (uint16_t)port, &config);
}

static rt_tls_session_t *connect_local_tls_server_verified(int port,
                                                           const char *connect_host,
                                                           const char *hostname,
                                                           const char *ca_file) {
    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = hostname;
    config.alpn_protocol = "http/1.1";
    config.ca_file = ca_file;
    config.verify_cert = 1;
    config.timeout_ms = 5000;
    return rt_tls_connect(connect_host, (uint16_t)port, &config);
}

static long tls_http2_read_cb(void *ctx, uint8_t *buf, size_t len) {
    return rt_tls_recv((rt_tls_session_t *)ctx, buf, len);
}

static int tls_http2_write_cb(void *ctx, const uint8_t *buf, size_t len) {
    return rt_tls_send((rt_tls_session_t *)ctx, buf, len) == (long)len;
}

static bool tls_recv_ws_frame(rt_tls_session_t *tls, uint8_t *opcode_out, std::vector<uint8_t> *payload_out) {
    uint8_t header[2] = {0, 0};
    if (!tls_recv_exact(tls, header, sizeof(header)))
        return false;

    uint8_t opcode = (uint8_t)(header[0] & 0x0F);
    int masked = (header[1] & 0x80) != 0;
    size_t payload_len = (size_t)(header[1] & 0x7F);
    if (payload_len == 126) {
        uint8_t ext[2];
        if (!tls_recv_exact(tls, ext, sizeof(ext)))
            return false;
        payload_len = ((size_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!tls_recv_exact(tls, ext, sizeof(ext)))
            return false;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | ext[i];
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !tls_recv_exact(tls, mask, sizeof(mask)))
        return false;

    payload_out->assign(payload_len, 0);
    if (payload_len > 0 && !tls_recv_exact(tls, payload_out->data(), payload_len))
        return false;
    if (masked) {
        for (size_t i = 0; i < payload_out->size(); i++)
            (*payload_out)[i] ^= mask[i & 3];
    }

    if (opcode_out)
        *opcode_out = opcode;
    return true;
}

static bool tls_send_ws_client_close(rt_tls_session_t *tls) {
    const uint8_t close_frame[] = {0x88, 0x80, 0x00, 0x00, 0x00, 0x00};
    return tls_send_all(tls, close_frame, sizeof(close_frame));
}

static void https_server_secure_handler(void *req_obj, void *res_obj) {
    (void)req_obj;
    rt_server_res_send(res_obj, rt_const_cstr("secure-ok"));
}

static void sse_plain_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    read_http_request_headers(client);
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Connection: close\r\n"
              "\r\n"
              "event: greet\r\n"
              "id: 42\r\n"
              "data: hello\r\n"
              "\r\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void sse_chunked_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    read_http_request_headers(client);
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Transfer-Encoding: chunked\r\n"
              "Connection: close\r\n"
              "\r\n");
    // HTTP chunked encoding: each chunk is <hex-size>\r\n<data>\r\n
    // Chunk 1: 0x17 = 23 bytes of SSE event metadata + blank-line delimiter
    // Chunk 2: 0x18 = 24 bytes of SSE data fields + blank-line delimiter
    // Chunk 0: terminates the stream
    send_text(client, "17\r\nevent: multi\r\nid: 7\r\n\r\n\r\n");
    send_text(client, "18\r\ndata: one\r\ndata: two\r\n\r\n\r\n");
    send_text(client, "0\r\n\r\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void sse_resume_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;
    sse_resume_header.clear();

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }
    read_http_request_headers(client);
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Connection: close\r\n"
              "\r\n"
              "retry: 1\r\n"
              "id: 1\r\n"
              "data: first\r\n"
              "\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rt_tcp_close(client);

    client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }
    sse_resume_header = read_http_header_value(client, "Last-Event-ID");
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Connection: close\r\n"
              "\r\n"
              "id: 2\r\n"
              "data: second\r\n"
              "\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void smtp_server_thread(int port,
                               const char *ehlo_response,
                               const char *rcpt_response,
                               bool capture_message) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    smtp_captured_message.clear();
    send_text(client, "220 localhost ESMTP ready\r\n");

    rt_string line = rt_tcp_recv_line(client);
    const char *cstr = rt_string_cstr(line);
    assert(cstr && strncmp(cstr, "EHLO ", 5) == 0);
    rt_string_unref(line);
    send_text(client, ehlo_response);

    if (!capture_message) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        rt_tcp_close(client);
        rt_tcp_server_close(server);
        return;
    }

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strncmp(cstr, "MAIL FROM:", 10) == 0);
    rt_string_unref(line);
    send_text(client, "250 OK\r\n");

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strncmp(cstr, "RCPT TO:", 8) == 0);
    rt_string_unref(line);
    send_text(client, rcpt_response);

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strcmp(cstr, "DATA") == 0);
    rt_string_unref(line);
    send_text(client, "354 End data with <CR><LF>.<CR><LF>\r\n");

    while (true) {
        line = rt_tcp_recv_line(client);
        cstr = rt_string_cstr(line);
        if (cstr && strcmp(cstr, ".") == 0) {
            rt_string_unref(line);
            break;
        }
        smtp_captured_message += cstr ? cstr : "";
        smtp_captured_message += '\n';
        rt_string_unref(line);
    }

    send_text(client, "250 Queued\r\n");

    line = rt_tcp_recv_line(client);
    cstr = rt_string_cstr(line);
    assert(cstr && strcmp(cstr, "QUIT") == 0);
    rt_string_unref(line);
    send_text(client, "221 Bye\r\n");

    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void smtp_plain_server_thread(int port) {
    smtp_server_thread(port, "250-localhost\r\n250 SIZE 1024\r\n", "250 OK\r\n", true);
}

static void smtp_no_starttls_server_thread(int port) {
    smtp_server_thread(port, "250-localhost\r\n250 AUTH LOGIN\r\n", "250 OK\r\n", false);
}

static void smtp_forwarding_rcpt_server_thread(int port) {
    smtp_server_thread(
        port, "250-localhost\r\n250 SIZE 1024\r\n", "251 User not local; will forward\r\n", true);
}

static void http_cookie_scope_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;
    http_cookie_headers[0].clear();
    http_cookie_headers[1].clear();
    http_cookie_headers[2].clear();

    for (int i = 0; i < 3; i++) {
        void *client = rt_tcp_server_accept_for(server, 5000);
        if (!client)
            break;

        rt_string request_line = rt_tcp_recv_line(client);
        rt_string_unref(request_line);
        http_cookie_headers[i] = read_http_cookie_header(client);

        if (i == 0) {
            send_text(client,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Length: 2\r\n"
                      "Set-Cookie: appToken=alpha; Path=/app\r\n"
                      "Set-Cookie: rootToken=beta; Path=/\r\n"
                      "Set-Cookie: secureToken=secret; Path=/app; Secure\r\n"
                      "\r\nok");
        } else if (i == 1) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\napp");
        } else {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nother");
        }

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
}

static void http_keepalive_server_thread(int port) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        api_server_failed = true;
        api_server_ready = true;
        return;
    }

    api_server_failed = false;
    api_server_ready = true;
    http_keepalive_accept_count = 0;
    http_keepalive_reused = false;
    http_keepalive_connection_headers[0].clear();
    http_keepalive_connection_headers[1].clear();

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (!client) {
        rt_tcp_server_close(server);
        return;
    }

    http_keepalive_accept_count = 1;
    rt_string request_line = rt_tcp_recv_line(client);
    rt_string_unref(request_line);
    http_keepalive_connection_headers[0] = read_http_header_value(client, "Connection");
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 2\r\n"
              "Connection: keep-alive\r\n"
              "\r\n"
              "ok");

    void *second_client = rt_tcp_server_accept_for(server, 750);
    if (second_client) {
        http_keepalive_accept_count = 2;
        request_line = rt_tcp_recv_line(second_client);
        rt_string_unref(request_line);
        http_keepalive_connection_headers[1] = read_http_header_value(second_client, "Connection");
        send_text(second_client,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Length: 2\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  "ok");
        rt_tcp_close(second_client);
        rt_tcp_close(client);
        rt_tcp_server_close(server);
        return;
    }

    request_line = rt_tcp_recv_line(client);
    rt_string_unref(request_line);
    http_keepalive_connection_headers[1] = read_http_header_value(client, "Connection");
    http_keepalive_reused = true;
    send_text(client,
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 2\r\n"
              "Connection: close\r\n"
              "\r\n"
              "ok");
    rt_tcp_close(client);
    rt_tcp_server_close(server);
}

static void http_informational_server_thread(int port, std::atomic<bool> *ready, std::atomic<bool> *failed) {
    void *server = rt_tcp_server_listen(port);
    if (!server) {
        if (failed)
            *failed = true;
        if (ready)
            *ready = true;
        return;
    }

    if (failed)
        *failed = false;
    if (ready)
        *ready = true;

    void *client = rt_tcp_server_accept_for(server, 5000);
    if (client) {
        rt_string request_line = rt_tcp_recv_line(client);
        rt_string_unref(request_line);
        read_http_request_headers(client);
        send_text(client,
                  "HTTP/1.1 103 Early Hints\r\n"
                  "Link: </style.css>; rel=preload; as=style\r\n"
                  "\r\n"
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Length: 5\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  "hello");
        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
}

static void wait_for_server() {
    while (!api_server_ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

static void test_sse_plain_event() {
    printf("\nTesting SseClient plain event stream:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(sse_plain_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/events", port);
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data = rt_sse_recv(sse);
    test_result("SSE plain event payload matches", strcmp(rt_string_cstr(data), "hello") == 0);

    rt_string event_type = rt_sse_last_event_type(sse);
    rt_string event_id = rt_sse_last_event_id(sse);
    test_result("SSE plain event type captured", strcmp(rt_string_cstr(event_type), "greet") == 0);
    test_result("SSE plain event id captured", strcmp(rt_string_cstr(event_id), "42") == 0);

    rt_sse_close(sse);
    server.join();
}

static void test_sse_chunked_event() {
    printf("\nTesting SseClient chunked event stream:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(sse_chunked_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/chunked", port);
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE chunked connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data = rt_sse_recv_for(sse, 2000);
    test_result("SSE chunked payload matches", strcmp(rt_string_cstr(data), "one\ntwo") == 0);

    rt_string event_type = rt_sse_last_event_type(sse);
    rt_string event_id = rt_sse_last_event_id(sse);
    test_result("SSE chunked event type captured",
                strcmp(rt_string_cstr(event_type), "multi") == 0);
    test_result("SSE chunked event id captured", strcmp(rt_string_cstr(event_id), "7") == 0);

    rt_sse_close(sse);
    server.join();
}

static void test_sse_resume_after_disconnect() {
    printf("\nTesting SseClient reconnect and resume:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(sse_resume_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/resume", port);
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE resume connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data1 = rt_sse_recv(sse);
    test_result("SSE first event payload matches", strcmp(rt_string_cstr(data1), "first") == 0);
    rt_string first_id = rt_sse_last_event_id(sse);
    test_result("SSE first event id captured", strcmp(rt_string_cstr(first_id), "1") == 0);

    rt_string data2 = rt_sse_recv(sse);
    test_result("SSE reconnect receives next event", strcmp(rt_string_cstr(data2), "second") == 0);
    rt_string second_id = rt_sse_last_event_id(sse);
    test_result("SSE second event id captured", strcmp(rt_string_cstr(second_id), "2") == 0);
    test_result("SSE reconnect sent Last-Event-ID header", sse_resume_header == "1");

    rt_sse_close(sse);
    server.join();
}

static void test_sse_redirect_to_stream() {
    printf("\nTesting SseClient redirect handling:\n");

    const int target_port = (int)rt_netutils_get_free_port();
    const int redirect_port = get_bindable_local_port();
    if (target_port <= 0 || redirect_port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread target_server(sse_plain_server_thread, target_port);
    wait_for_server();
    if (api_server_failed) {
        target_server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *redirect_server = rt_http_server_new(redirect_port);
    rt_http_server_get(redirect_server, rt_const_cstr("/redirect"), rt_const_cstr("redirect"));
    rt_http_server_bind_handler(
        redirect_server, rt_const_cstr("redirect"), (void *)&http_redirect_source_handler);
    rt_http_server_start(redirect_server);

    const bool redirect_ready = wait_for_condition([&]() {
        return rt_http_server_is_running(redirect_server) == 1 &&
               rt_http_server_port(redirect_server) > 0;
    }, 2000);
    if (!redirect_ready) {
        rt_http_server_stop(redirect_server);
        target_server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char location_buf[128];
    snprintf(location_buf, sizeof(location_buf), "http://127.0.0.1:%d/events", target_port);
    redirect_source_location = location_buf;

    char url_buf[128];
    snprintf(url_buf,
             sizeof(url_buf),
             "http://127.0.0.1:%lld/redirect",
             (long long)rt_http_server_port(redirect_server));
    void *sse = rt_sse_connect(rt_const_cstr(url_buf));
    test_result("SSE redirected connection opens", sse != nullptr && rt_sse_is_open(sse) == 1);

    rt_string data = rt_sse_recv(sse);
    test_result("SSE redirected payload matches", strcmp(rt_string_cstr(data), "hello") == 0);
    rt_string event_type = rt_sse_last_event_type(sse);
    test_result("SSE redirected event type captured", strcmp(rt_string_cstr(event_type), "greet") == 0);

    rt_sse_close(sse);
    rt_http_server_stop(redirect_server);
    target_server.join();
}

static void test_smtp_plain_send_sanitizes_and_dot_stuffs() {
    printf("\nTesting SmtpClient plain send path:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(smtp_plain_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *smtp = rt_smtp_new(rt_const_cstr("127.0.0.1"), port);
    int8_t ok = rt_smtp_send(smtp,
                             rt_const_cstr("sender@example.com"),
                             rt_const_cstr("dest@example.com"),
                             rt_const_cstr("Hello\r\nInjected: yes"),
                             rt_const_cstr(".leading line\nsecond line"));

    test_result("SMTP send succeeds", ok == 1);
    test_result("SMTP last error is empty",
                strcmp(rt_string_cstr(rt_smtp_last_error(smtp)), "") == 0);

    server.join();

    test_result("SMTP captured From header",
                smtp_captured_message.find("From: sender@example.com") != std::string::npos);
    test_result("SMTP captured To header",
                smtp_captured_message.find("To: dest@example.com") != std::string::npos);
    test_result("SMTP subject was sanitized",
                smtp_captured_message.find("Subject: Hello  Injected: yes") != std::string::npos);
    test_result("SMTP content type header present",
                smtp_captured_message.find("Content-Type: text/plain; charset=utf-8") !=
                    std::string::npos);
    test_result("SMTP body was dot-stuffed",
                smtp_captured_message.find("..leading line") != std::string::npos);
    test_result("SMTP body preserved following line",
                smtp_captured_message.find("second line") != std::string::npos);
}

static void test_smtp_requires_starttls_capability() {
    printf("\nTesting SmtpClient STARTTLS capability enforcement:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(smtp_no_starttls_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *smtp = rt_smtp_new(rt_const_cstr("127.0.0.1"), port);
    rt_smtp_set_tls(smtp, 1);
    int8_t ok = rt_smtp_send(smtp,
                             rt_const_cstr("sender@example.com"),
                             rt_const_cstr("dest@example.com"),
                             rt_const_cstr("Hello"),
                             rt_const_cstr("Body"));
    rt_string error = rt_smtp_last_error(smtp);

    test_result("SMTP send rejects missing STARTTLS capability", ok == 0);
    test_result("SMTP error mentions STARTTLS",
                strstr(rt_string_cstr(error), "STARTTLS") != nullptr);

    server.join();
}

static void test_smtp_accepts_forwarding_recipient_codes() {
    printf("\nTesting SmtpClient forwarding recipient handling:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(smtp_forwarding_rcpt_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    smtp_captured_message.clear();
    void *smtp = rt_smtp_new(rt_const_cstr("127.0.0.1"), port);
    int8_t ok = rt_smtp_send(smtp,
                             rt_const_cstr("sender@example.com"),
                             rt_const_cstr("dest@example.com"),
                             rt_const_cstr("Forwarded"),
                             rt_const_cstr("Accepted via 251"));

    test_result("SMTP send accepts 251 recipient forwarding", ok == 1);
    server.join();
    test_result("SMTP forwarded message still enters DATA path",
                smtp_captured_message.find("Accepted via 251") != std::string::npos);
}

static void test_http_client_cookie_scope() {
    printf("\nTesting HttpClient cookie scope:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(http_cookie_scope_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    void *client = rt_http_client_new();

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/set", port);
    void *res1 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient initial cookie response succeeds", rt_http_res_status(res1) == 200);
    void *cookies = rt_http_client_get_cookies(client, rt_const_cstr("127.0.0.1"));
    test_result("HttpClient rejects secure cookies set over HTTP",
                rt_map_has(cookies, rt_const_cstr("secureToken")) == 0);
    if (cookies && rt_obj_release_check0(cookies))
        rt_obj_free(cookies);

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/app/data", port);
    void *res2 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient matching-path request succeeds", rt_http_res_status(res2) == 200);

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/other", port);
    void *res3 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient non-matching-path request succeeds", rt_http_res_status(res3) == 200);

    server.join();

    test_result("Path cookie sent on matching path",
                http_cookie_headers[1].find("appToken=alpha") != std::string::npos);
    test_result("Root cookie sent on matching path",
                http_cookie_headers[1].find("rootToken=beta") != std::string::npos);
    test_result("Secure cookie withheld on plain HTTP",
                http_cookie_headers[1].find("secureToken=secret") == std::string::npos);
    test_result("Path cookie withheld outside its path",
                http_cookie_headers[2].find("appToken=alpha") == std::string::npos);
    test_result("Root cookie still sent outside the path",
                http_cookie_headers[2].find("rootToken=beta") != std::string::npos);
}

static void test_http_client_manual_cookie_is_host_only() {
    printf("\nTesting HttpClient manual cookie host scoping:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(http_cookie_scope_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    void *client = rt_http_client_new();
    rt_http_client_set_cookie(
        client, rt_const_cstr("localhost"), rt_const_cstr("manualToken"), rt_const_cstr("hostonly"));

    snprintf(url_buf, sizeof(url_buf), "http://localhost:%d/set", port);
    void *res1 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient localhost cookie request succeeds", rt_http_res_status(res1) == 200);

    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/app/data", port);
    void *res2 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient IP request succeeds", rt_http_res_status(res2) == 200);

    snprintf(url_buf, sizeof(url_buf), "http://localhost:%d/other", port);
    void *res3 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient second localhost request succeeds", rt_http_res_status(res3) == 200);

    server.join();

    test_result("Manual cookie sent to matching host",
                http_cookie_headers[0].find("manualToken=hostonly") != std::string::npos);
    test_result("Manual cookie withheld from different host spelling",
                http_cookie_headers[1].find("manualToken=hostonly") == std::string::npos);
    test_result("Host-only response cookies withheld from different host spelling",
                http_cookie_headers[1].find("rootToken=beta") == std::string::npos);
    test_result("Manual cookie still sent to original host",
                http_cookie_headers[2].find("manualToken=hostonly") != std::string::npos);
}

static void test_http_client_cross_origin_redirect_strips_sensitive_headers() {
    printf("\nTesting HttpClient cross-origin redirect credential stripping:\n");

    const int target_port = get_bindable_local_port();
    const int redirect_port = get_bindable_local_port();
    if (target_port <= 0 || redirect_port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *target_server = rt_http_server_new(target_port);
    void *redirect_server = rt_http_server_new(redirect_port);
    rt_http_server_get(target_server, rt_const_cstr("/final"), rt_const_cstr("target"));
    rt_http_server_bind_handler(target_server, rt_const_cstr("target"), (void *)&http_redirect_target_handler);
    rt_http_server_get(redirect_server, rt_const_cstr("/redirect"), rt_const_cstr("redirect"));
    rt_http_server_bind_handler(
        redirect_server, rt_const_cstr("redirect"), (void *)&http_redirect_source_handler);

    rt_http_server_start(target_server);
    rt_http_server_start(redirect_server);

    const bool ready = wait_for_condition([&]() {
        return rt_http_server_is_running(target_server) == 1 &&
               rt_http_server_is_running(redirect_server) == 1 &&
               rt_http_server_port(target_server) > 0 && rt_http_server_port(redirect_server) > 0;
    }, 2000);
    if (!ready) {
        rt_http_server_stop(redirect_server);
        rt_http_server_stop(target_server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    redirect_target_authorization_header.clear();
    redirect_target_api_key_header.clear();

    char location_buf[128];
    snprintf(location_buf,
             sizeof(location_buf),
             "http://127.0.0.1:%lld/final",
             (long long)rt_http_server_port(target_server));
    redirect_source_location = location_buf;

    char url_buf[128];
    snprintf(url_buf,
             sizeof(url_buf),
             "http://127.0.0.1:%lld/redirect",
             (long long)rt_http_server_port(redirect_server));

    void *client = rt_http_client_new();
    rt_http_client_set_header(client, rt_const_cstr("Authorization"), rt_const_cstr("Bearer secret"));
    rt_http_client_set_header(client, rt_const_cstr("X-API-Key"), rt_const_cstr("top-secret"));

    void *res = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient follows cross-origin redirect", rt_http_res_status(res) == 200);
    rt_http_server_stop(redirect_server);
    rt_http_server_stop(target_server);

    test_result("HttpClient strips Authorization on cross-origin redirect",
                redirect_target_authorization_header.empty());
    test_result("HttpClient strips API key headers on cross-origin redirect",
                redirect_target_api_key_header.empty());
}

static void test_http_client_consumes_informational_responses() {
    printf("\nTesting HttpClient informational response handling:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::thread server(http_informational_server_thread, port, &ready, &failed);
    if (!wait_for_condition([&]() { return ready.load(); }, 1000) || failed.load()) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/hints", port);
    void *client = rt_http_client_new();
    void *res = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient returns the final response after 103",
                rt_http_res_status(res) == 200);
    rt_string body = rt_http_res_body_str(res);
    test_result("HttpClient preserves the final response body", strcmp(rt_string_cstr(body), "hello") == 0);
    rt_string_unref(body);

    server.join();
}

static void test_http_client_keepalive_reuse() {
    printf("\nTesting HttpClient keep-alive reuse:\n");

    const int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    api_server_ready = false;
    api_server_failed = false;
    std::thread server(http_keepalive_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        server.join();
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "http://127.0.0.1:%d/reuse", port);

    void *client = rt_http_client_new();
    test_result("HttpClient keep-alive defaults on", rt_http_client_get_keep_alive(client) == 1);

    void *res1 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient first keep-alive request succeeds", rt_http_res_status(res1) == 200);

    void *res2 = rt_http_client_get(client, rt_const_cstr(url_buf));
    test_result("HttpClient second keep-alive request succeeds", rt_http_res_status(res2) == 200);

    server.join();

    test_result("HttpClient reused a single accepted socket", http_keepalive_reused.load());
    test_result("HttpClient keep-alive did not require a second accept",
                http_keepalive_accept_count.load() == 1);
    test_result("HttpClient first request advertises keep-alive",
                http_keepalive_connection_headers[0].find("keep-alive") != std::string::npos);
    test_result("HttpClient second request advertises keep-alive",
                http_keepalive_connection_headers[1].find("keep-alive") != std::string::npos);
}

static void test_http_server_keepalive_response() {
    printf("\nTesting HttpServer keep-alive responses:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *server = rt_http_server_new(port);
    rt_http_server_get(server, rt_const_cstr("/ping"), rt_const_cstr("ping"));
    rt_http_server_bind_handler(server, rt_const_cstr("ping"), (void *)&http_server_keepalive_handler);
    rt_http_server_start(server);
    if (!wait_for_condition([&]() { return rt_http_server_is_running(server) == 1; }, 1000)) {
        rt_http_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    void *client = rt_tcp_connect(rt_const_cstr("127.0.0.1"), port);

    send_text(client,
              "GET /ping HTTP/1.1\r\n"
              "Host: 127.0.0.1\r\n"
              "Connection: keep-alive\r\n"
              "\r\n");
    rt_string status1 = rt_tcp_recv_line(client);
    test_result("HttpServer first response status is 200",
                strcmp(rt_string_cstr(status1), "HTTP/1.1 200 OK") == 0);
    rt_string_unref(status1);
    std::string connection1 = read_http_header_value(client, "Connection");
    rt_string body1_bytes = nullptr;
    void *body1 = rt_tcp_recv_exact(client, 2);
    body1_bytes = rt_bytes_to_str(body1);
    test_result("HttpServer first response body matches", strcmp(rt_string_cstr(body1_bytes), "ok") == 0);
    rt_string_unref(body1_bytes);

    send_text(client,
              "GET /ping HTTP/1.1\r\n"
              "Host: 127.0.0.1\r\n"
              "Connection: close\r\n"
              "\r\n");
    rt_string status2 = rt_tcp_recv_line(client);
    test_result("HttpServer second response status is 200",
                strcmp(rt_string_cstr(status2), "HTTP/1.1 200 OK") == 0);
    rt_string_unref(status2);
    std::string connection2 = read_http_header_value(client, "Connection");
    void *body2 = rt_tcp_recv_exact(client, 2);
    rt_string body2_str = rt_bytes_to_str(body2);
    test_result("HttpServer second response body matches", strcmp(rt_string_cstr(body2_str), "ok") == 0);
    rt_string_unref(body2_str);

    rt_http_server_stop(server);
    rt_tcp_close(client);

    test_result("HttpServer honors keep-alive on the first response",
                connection1.find("keep-alive") != std::string::npos);
    test_result("HttpServer closes when the request asks for close",
                connection2.find("close") != std::string::npos);
}

static void test_https_server_roundtrip() {
    printf("\nTesting HttpsServer round-trip and TLS keep-alive:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files();
    if (!tls_files.valid) {
        printf("  SKIP: could not write temporary TLS fixture files\n");
        return;
    }

    void *server =
        rt_https_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_https_server_get(server, rt_const_cstr("/secure"), rt_const_cstr("secure"));
    rt_https_server_bind_handler(server, rt_const_cstr("secure"), (void *)&https_server_secure_handler);
    rt_https_server_start(server);
    if (!wait_for_condition([&]() { return rt_https_server_is_running(server) == 1; }, 1000)) {
        rt_https_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    test_result("HttpsServer reports running", rt_https_server_is_running(server) == 1);

    char url_buf[128];
    snprintf(url_buf, sizeof(url_buf), "https://127.0.0.1:%d/secure", port);

    void *req = rt_http_req_new(rt_const_cstr("GET"), rt_const_cstr(url_buf));
    rt_http_req_set_tls_verify(req, 0);
    rt_http_req_set_keep_alive(req, 1);
    rt_http_req_set_force_http1(req, 1); // HTTP/1.1-specific assertion below; don't negotiate h2.
    void *res = rt_http_req_send(req);
    test_result("HttpsServer request status is 200", rt_http_res_status(res) == 200);
    rt_string body = rt_http_res_body_str(res);
    test_result("HttpsServer request body matches", strcmp(rt_string_cstr(body), "secure-ok") == 0);
    rt_string_unref(body);
    rt_string conn = rt_http_res_header(res, rt_const_cstr("Connection"));
    test_result("HttpsServer high-level client sees keep-alive",
                strstr(rt_string_cstr(conn), "keep-alive") != nullptr);
    rt_string_unref(conn);

    rt_tls_config_t bad_sni_config;
    rt_tls_config_init(&bad_sni_config);
    bad_sni_config.hostname = "wrong.example.com";
    bad_sni_config.alpn_protocol = "http/1.1";
    bad_sni_config.verify_cert = 0;
    bad_sni_config.timeout_ms = 5000;
    rt_tls_session_t *bad_sni_tls = rt_tls_connect("127.0.0.1", (uint16_t)port, &bad_sni_config);
    const char *bad_sni_error = rt_tls_last_error();
    test_result("HttpsServer rejects mismatched SNI", bad_sni_tls == nullptr);
    test_result("HttpsServer reports the failed TLS handshake",
                bad_sni_error != nullptr && *bad_sni_error != '\0');
    if (bad_sni_tls)
        rt_tls_close(bad_sni_tls);

    rt_tls_session_t *tls = connect_local_tls_server(port);
    test_result("Raw TLS client connects to HttpsServer", tls != nullptr);
    if (!tls) {
        rt_https_server_stop(server);
        return;
    }

    const char *request1 =
        "GET /secure HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    test_result("HttpsServer accepts first HTTPS request", tls_send_all(tls, request1, strlen(request1)));
    const std::string status1 = tls_recv_line(tls);
    test_result("HttpsServer first raw status is 200", status1 == "HTTP/1.1 200 OK");
    const auto headers1 = tls_read_http_headers(tls);
    const std::string connection1 = tls_find_http_header(headers1, "Connection");
    const std::string length1 = tls_find_http_header(headers1, "Content-Length");
    const std::string body1 = tls_recv_string(tls, (size_t)strtoull(length1.c_str(), nullptr, 10));
    test_result("HttpsServer first raw body matches", body1 == "secure-ok");

    const char *request2 =
        "GET /secure HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    test_result("HttpsServer accepts second HTTPS request", tls_send_all(tls, request2, strlen(request2)));
    const std::string status2 = tls_recv_line(tls);
    test_result("HttpsServer second raw status is 200", status2 == "HTTP/1.1 200 OK");
    const auto headers2 = tls_read_http_headers(tls);
    const std::string connection2 = tls_find_http_header(headers2, "Connection");
    const std::string length2 = tls_find_http_header(headers2, "Content-Length");
    const std::string body2 = tls_recv_string(tls, (size_t)strtoull(length2.c_str(), nullptr, 10));
    test_result("HttpsServer second raw body matches", body2 == "secure-ok");
    test_result("HttpsServer keeps the first TLS response alive", connection1.find("keep-alive") != std::string::npos);
    test_result("HttpsServer closes the second TLS response", connection2.find("close") != std::string::npos);

    rt_tls_close(tls);
    rt_https_server_stop(server);
}

static void test_https_server_http2_roundtrip() {
    printf("\nTesting HttpsServer HTTP/2 ALPN round-trip:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files();
    if (!tls_files.valid) {
        printf("  SKIP: could not write temporary TLS fixture files\n");
        return;
    }

    void *server =
        rt_https_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_https_server_get(server, rt_const_cstr("/secure"), rt_const_cstr("secure"));
    rt_https_server_bind_handler(server, rt_const_cstr("secure"), (void *)&https_server_secure_handler);
    rt_https_server_start(server);
    if (!wait_for_condition([&]() { return rt_https_server_is_running(server) == 1; }, 1000)) {
        rt_https_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    rt_tls_session_t *tls = connect_local_tls_server_with_alpn(port, "h2,http/1.1");
    test_result("HTTP/2 TLS client connects to HttpsServer", tls != nullptr);
    if (!tls) {
        rt_https_server_stop(server);
        return;
    }

    test_result("HttpsServer negotiates h2 over ALPN",
                std::strcmp(rt_tls_get_negotiated_alpn(tls), "h2") == 0);

    rt_http2_io_t io{tls, tls_http2_read_cb, tls_http2_write_cb};
    rt_http2_conn_t *h2 = rt_http2_client_new(&io);
    test_result("HTTP/2 client transport allocates", h2 != nullptr);
    if (!h2) {
        rt_tls_close(tls);
        rt_https_server_stop(server);
        return;
    }

    rt_http2_response_t first{};
    rt_http2_response_t second{};
    bool ok = rt_http2_client_roundtrip(
                  h2, "GET", "https", "127.0.0.1", "/secure", nullptr, nullptr, 0, 1024, &first) == 1 &&
              rt_http2_client_roundtrip(
                  h2, "GET", "https", "127.0.0.1", "/secure", nullptr, nullptr, 0, 1024, &second) == 1;
    test_result("HTTP/2 client completes two requests on one TLS connection", ok);
    test_result("HTTP/2 first response status is 200", first.status == 200);
    test_result("HTTP/2 second response status is 200", second.status == 200);
    test_result("HTTP/2 first response body matches",
                first.body_len == 9 && std::memcmp(first.body, "secure-ok", 9) == 0);
    test_result("HTTP/2 second response body matches",
                second.body_len == 9 && std::memcmp(second.body, "secure-ok", 9) == 0);
    test_result("HTTP/2 second stream id advances",
                first.stream_id == 1 && second.stream_id == 3);

    rt_http2_response_free(&first);
    rt_http2_response_free(&second);
    rt_http2_conn_free(h2);
    rt_tls_close(tls);
    rt_https_server_stop(server);
}

static void test_https_server_rsa_roundtrip_with_verification() {
    printf("\nTesting HttpsServer RSA round-trip with native verification:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files_with_contents(
        LOCALHOST_RSA_LEAF_CERT_PEM, LOCALHOST_RSA_LEAF_KEY_PEM, LOCALHOST_RSA_ROOT_CERT_PEM);
    if (!tls_files.valid || tls_files.ca_path.empty()) {
        printf("  SKIP: could not write temporary RSA TLS fixture files\n");
        return;
    }

    void *server =
        rt_https_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_https_server_get(server, rt_const_cstr("/secure"), rt_const_cstr("secure"));
    rt_https_server_bind_handler(server, rt_const_cstr("secure"), (void *)&https_server_secure_handler);
    rt_https_server_start(server);
    if (!wait_for_condition([&]() { return rt_https_server_is_running(server) == 1; }, 1000)) {
        rt_https_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    test_result("HttpsServer RSA fixture reports running", rt_https_server_is_running(server) == 1);

    rt_tls_session_t *tls =
        connect_local_tls_server_verified(port, "127.0.0.1", "localhost", tls_files.ca_path.c_str());
    test_result("RSA HttpsServer verifies against the custom trust bundle", tls != nullptr);
    if (!tls) {
        rt_https_server_stop(server);
        return;
    }

    const char *request =
        "GET /secure HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    test_result("RSA HttpsServer accepts the verified HTTPS request",
                tls_send_all(tls, request, strlen(request)));
    const std::string status = tls_recv_line(tls);
    test_result("RSA HttpsServer response status is 200", status == "HTTP/1.1 200 OK");
    const auto headers = tls_read_http_headers(tls);
    const std::string length = tls_find_http_header(headers, "Content-Length");
    const std::string body = tls_recv_string(tls, (size_t)strtoull(length.c_str(), nullptr, 10));
    test_result("RSA HttpsServer verified response body matches", body == "secure-ok");

    rt_tls_close(tls);
    rt_https_server_stop(server);
}

static void test_wss_server_broadcast() {
    printf("\nTesting WssServer TLS upgrade and broadcast:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files();
    if (!tls_files.valid) {
        printf("  SKIP: could not write temporary TLS fixture files\n");
        return;
    }

    void *server =
        rt_wss_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_wss_server_start(server);
    if (!wait_for_condition([&]() { return rt_wss_server_is_running(server) == 1; }, 1000)) {
        rt_wss_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    test_result("WssServer reports running", rt_wss_server_is_running(server) == 1);

    rt_tls_session_t *tls = connect_local_tls_server(port);
    test_result("Raw TLS client connects to WssServer", tls != nullptr);
    if (!tls) {
        rt_wss_server_stop(server);
        return;
    }

    const char *ws_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char request[512];
    snprintf(request,
             sizeof(request),
             "GET /chat HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             port,
             ws_key);
    test_result("WssServer accepts TLS WebSocket upgrade request", tls_send_all(tls, request, strlen(request)));
    const std::string status = tls_recv_line(tls);
    test_result("WssServer returns 101 Switching Protocols", status == "HTTP/1.1 101 Switching Protocols");
    const auto headers = tls_read_http_headers(tls);
    const std::string accept = tls_find_http_header(headers, "Sec-WebSocket-Accept");
    char *expected_accept = rt_ws_compute_accept_key(ws_key);
    test_result("WssServer returns correct Sec-WebSocket-Accept",
                expected_accept != nullptr && accept == expected_accept);
    free(expected_accept);

    test_result("WssServer tracks the connected client",
                wait_for_condition([&]() { return rt_wss_server_client_count(server) == 1; }, 1000));

    rt_wss_server_broadcast(server, rt_const_cstr("broadcast"));
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
    test_result("WssServer sends a text frame", tls_recv_ws_frame(tls, &opcode, &payload));
    test_result("WssServer text frame opcode is correct", opcode == 0x01);
    test_result("WssServer text frame payload matches",
                std::string(reinterpret_cast<const char *>(payload.data()), payload.size()) == "broadcast");

    void *binary = rt_bytes_from_str(rt_const_cstr("bin"));
    rt_wss_server_broadcast_bytes(server, binary);
    payload.clear();
    test_result("WssServer sends a binary frame", tls_recv_ws_frame(tls, &opcode, &payload));
    test_result("WssServer binary frame opcode is correct", opcode == 0x02);
    test_result("WssServer binary frame payload matches",
                std::string(reinterpret_cast<const char *>(payload.data()), payload.size()) == "bin");

    test_result("WssServer accepts a client close frame", tls_send_ws_client_close(tls));
    payload.clear();
    test_result("WssServer replies with a close frame", tls_recv_ws_frame(tls, &opcode, &payload));
    test_result("WssServer close frame opcode is correct", opcode == 0x08);

    rt_tls_close(tls);
    test_result("WssServer removes the closed client",
                wait_for_condition([&]() { return rt_wss_server_client_count(server) == 0; }, 1000));
    rt_wss_server_stop(server);
}

static void test_wss_server_rejects_invalid_sec_websocket_key() {
    printf("\nTesting WssServer rejects malformed handshake keys:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files();
    if (!tls_files.valid) {
        printf("  SKIP: could not write temporary TLS fixture files\n");
        return;
    }

    void *server =
        rt_wss_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_wss_server_start(server);
    if (!wait_for_condition([&]() { return rt_wss_server_is_running(server) == 1; }, 1000)) {
        rt_wss_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    rt_tls_session_t *tls = connect_local_tls_server(port);
    test_result("Raw TLS client connects to WssServer", tls != nullptr);
    if (!tls) {
        rt_wss_server_stop(server);
        return;
    }

    char request[512];
    snprintf(request,
             sizeof(request),
             "GET /chat HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: bad\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             port);
    test_result("Malformed WSS handshake request is sent", tls_send_all(tls, request, strlen(request)));

    const std::string status = tls_recv_line(tls);
    test_result("WssServer does not upgrade malformed handshake", status.empty());
    test_result("WssServer keeps zero connected clients",
                wait_for_condition([&]() { return rt_wss_server_client_count(server) == 0; }, 250));

    rt_tls_close(tls);
    rt_wss_server_stop(server);
}

static void test_wss_server_rejects_invalid_host_header() {
    printf("\nTesting WssServer rejects malformed Host headers:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files();
    if (!tls_files.valid) {
        printf("  SKIP: could not write temporary TLS fixture files\n");
        return;
    }

    void *server =
        rt_wss_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_wss_server_start(server);
    if (!wait_for_condition([&]() { return rt_wss_server_is_running(server) == 1; }, 1000)) {
        rt_wss_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    rt_tls_session_t *tls = connect_local_tls_server(port);
    test_result("Raw TLS client connects to WssServer", tls != nullptr);
    if (!tls) {
        rt_wss_server_stop(server);
        return;
    }

    const char *request =
        "GET /chat HTTP/1.1\r\n"
        "Host: ::1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    test_result("Malformed Host handshake request is sent", tls_send_all(tls, request, strlen(request)));

    const std::string status = tls_recv_line(tls);
    test_result("WssServer does not upgrade malformed Host handshake", status.empty());
    test_result("WssServer keeps zero connected clients",
                wait_for_condition([&]() { return rt_wss_server_client_count(server) == 0; }, 250));

    rt_tls_close(tls);
    rt_wss_server_stop(server);
}

static void test_wss_server_subprotocol_negotiation() {
    printf("\nTesting WssServer subprotocol negotiation:\n");

    const int port = get_bindable_local_port();
    if (port <= 0) {
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    temp_tls_files_t tls_files = create_temp_tls_files();
    if (!tls_files.valid) {
        printf("  SKIP: could not write temporary TLS fixture files\n");
        return;
    }

    void *server =
        rt_wss_server_new(port, rt_const_cstr(tls_files.cert_path.c_str()), rt_const_cstr(tls_files.key_path.c_str()));
    rt_wss_server_set_subprotocol(server, rt_const_cstr("chat.v1"));
    rt_wss_server_start(server);
    if (!wait_for_condition([&]() { return rt_wss_server_is_running(server) == 1; }, 1000)) {
        rt_wss_server_stop(server);
        printf("  SKIP: local bind unavailable in this environment\n");
        return;
    }

    rt_tls_session_t *tls = connect_local_tls_server(port);
    test_result("Raw TLS client connects to WssServer", tls != nullptr);
    if (!tls) {
        rt_wss_server_stop(server);
        return;
    }

    char request[768];
    snprintf(request,
             sizeof(request),
             "GET /chat HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Sec-WebSocket-Protocol: other, chat.v1\r\n"
             "\r\n",
             port);
    test_result("Matching subprotocol handshake request is sent", tls_send_all(tls, request, strlen(request)));
    const std::string status = tls_recv_line(tls);
    test_result("WssServer upgrades matching subprotocol handshake", status == "HTTP/1.1 101 Switching Protocols");
    const auto headers = tls_read_http_headers(tls);
    test_result("WssServer returns negotiated subprotocol",
                tls_find_http_header(headers, "Sec-WebSocket-Protocol") == "chat.v1");
    rt_tls_close(tls);
    test_result("WssServer removes the negotiated client",
                wait_for_condition([&]() { return rt_wss_server_client_count(server) == 0; }, 1000));

    tls = connect_local_tls_server(port);
    test_result("Second raw TLS client connects to WssServer", tls != nullptr);
    if (!tls) {
        rt_wss_server_stop(server);
        return;
    }

    snprintf(request,
             sizeof(request),
             "GET /chat HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Sec-WebSocket-Protocol: other\r\n"
             "\r\n",
             port);
    test_result("Mismatched subprotocol handshake request is sent", tls_send_all(tls, request, strlen(request)));
    test_result("WssServer rejects missing required subprotocol", tls_recv_line(tls).empty());
    test_result("WssServer keeps zero connected clients after rejection",
                wait_for_condition([&]() { return rt_wss_server_client_count(server) == 0; }, 250));

    rt_tls_close(tls);
    rt_wss_server_stop(server);
}

int main() {
    test_sse_plain_event();
    test_sse_chunked_event();
    test_sse_resume_after_disconnect();
    test_sse_redirect_to_stream();
    test_http_client_cookie_scope();
    test_http_client_manual_cookie_is_host_only();
    test_http_client_cross_origin_redirect_strips_sensitive_headers();
    test_http_client_consumes_informational_responses();
    test_http_client_keepalive_reuse();
    test_http_server_keepalive_response();
    test_https_server_roundtrip();
    test_https_server_http2_roundtrip();
    test_https_server_rsa_roundtrip_with_verification();
    test_wss_server_broadcast();
    test_wss_server_rejects_invalid_sec_websocket_key();
    test_wss_server_rejects_invalid_host_header();
    test_wss_server_subprotocol_negotiation();
    test_smtp_plain_send_sanitizes_and_dot_stuffs();
    test_smtp_requires_starttls_capability();
    test_smtp_accepts_forwarding_recipient_codes();

    printf("\nAll high-level network tests passed.\n");
    return 0;
}
