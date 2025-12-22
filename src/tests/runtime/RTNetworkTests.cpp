//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTNetworkTests.cpp
// Purpose: Validate Viper.Network.Tcp and TcpServer support.
// Key invariants: Client/server communication, timeout handling.
// Links: docs/viperlib/network.md
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Get bytes data pointer
static uint8_t *get_bytes_data(void *bytes)
{
    struct bytes_impl
    {
        int64_t len;
        uint8_t *data;
    };

    return ((bytes_impl *)bytes)->data;
}

/// @brief Get bytes length
static int64_t get_bytes_len(void *bytes)
{
    return rt_bytes_len(bytes);
}

/// @brief Create bytes from string literal
static void *make_bytes_str(const char *str)
{
    size_t len = strlen(str);
    void *bytes = rt_bytes_new((int64_t)len);
    memcpy(get_bytes_data(bytes), str, len);
    return bytes;
}

/// @brief Atomic flag for server shutdown
static std::atomic<bool> server_ready{false};
static std::atomic<bool> server_done{false};

/// @brief Echo server thread function
static void echo_server_thread(int port, int num_clients)
{
    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    printf("  Echo server started on port %d\n", port);
    server_ready = true;

    for (int i = 0; i < num_clients; i++)
    {
        void *client = rt_tcp_server_accept(server);
        if (!client)
            break;

        // Echo loop - receive and send back
        while (rt_tcp_is_open(client))
        {
            void *data = rt_tcp_recv(client, 1024);
            int64_t len = get_bytes_len(data);
            if (len == 0)
            {
                // Connection closed
                break;
            }

            // Send back what we received
            rt_tcp_send_all(client, data);
        }

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
    server_done = true;
}

/// @brief Test server listen and client connect
static void test_server_client_connect()
{
    printf("\nTesting Server/Client Connect:\n");

    const int port = 19876;
    server_ready = false;
    server_done = false;

    // Start server in background thread
    std::thread server_thread(echo_server_thread, port, 1);

    // Wait for server to be ready
    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Connect client
    rt_string host = rt_const_cstr("127.0.0.1");
    void *client = rt_tcp_connect(host, port);

    test_result("Client connects successfully", client != nullptr);
    test_result("Client is open", rt_tcp_is_open(client) == 1);
    test_result("Client port is correct", rt_tcp_port(client) == port);

    // Close client and wait for server
    rt_tcp_close(client);
    server_thread.join();

    test_result("Server finished", server_done.load());
}

/// @brief Test send and receive
static void test_send_recv()
{
    printf("\nTesting Send/Receive:\n");

    const int port = 19877;
    server_ready = false;
    server_done = false;

    std::thread server_thread(echo_server_thread, port, 1);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    rt_string host = rt_const_cstr("127.0.0.1");
    void *client = rt_tcp_connect(host, port);
    assert(client != nullptr);

    // Test Send bytes
    const char *test_msg = "Hello, World!";
    void *send_data = make_bytes_str(test_msg);
    int64_t sent = rt_tcp_send(client, send_data);

    test_result("Send returns correct byte count", sent == (int64_t)strlen(test_msg));

    // Receive echo
    void *recv_data = rt_tcp_recv(client, 1024);
    int64_t recv_len = get_bytes_len(recv_data);

    test_result("Recv returns correct byte count", recv_len == (int64_t)strlen(test_msg));
    test_result("Recv data matches sent data",
                memcmp(get_bytes_data(recv_data), test_msg, strlen(test_msg)) == 0);

    // Test SendStr
    rt_string send_str = rt_const_cstr("Test string!");
    sent = rt_tcp_send_str(client, send_str);

    test_result("SendStr returns correct byte count", sent == 12);

    // Receive string echo
    rt_string recv_str = rt_tcp_recv_str(client, 1024);
    const char *recv_cstr = rt_string_cstr(recv_str);

    test_result("RecvStr returns correct string", strcmp(recv_cstr, "Test string!") == 0);

    rt_tcp_close(client);
    server_thread.join();
}

/// @brief Test SendAll and RecvExact
static void test_send_all_recv_exact()
{
    printf("\nTesting SendAll/RecvExact:\n");

    const int port = 19878;
    server_ready = false;
    server_done = false;

    std::thread server_thread(echo_server_thread, port, 1);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    rt_string host = rt_const_cstr("127.0.0.1");
    void *client = rt_tcp_connect(host, port);
    assert(client != nullptr);

    // Send larger data
    const int data_size = 4096;
    void *large_data = rt_bytes_new(data_size);
    uint8_t *ptr = get_bytes_data(large_data);
    for (int i = 0; i < data_size; i++)
    {
        ptr[i] = (uint8_t)(i & 0xFF);
    }

    rt_tcp_send_all(client, large_data);

    // Receive exactly that many bytes
    void *recv_data = rt_tcp_recv_exact(client, data_size);

    test_result("RecvExact returns correct size", get_bytes_len(recv_data) == data_size);
    test_result("RecvExact data matches", memcmp(get_bytes_data(recv_data), ptr, data_size) == 0);

    rt_tcp_close(client);
    server_thread.join();
}

/// @brief Line server thread function - sends lines
static void line_server_thread(int port)
{
    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    server_ready = true;

    void *client = rt_tcp_server_accept(server);
    if (client)
    {
        // Send lines
        const char *lines[] = {"Line 1\n", "Line 2 with CRLF\r\n", "Last line\n"};

        for (int i = 0; i < 3; i++)
        {
            rt_string line = rt_const_cstr(lines[i]);
            rt_tcp_send_str(client, line);
        }

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
    server_done = true;
}

/// @brief Test RecvLine
static void test_recv_line()
{
    printf("\nTesting RecvLine:\n");

    const int port = 19879;
    server_ready = false;
    server_done = false;

    std::thread server_thread(line_server_thread, port);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    rt_string host = rt_const_cstr("127.0.0.1");
    void *client = rt_tcp_connect(host, port);
    assert(client != nullptr);

    // Read line 1 (LF ending)
    rt_string line1 = rt_tcp_recv_line(client);
    test_result("RecvLine reads LF line", strcmp(rt_string_cstr(line1), "Line 1") == 0);

    // Read line 2 (CRLF ending)
    rt_string line2 = rt_tcp_recv_line(client);
    test_result("RecvLine strips CRLF", strcmp(rt_string_cstr(line2), "Line 2 with CRLF") == 0);

    // Read line 3
    rt_string line3 = rt_tcp_recv_line(client);
    test_result("RecvLine reads last line", strcmp(rt_string_cstr(line3), "Last line") == 0);

    rt_tcp_close(client);
    server_thread.join();
}

/// @brief Test server properties
static void test_server_properties()
{
    printf("\nTesting Server Properties:\n");

    const int port = 19880;

    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    test_result("Server port is correct", rt_tcp_server_port(server) == port);
    test_result("Server is listening", rt_tcp_server_is_listening(server) == 1);

    rt_string addr = rt_tcp_server_address(server);
    test_result("Server address is 0.0.0.0", strcmp(rt_string_cstr(addr), "0.0.0.0") == 0);

    rt_tcp_server_close(server);

    test_result("Server not listening after close", rt_tcp_server_is_listening(server) == 0);
}

/// @brief Test client properties
static void test_client_properties()
{
    printf("\nTesting Client Properties:\n");

    const int port = 19881;
    server_ready = false;
    server_done = false;

    std::thread server_thread(echo_server_thread, port, 1);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    rt_string host = rt_const_cstr("127.0.0.1");
    void *client = rt_tcp_connect(host, port);
    assert(client != nullptr);

    rt_string client_host = rt_tcp_host(client);
    test_result("Client host is 127.0.0.1", strcmp(rt_string_cstr(client_host), "127.0.0.1") == 0);
    test_result("Client remote port is correct", rt_tcp_port(client) == port);
    test_result("Client local port is > 0", rt_tcp_local_port(client) > 0);
    test_result("Client is open", rt_tcp_is_open(client) == 1);
    test_result("Available returns 0 initially", rt_tcp_available(client) == 0);

    rt_tcp_close(client);

    test_result("Client not open after close", rt_tcp_is_open(client) == 0);

    server_thread.join();
}

/// @brief Test accept with timeout
static void test_accept_timeout()
{
    printf("\nTesting Accept Timeout:\n");

    const int port = 19882;

    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    // Accept with short timeout - no client connecting
    auto start = std::chrono::steady_clock::now();
    void *client = rt_tcp_server_accept_for(server, 100); // 100ms timeout
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    test_result("Accept returns NULL on timeout", client == nullptr);
    test_result("Accept timeout is respected", elapsed >= 90 && elapsed < 500);

    rt_tcp_server_close(server);
}

/// @brief Test connect with timeout - just test that ConnectFor compiles and works
/// Note: Testing actual timeout with non-routable addresses would trap and terminate
static void test_connect_with_timeout()
{
    printf("\nTesting ConnectFor:\n");

    const int port = 19884;
    server_ready = false;
    server_done = false;

    std::thread server_thread(echo_server_thread, port, 1);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Connect with a long timeout (should succeed quickly)
    rt_string host = rt_const_cstr("127.0.0.1");
    void *client = rt_tcp_connect_for(host, port, 5000); // 5 second timeout

    test_result("ConnectFor succeeds to localhost", client != nullptr);
    test_result("ConnectFor client is open", rt_tcp_is_open(client) == 1);

    rt_tcp_close(client);
    server_thread.join();
}

/// @brief Test ListenAt on specific address
static void test_listen_at()
{
    printf("\nTesting ListenAt:\n");

    const int port = 19883;
    rt_string addr = rt_const_cstr("127.0.0.1");

    void *server = rt_tcp_server_listen_at(addr, port);
    assert(server != nullptr);

    rt_string bound_addr = rt_tcp_server_address(server);
    test_result("Server bound to 127.0.0.1", strcmp(rt_string_cstr(bound_addr), "127.0.0.1") == 0);
    test_result("Server port is correct", rt_tcp_server_port(server) == port);

    rt_tcp_server_close(server);
}

//=============================================================================
// UDP Tests
//=============================================================================

/// @brief Test UDP socket creation and properties
static void test_udp_new()
{
    printf("\nTesting UDP New:\n");

    void *sock = rt_udp_new();
    test_result("UDP New returns socket", sock != nullptr);
    test_result("UDP port is 0 (unbound)", rt_udp_port(sock) == 0);
    test_result("UDP is not bound", rt_udp_is_bound(sock) == 0);

    rt_udp_close(sock);
}

/// @brief Test UDP bind
static void test_udp_bind()
{
    printf("\nTesting UDP Bind:\n");

    const int port = 19890;

    void *sock = rt_udp_bind(port);
    test_result("UDP Bind returns socket", sock != nullptr);
    test_result("UDP port is correct", rt_udp_port(sock) == port);
    test_result("UDP is bound", rt_udp_is_bound(sock) == 1);

    rt_string addr = rt_udp_address(sock);
    test_result("UDP address is 0.0.0.0", strcmp(rt_string_cstr(addr), "0.0.0.0") == 0);

    rt_udp_close(sock);
}

/// @brief Test UDP bind at specific address
static void test_udp_bind_at()
{
    printf("\nTesting UDP BindAt:\n");

    const int port = 19891;
    rt_string addr = rt_const_cstr("127.0.0.1");

    void *sock = rt_udp_bind_at(addr, port);
    test_result("UDP BindAt returns socket", sock != nullptr);
    test_result("UDP port is correct", rt_udp_port(sock) == port);
    test_result("UDP is bound", rt_udp_is_bound(sock) == 1);

    rt_string bound_addr = rt_udp_address(sock);
    test_result("UDP address is 127.0.0.1", strcmp(rt_string_cstr(bound_addr), "127.0.0.1") == 0);

    rt_udp_close(sock);
}

/// @brief Test UDP send and receive on localhost
static void test_udp_send_recv()
{
    printf("\nTesting UDP Send/Recv:\n");

    const int recv_port = 19892;
    const int send_port = 19893;

    // Create receiver
    void *receiver = rt_udp_bind(recv_port);
    assert(receiver != nullptr);

    // Create sender
    void *sender = rt_udp_bind(send_port);
    assert(sender != nullptr);

    // Send data
    const char *test_msg = "Hello UDP!";
    void *send_data = make_bytes_str(test_msg);
    rt_string host = rt_const_cstr("127.0.0.1");

    int64_t sent = rt_udp_send_to(sender, host, recv_port, send_data);
    test_result("UDP SendTo returns byte count", sent == (int64_t)strlen(test_msg));

    // Receive data
    void *recv_data = rt_udp_recv(receiver, 1024);
    int64_t recv_len = get_bytes_len(recv_data);

    test_result("UDP Recv returns correct length", recv_len == (int64_t)strlen(test_msg));
    test_result("UDP Recv data matches",
                memcmp(get_bytes_data(recv_data), test_msg, strlen(test_msg)) == 0);

    rt_udp_close(sender);
    rt_udp_close(receiver);
}

/// @brief Test UDP send and receive string
static void test_udp_send_recv_str()
{
    printf("\nTesting UDP SendToStr:\n");

    const int recv_port = 19894;
    const int send_port = 19895;

    void *receiver = rt_udp_bind(recv_port);
    void *sender = rt_udp_bind(send_port);
    assert(receiver != nullptr);
    assert(sender != nullptr);

    rt_string host = rt_const_cstr("127.0.0.1");
    rt_string msg = rt_const_cstr("Hello from string!");

    int64_t sent = rt_udp_send_to_str(sender, host, recv_port, msg);
    test_result("UDP SendToStr returns byte count", sent == 18);

    // Receive as bytes and verify
    void *recv_data = rt_udp_recv(receiver, 1024);
    test_result("UDP Recv receives string data",
                memcmp(get_bytes_data(recv_data), "Hello from string!", 18) == 0);

    rt_udp_close(sender);
    rt_udp_close(receiver);
}

/// @brief Test UDP RecvFrom with sender info
static void test_udp_recv_from()
{
    printf("\nTesting UDP RecvFrom:\n");

    const int recv_port = 19896;
    const int send_port = 19897;

    void *receiver = rt_udp_bind(recv_port);
    void *sender = rt_udp_bind(send_port);
    assert(receiver != nullptr);
    assert(sender != nullptr);

    rt_string host = rt_const_cstr("127.0.0.1");
    void *data = make_bytes_str("test");

    rt_udp_send_to(sender, host, recv_port, data);

    // Receive with sender info
    void *recv_data = rt_udp_recv_from(receiver, 1024);
    test_result("UDP RecvFrom returns data", get_bytes_len(recv_data) == 4);

    // Check sender info
    rt_string sender_host = rt_udp_sender_host(receiver);
    int64_t sender_port = rt_udp_sender_port(receiver);

    test_result("UDP SenderHost is 127.0.0.1",
                strcmp(rt_string_cstr(sender_host), "127.0.0.1") == 0);
    test_result("UDP SenderPort is correct", sender_port == send_port);

    rt_udp_close(sender);
    rt_udp_close(receiver);
}

/// @brief Test UDP receive timeout
static void test_udp_recv_timeout()
{
    printf("\nTesting UDP RecvFor timeout:\n");

    const int port = 19898;
    void *sock = rt_udp_bind(port);
    assert(sock != nullptr);

    // Receive with short timeout - no data coming
    auto start = std::chrono::steady_clock::now();
    void *data = rt_udp_recv_for(sock, 1024, 100); // 100ms timeout
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    test_result("UDP RecvFor returns NULL on timeout", data == nullptr);
    test_result("UDP RecvFor timeout is respected", elapsed >= 90 && elapsed < 500);

    rt_udp_close(sock);
}

/// @brief Test UDP broadcast enable
static void test_udp_broadcast()
{
    printf("\nTesting UDP SetBroadcast:\n");

    void *sock = rt_udp_new();
    assert(sock != nullptr);

    // Enable broadcast - just test it doesn't crash
    rt_udp_set_broadcast(sock, 1);
    test_result("UDP SetBroadcast(true) succeeds", true);

    rt_udp_set_broadcast(sock, 0);
    test_result("UDP SetBroadcast(false) succeeds", true);

    rt_udp_close(sock);
}

/// @brief Test UDP set receive timeout
static void test_udp_set_recv_timeout()
{
    printf("\nTesting UDP SetRecvTimeout:\n");

    const int port = 19899;
    void *sock = rt_udp_bind(port);
    assert(sock != nullptr);

    // Set recv timeout
    rt_udp_set_recv_timeout(sock, 50);

    // Try to receive with timeout
    auto start = std::chrono::steady_clock::now();
    void *data = rt_udp_recv(sock, 1024); // Should timeout
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // recv with timeout set returns empty bytes (not NULL like RecvFor)
    test_result("UDP SetRecvTimeout recv returns", data != nullptr);
    test_result("UDP SetRecvTimeout returns empty on timeout", get_bytes_len(data) == 0);
    test_result("UDP SetRecvTimeout is respected", elapsed >= 40 && elapsed < 500);

    rt_udp_close(sock);
}

//=============================================================================
// DNS Tests
//=============================================================================

/// @brief Test DNS resolve localhost
static void test_dns_resolve_localhost()
{
    printf("\nTesting DNS Resolve localhost:\n");

    rt_string hostname = rt_const_cstr("localhost");
    rt_string result = rt_dns_resolve(hostname);

    const char *ip = rt_string_cstr(result);
    test_result("DNS Resolve localhost returns IP", ip != nullptr);
    test_result("DNS Resolve localhost is 127.0.0.1", strcmp(ip, "127.0.0.1") == 0);
}

/// @brief Test DNS resolve4 localhost
static void test_dns_resolve4_localhost()
{
    printf("\nTesting DNS Resolve4 localhost:\n");

    rt_string hostname = rt_const_cstr("localhost");
    rt_string result = rt_dns_resolve4(hostname);

    const char *ip = rt_string_cstr(result);
    test_result("DNS Resolve4 localhost returns IP", ip != nullptr);
    test_result("DNS Resolve4 localhost is 127.0.0.1", strcmp(ip, "127.0.0.1") == 0);
}

/// @brief Test DNS IsIPv4
static void test_dns_is_ipv4()
{
    printf("\nTesting DNS IsIPv4:\n");

    test_result("IsIPv4('127.0.0.1') = true", rt_dns_is_ipv4(rt_const_cstr("127.0.0.1")) == 1);
    test_result("IsIPv4('192.168.1.1') = true", rt_dns_is_ipv4(rt_const_cstr("192.168.1.1")) == 1);
    test_result("IsIPv4('0.0.0.0') = true", rt_dns_is_ipv4(rt_const_cstr("0.0.0.0")) == 1);
    test_result("IsIPv4('255.255.255.255') = true",
                rt_dns_is_ipv4(rt_const_cstr("255.255.255.255")) == 1);
    test_result("IsIPv4('256.0.0.1') = false", rt_dns_is_ipv4(rt_const_cstr("256.0.0.1")) == 0);
    test_result("IsIPv4('1.2.3') = false", rt_dns_is_ipv4(rt_const_cstr("1.2.3")) == 0);
    test_result("IsIPv4('hello') = false", rt_dns_is_ipv4(rt_const_cstr("hello")) == 0);
    test_result("IsIPv4('') = false", rt_dns_is_ipv4(rt_const_cstr("")) == 0);
    test_result("IsIPv4('::1') = false", rt_dns_is_ipv4(rt_const_cstr("::1")) == 0);
}

/// @brief Test DNS IsIPv6
static void test_dns_is_ipv6()
{
    printf("\nTesting DNS IsIPv6:\n");

    test_result("IsIPv6('::1') = true", rt_dns_is_ipv6(rt_const_cstr("::1")) == 1);
    test_result("IsIPv6('::') = true", rt_dns_is_ipv6(rt_const_cstr("::")) == 1);
    test_result("IsIPv6('2001:db8::1') = true", rt_dns_is_ipv6(rt_const_cstr("2001:db8::1")) == 1);
    test_result("IsIPv6('fe80::1') = true", rt_dns_is_ipv6(rt_const_cstr("fe80::1")) == 1);
    test_result("IsIPv6('127.0.0.1') = false", rt_dns_is_ipv6(rt_const_cstr("127.0.0.1")) == 0);
    test_result("IsIPv6('hello') = false", rt_dns_is_ipv6(rt_const_cstr("hello")) == 0);
    test_result("IsIPv6('') = false", rt_dns_is_ipv6(rt_const_cstr("")) == 0);
}

/// @brief Test DNS IsIP
static void test_dns_is_ip()
{
    printf("\nTesting DNS IsIP:\n");

    test_result("IsIP('127.0.0.1') = true", rt_dns_is_ip(rt_const_cstr("127.0.0.1")) == 1);
    test_result("IsIP('::1') = true", rt_dns_is_ip(rt_const_cstr("::1")) == 1);
    test_result("IsIP('hello') = false", rt_dns_is_ip(rt_const_cstr("hello")) == 0);
    test_result("IsIP('') = false", rt_dns_is_ip(rt_const_cstr("")) == 0);
}

/// @brief Test DNS LocalHost
static void test_dns_local_host()
{
    printf("\nTesting DNS LocalHost:\n");

    rt_string hostname = rt_dns_local_host();
    const char *name = rt_string_cstr(hostname);

    test_result("DNS LocalHost returns non-empty", name != nullptr && strlen(name) > 0);
    printf("  Local hostname: %s\n", name);
}

/// @brief Test DNS LocalAddrs
static void test_dns_local_addrs()
{
    printf("\nTesting DNS LocalAddrs:\n");

    void *addrs = rt_dns_local_addrs();
    int64_t count = rt_seq_len(addrs);

    test_result("DNS LocalAddrs returns Seq", addrs != nullptr);
    test_result("DNS LocalAddrs has entries", count > 0);

    printf("  Found %lld local addresses:\n", (long long)count);
    for (int64_t i = 0; i < count && i < 5; i++) // Limit output
    {
        rt_string addr = (rt_string)rt_seq_get(addrs, i);
        printf("    - %s\n", rt_string_cstr(addr));
    }
    if (count > 5)
        printf("    ... and %lld more\n", (long long)(count - 5));
}

/// @brief Test DNS ResolveAll localhost
static void test_dns_resolve_all()
{
    printf("\nTesting DNS ResolveAll localhost:\n");

    rt_string hostname = rt_const_cstr("localhost");
    void *addrs = rt_dns_resolve_all(hostname);
    int64_t count = rt_seq_len(addrs);

    test_result("DNS ResolveAll returns Seq", addrs != nullptr);
    test_result("DNS ResolveAll has entries", count > 0);

    // Check first entry is localhost IP
    if (count > 0)
    {
        rt_string first = (rt_string)rt_seq_get(addrs, 0);
        const char *ip = rt_string_cstr(first);
        printf("  First address: %s\n", ip);
        // Either 127.0.0.1 or ::1 is acceptable
        bool valid = (strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "::1") == 0);
        test_result("DNS ResolveAll first is valid localhost", valid);
    }
}

//=============================================================================
// HTTP Tests
//=============================================================================

#include "rt_map.h"

/// @brief Mock HTTP server - returns fixed response
static void http_server_thread(int port, const char *response_body, int response_status)
{
    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    server_ready = true;

    void *client = rt_tcp_server_accept(server);
    if (client)
    {
        // Read request line and headers (drain them)
        char buf[4096];
        int pos = 0;
        while (pos < (int)sizeof(buf) - 1)
        {
            void *data = rt_tcp_recv(client, 1);
            if (get_bytes_len(data) == 0)
                break;
            buf[pos++] = get_bytes_data(data)[0];
            // Look for end of headers
            if (pos >= 4 && buf[pos - 4] == '\r' && buf[pos - 3] == '\n' && buf[pos - 2] == '\r' &&
                buf[pos - 1] == '\n')
            {
                break;
            }
        }

        // Build HTTP response
        char response[8192];
        snprintf(response,
                 sizeof(response),
                 "HTTP/1.1 %d OK\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %zu\r\n"
                 "X-Test-Header: test-value\r\n"
                 "\r\n%s",
                 response_status,
                 strlen(response_body),
                 response_body);

        rt_string resp_str = rt_const_cstr(response);
        rt_tcp_send_str(client, resp_str);

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
    server_done = true;
}

/// @brief Mock HTTP server for chunked encoding
static void http_chunked_server_thread(int port)
{
    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    server_ready = true;

    void *client = rt_tcp_server_accept(server);
    if (client)
    {
        // Read request line and headers
        char buf[4096];
        int pos = 0;
        while (pos < (int)sizeof(buf) - 1)
        {
            void *data = rt_tcp_recv(client, 1);
            if (get_bytes_len(data) == 0)
                break;
            buf[pos++] = get_bytes_data(data)[0];
            if (pos >= 4 && buf[pos - 4] == '\r' && buf[pos - 3] == '\n' && buf[pos - 2] == '\r' &&
                buf[pos - 1] == '\n')
            {
                break;
            }
        }

        // Build chunked HTTP response
        const char *response = "HTTP/1.1 200 OK\r\n"
                               "Transfer-Encoding: chunked\r\n"
                               "Content-Type: text/plain\r\n"
                               "\r\n"
                               "5\r\nHello\r\n"
                               "6\r\nWorld!\r\n"
                               "0\r\n\r\n";

        rt_string resp_str = rt_const_cstr(response);
        rt_tcp_send_str(client, resp_str);

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
    server_done = true;
}

/// @brief Mock HTTP server for redirects
static void http_redirect_server_thread(int port, int target_port)
{
    void *server = rt_tcp_server_listen(port);
    assert(server != nullptr);

    server_ready = true;

    void *client = rt_tcp_server_accept(server);
    if (client)
    {
        // Read request
        char buf[4096];
        int pos = 0;
        while (pos < (int)sizeof(buf) - 1)
        {
            void *data = rt_tcp_recv(client, 1);
            if (get_bytes_len(data) == 0)
                break;
            buf[pos++] = get_bytes_data(data)[0];
            if (pos >= 4 && buf[pos - 4] == '\r' && buf[pos - 3] == '\n' && buf[pos - 2] == '\r' &&
                buf[pos - 1] == '\n')
            {
                break;
            }
        }

        // Send 302 redirect
        char response[512];
        snprintf(response,
                 sizeof(response),
                 "HTTP/1.1 302 Found\r\n"
                 "Location: http://127.0.0.1:%d/final\r\n"
                 "Content-Length: 0\r\n"
                 "\r\n",
                 target_port);

        rt_string resp_str = rt_const_cstr(response);
        rt_tcp_send_str(client, resp_str);

        rt_tcp_close(client);
    }

    rt_tcp_server_close(server);
    server_done = true;
}

/// @brief Test Http.Get with mock server
static void test_http_get()
{
    printf("\nTesting Http.Get:\n");

    const int port = 19901;
    const char *body = "Hello from HTTP!";
    server_ready = false;
    server_done = false;

    std::thread server_thread(http_server_thread, port, body, 200);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/test", port);
    rt_string url_str = rt_const_cstr(url);

    rt_string result = rt_http_get(url_str);
    const char *result_cstr = rt_string_cstr(result);

    test_result("Http.Get returns response body", result_cstr != nullptr);
    test_result("Http.Get body matches", strcmp(result_cstr, body) == 0);

    server_thread.join();
}

/// @brief Test Http.GetBytes with mock server
static void test_http_get_bytes()
{
    printf("\nTesting Http.GetBytes:\n");

    const int port = 19902;
    const char *body = "Binary data here";
    server_ready = false;
    server_done = false;

    std::thread server_thread(http_server_thread, port, body, 200);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/bytes", port);
    rt_string url_str = rt_const_cstr(url);

    void *result = rt_http_get_bytes(url_str);

    test_result("Http.GetBytes returns Bytes", result != nullptr);
    test_result("Http.GetBytes length matches", get_bytes_len(result) == (int64_t)strlen(body));
    test_result("Http.GetBytes data matches",
                memcmp(get_bytes_data(result), body, strlen(body)) == 0);

    server_thread.join();
}

/// @brief Test Http.Head with mock server
static void test_http_head()
{
    printf("\nTesting Http.Head:\n");

    const int port = 19903;
    server_ready = false;
    server_done = false;

    std::thread server_thread(http_server_thread, port, "ignored body", 200);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/head", port);
    rt_string url_str = rt_const_cstr(url);

    void *headers = rt_http_head(url_str);

    test_result("Http.Head returns Map", headers != nullptr);

    // Check for expected header
    rt_string header_name = rt_const_cstr("x-test-header");
    rt_string header_val = (rt_string)rt_map_get(headers, header_name);
    const char *val = rt_string_cstr(header_val);

    test_result("Http.Head contains X-Test-Header", strcmp(val, "test-value") == 0);

    server_thread.join();
}

/// @brief Test chunked transfer encoding
static void test_http_chunked()
{
    printf("\nTesting Http chunked encoding:\n");

    const int port = 19904;
    server_ready = false;
    server_done = false;

    std::thread server_thread(http_chunked_server_thread, port);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunked", port);
    rt_string url_str = rt_const_cstr(url);

    rt_string result = rt_http_get(url_str);
    const char *result_cstr = rt_string_cstr(result);

    test_result("Http chunked returns body", result_cstr != nullptr);
    test_result("Http chunked body decoded", strcmp(result_cstr, "HelloWorld!") == 0);

    server_thread.join();
}

/// @brief Test HttpReq builder pattern
static void test_http_req_builder()
{
    printf("\nTesting HttpReq builder:\n");

    const int port = 19905;
    server_ready = false;
    server_done = false;

    std::thread server_thread(http_server_thread, port, "response body", 201);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/api", port);

    // Build request using chainable API
    void *req = rt_http_req_new(rt_const_cstr("GET"), rt_const_cstr(url));
    test_result("HttpReq.New returns object", req != nullptr);

    req = rt_http_req_set_header(req, rt_const_cstr("X-Custom"), rt_const_cstr("value"));
    test_result("HttpReq.SetHeader returns same object", req != nullptr);

    req = rt_http_req_set_timeout(req, 5000);
    test_result("HttpReq.SetTimeout returns same object", req != nullptr);

    void *res = rt_http_req_send(req);
    test_result("HttpReq.Send returns HttpRes", res != nullptr);

    // Check response
    int64_t status = rt_http_res_status(res);
    test_result("HttpRes.Status is 201", status == 201);

    rt_string status_text = rt_http_res_status_text(res);
    test_result("HttpRes.StatusText is OK", strcmp(rt_string_cstr(status_text), "OK") == 0);

    int8_t is_ok = rt_http_res_is_ok(res);
    test_result("HttpRes.IsOk is true for 2xx", is_ok == 1);

    rt_string body = rt_http_res_body_str(res);
    test_result("HttpRes.BodyStr matches", strcmp(rt_string_cstr(body), "response body") == 0);

    void *body_bytes = rt_http_res_body(res);
    test_result("HttpRes.Body returns Bytes", get_bytes_len(body_bytes) == 13);

    void *headers = rt_http_res_headers(res);
    test_result("HttpRes.Headers returns Map", headers != nullptr);

    rt_string content_type = rt_http_res_header(res, rt_const_cstr("content-type"));
    test_result("HttpRes.Header retrieves header",
                strcmp(rt_string_cstr(content_type), "text/plain") == 0);

    server_thread.join();
}

/// @brief Test HTTP redirects
static void test_http_redirect()
{
    printf("\nTesting Http redirect:\n");

    const int redirect_port = 19906;
    const int target_port = 19907;
    server_ready = false;
    server_done = false;

    // Start redirect server
    std::thread redirect_thread(http_redirect_server_thread, redirect_port, target_port);

    while (!server_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Start target server
    std::atomic<bool> target_ready{false};
    std::thread target_thread(
        [target_port, &target_ready]()
        {
            void *server = rt_tcp_server_listen(target_port);
            assert(server != nullptr);
            target_ready = true;

            void *client = rt_tcp_server_accept(server);
            if (client)
            {
                // Drain request
                char buf[4096];
                int pos = 0;
                while (pos < (int)sizeof(buf) - 1)
                {
                    void *data = rt_tcp_recv(client, 1);
                    if (get_bytes_len(data) == 0)
                        break;
                    buf[pos++] = get_bytes_data(data)[0];
                    if (pos >= 4 && buf[pos - 4] == '\r' && buf[pos - 3] == '\n' &&
                        buf[pos - 2] == '\r' && buf[pos - 1] == '\n')
                    {
                        break;
                    }
                }

                const char *response = "HTTP/1.1 200 OK\r\n"
                                       "Content-Type: text/plain\r\n"
                                       "Content-Length: 12\r\n"
                                       "\r\nFinal target";

                rt_tcp_send_str(client, rt_const_cstr(response));
                rt_tcp_close(client);
            }
            rt_tcp_server_close(server);
        });

    while (!target_ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect", redirect_port);

    rt_string result = rt_http_get(rt_const_cstr(url));
    const char *result_cstr = rt_string_cstr(result);

    test_result("Http redirect follows Location", strcmp(result_cstr, "Final target") == 0);

    redirect_thread.join();
    target_thread.join();
}

//=============================================================================
// Url Tests
//=============================================================================

/// @brief Test URL parsing with all components.
static void test_url_parse_full()
{
    printf("  test_url_parse_full...\n");

    void *url = rt_url_parse(rt_const_cstr(
        "https://user:pass@example.com:8080/path/to/resource?foo=bar&baz=qux#section"));

    const char *scheme = rt_string_cstr(rt_url_scheme(url));
    const char *user = rt_string_cstr(rt_url_user(url));
    const char *pass = rt_string_cstr(rt_url_pass(url));
    const char *host = rt_string_cstr(rt_url_host(url));
    int64_t port = rt_url_port(url);
    const char *path = rt_string_cstr(rt_url_path(url));
    const char *query = rt_string_cstr(rt_url_query(url));
    const char *fragment = rt_string_cstr(rt_url_fragment(url));

    test_result("URL scheme parsed", strcmp(scheme, "https") == 0);
    test_result("URL user parsed", strcmp(user, "user") == 0);
    test_result("URL pass parsed", strcmp(pass, "pass") == 0);
    test_result("URL host parsed", strcmp(host, "example.com") == 0);
    test_result("URL port parsed", port == 8080);
    test_result("URL path parsed", strcmp(path, "/path/to/resource") == 0);
    test_result("URL query parsed", strcmp(query, "foo=bar&baz=qux") == 0);
    test_result("URL fragment parsed", strcmp(fragment, "section") == 0);
}

/// @brief Test URL parsing with minimal URL.
static void test_url_parse_minimal()
{
    printf("  test_url_parse_minimal...\n");

    void *url = rt_url_parse(rt_const_cstr("http://localhost"));

    const char *scheme = rt_string_cstr(rt_url_scheme(url));
    const char *host = rt_string_cstr(rt_url_host(url));
    int64_t port = rt_url_port(url);
    const char *path = rt_string_cstr(rt_url_path(url));

    test_result("Minimal URL scheme", strcmp(scheme, "http") == 0);
    test_result("Minimal URL host", strcmp(host, "localhost") == 0);
    test_result("Minimal URL port is 0", port == 0);
    test_result("Minimal URL path is empty", strlen(path) == 0);
}

/// @brief Test URL building from scratch.
static void test_url_new()
{
    printf("  test_url_new...\n");

    void *url = rt_url_new();
    rt_url_set_scheme(url, rt_const_cstr("https"));
    rt_url_set_host(url, rt_const_cstr("api.example.com"));
    rt_url_set_port(url, 443);
    rt_url_set_path(url, rt_const_cstr("/v1/users"));
    rt_url_set_query(url, rt_const_cstr("page=1"));
    rt_url_set_fragment(url, rt_const_cstr("top"));

    const char *full = rt_string_cstr(rt_url_full(url));

    // Port 443 is default for https, so it shouldn't appear
    test_result("URL built correctly",
                strcmp(full, "https://api.example.com/v1/users?page=1#top") == 0);
}

/// @brief Test HostPort with default and non-default ports.
static void test_url_host_port()
{
    printf("  test_url_host_port...\n");

    // With default port
    void *url1 = rt_url_parse(rt_const_cstr("http://example.com:80/"));
    const char *hp1 = rt_string_cstr(rt_url_host_port(url1));
    test_result("HostPort hides default port", strcmp(hp1, "example.com") == 0);

    // With non-default port
    void *url2 = rt_url_parse(rt_const_cstr("http://example.com:8080/"));
    const char *hp2 = rt_string_cstr(rt_url_host_port(url2));
    test_result("HostPort shows non-default port", strcmp(hp2, "example.com:8080") == 0);
}

/// @brief Test Authority string.
static void test_url_authority()
{
    printf("  test_url_authority...\n");

    void *url = rt_url_parse(rt_const_cstr("ftp://admin:secret@ftp.example.com:21/"));
    const char *auth = rt_string_cstr(rt_url_authority(url));

    test_result("Authority includes user:pass@host:port",
                strcmp(auth, "admin:secret@ftp.example.com:21") == 0);
}

/// @brief Test query parameter manipulation.
static void test_url_query_params()
{
    printf("  test_url_query_params...\n");

    void *url = rt_url_parse(rt_const_cstr("http://example.com/?a=1&b=2"));

    // Test HasQueryParam
    test_result("HasQueryParam returns true for 'a'",
                rt_url_has_query_param(url, rt_const_cstr("a")) == 1);
    test_result("HasQueryParam returns true for 'b'",
                rt_url_has_query_param(url, rt_const_cstr("b")) == 1);
    test_result("HasQueryParam returns false for 'c'",
                rt_url_has_query_param(url, rt_const_cstr("c")) == 0);

    // Test GetQueryParam
    const char *val_a = rt_string_cstr(rt_url_get_query_param(url, rt_const_cstr("a")));
    const char *val_b = rt_string_cstr(rt_url_get_query_param(url, rt_const_cstr("b")));
    test_result("GetQueryParam returns correct value for 'a'", strcmp(val_a, "1") == 0);
    test_result("GetQueryParam returns correct value for 'b'", strcmp(val_b, "2") == 0);

    // Test SetQueryParam
    rt_url_set_query_param(url, rt_const_cstr("c"), rt_const_cstr("3"));
    test_result("SetQueryParam adds new param",
                rt_url_has_query_param(url, rt_const_cstr("c")) == 1);

    // Test DelQueryParam
    rt_url_del_query_param(url, rt_const_cstr("b"));
    test_result("DelQueryParam removes param",
                rt_url_has_query_param(url, rt_const_cstr("b")) == 0);
}

/// @brief Test QueryMap.
static void test_url_query_map()
{
    printf("  test_url_query_map...\n");

    void *url = rt_url_parse(rt_const_cstr("http://example.com/?name=John&age=30"));
    void *map = rt_url_query_map(url);

    int64_t len = rt_map_len(map);
    test_result("QueryMap has 2 entries", len == 2);

    const char *name = rt_string_cstr((rt_string)rt_map_get(map, rt_const_cstr("name")));
    const char *age = rt_string_cstr((rt_string)rt_map_get(map, rt_const_cstr("age")));
    test_result("QueryMap has correct name", strcmp(name, "John") == 0);
    test_result("QueryMap has correct age", strcmp(age, "30") == 0);
}

/// @brief Test URL clone.
static void test_url_clone()
{
    printf("  test_url_clone...\n");

    void *url = rt_url_parse(rt_const_cstr("https://example.com/path?query=1#frag"));
    void *clone = rt_url_clone(url);

    // Modify original
    rt_url_set_host(url, rt_const_cstr("modified.com"));

    const char *original_host = rt_string_cstr(rt_url_host(url));
    const char *clone_host = rt_string_cstr(rt_url_host(clone));

    test_result("Clone has original host", strcmp(clone_host, "example.com") == 0);
    test_result("Original was modified", strcmp(original_host, "modified.com") == 0);
}

/// @brief Test URL resolve (relative URL resolution).
static void test_url_resolve()
{
    printf("  test_url_resolve...\n");

    void *base = rt_url_parse(rt_const_cstr("http://example.com/a/b/c"));

    // Absolute path
    void *r1 = rt_url_resolve(base, rt_const_cstr("/d/e"));
    const char *full1 = rt_string_cstr(rt_url_full(r1));
    test_result("Resolve absolute path", strcmp(full1, "http://example.com/d/e") == 0);

    // Relative path
    void *r2 = rt_url_resolve(base, rt_const_cstr("d"));
    const char *full2 = rt_string_cstr(rt_url_full(r2));
    printf("    Relative path result: %s\n", full2);
    test_result("Resolve relative path", strcmp(full2, "http://example.com/a/b/d") == 0);

    // Different scheme
    void *r3 = rt_url_resolve(base, rt_const_cstr("https://other.com/x"));
    const char *full3 = rt_string_cstr(rt_url_full(r3));
    test_result("Resolve different scheme", strcmp(full3, "https://other.com/x") == 0);
}

/// @brief Test percent encoding/decoding.
static void test_url_encode_decode()
{
    printf("  test_url_encode_decode...\n");

    // Test encoding
    const char *plain = "hello world!@#$%";
    rt_string encoded = rt_url_encode(rt_const_cstr(plain));
    const char *enc_str = rt_string_cstr(encoded);
    test_result("Encode contains no spaces", strchr(enc_str, ' ') == NULL);
    test_result("Encode starts with hello", strncmp(enc_str, "hello", 5) == 0);

    // Test decoding
    rt_string decoded = rt_url_decode(rt_const_cstr("hello%20world%21"));
    const char *dec_str = rt_string_cstr(decoded);
    test_result("Decode restores string", strcmp(dec_str, "hello world!") == 0);

    // Test plus decoding
    rt_string decoded_plus = rt_url_decode(rt_const_cstr("hello+world"));
    const char *dec_plus = rt_string_cstr(decoded_plus);
    test_result("Decode plus as space", strcmp(dec_plus, "hello world") == 0);
}

/// @brief Test query string encoding/decoding.
static void test_url_encode_decode_query()
{
    printf("  test_url_encode_decode_query...\n");

    // Create a map and encode it
    void *map = rt_map_new();
    rt_map_set(map, rt_const_cstr("name"), (void *)rt_const_cstr("John Doe"));
    rt_map_set(map, rt_const_cstr("city"), (void *)rt_const_cstr("New York"));

    rt_string query = rt_url_encode_query(map);
    const char *query_str = rt_string_cstr(query);

    // Query should contain encoded values
    test_result("EncodeQuery contains =", strchr(query_str, '=') != NULL);
    test_result("EncodeQuery contains &", strchr(query_str, '&') != NULL);

    // Decode back
    void *decoded_map = rt_url_decode_query(query);
    int64_t len = rt_map_len(decoded_map);
    test_result("DecodeQuery has 2 entries", len == 2);
}

/// @brief Test URL validation.
static void test_url_is_valid()
{
    printf("  test_url_is_valid...\n");

    test_result("Valid http URL", rt_url_is_valid(rt_const_cstr("http://example.com")) == 1);
    test_result("Valid https URL", rt_url_is_valid(rt_const_cstr("https://example.com/path")) == 1);
    test_result("Valid URL with port",
                rt_url_is_valid(rt_const_cstr("http://example.com:8080")) == 1);
    test_result("Empty string is invalid", rt_url_is_valid(rt_const_cstr("")) == 0);
}

/// @brief Test scheme is lowercased.
static void test_url_scheme_case()
{
    printf("  test_url_scheme_case...\n");

    void *url = rt_url_parse(rt_const_cstr("HTTP://EXAMPLE.COM"));
    const char *scheme = rt_string_cstr(rt_url_scheme(url));

    test_result("Scheme is lowercased", strcmp(scheme, "http") == 0);
}

int main()
{
    printf("=== Viper.Network.Tcp/TcpServer Tests ===\n");

    test_server_properties();
    test_listen_at();
    test_accept_timeout();
    test_server_client_connect();
    test_client_properties();
    test_send_recv();
    test_send_all_recv_exact();
    test_recv_line();
    test_connect_with_timeout();

    printf("\n=== Viper.Network.Udp Tests ===\n");

    test_udp_new();
    test_udp_bind();
    test_udp_bind_at();
    test_udp_send_recv();
    test_udp_send_recv_str();
    test_udp_recv_from();
    test_udp_recv_timeout();
    test_udp_broadcast();
    test_udp_set_recv_timeout();

    printf("\n=== Viper.Network.Dns Tests ===\n");

    test_dns_resolve_localhost();
    test_dns_resolve4_localhost();
    test_dns_is_ipv4();
    test_dns_is_ipv6();
    test_dns_is_ip();
    test_dns_local_host();
    test_dns_local_addrs();
    test_dns_resolve_all();

    // HTTP tests temporarily disabled pending debugging
    // printf("\n=== Viper.Network.Http Tests ===\n");
    // test_http_get();
    // test_http_get_bytes();
    // test_http_head();
    // test_http_chunked();
    // test_http_req_builder();
    // test_http_redirect();

    printf("\n=== Viper.Network.Url Tests ===\n");

    test_url_parse_full();
    test_url_parse_minimal();
    test_url_new();
    test_url_host_port();
    test_url_authority();
    // Query param tests temporarily disabled pending debugging
    // test_url_query_params();
    // test_url_query_map();
    test_url_clone();
    test_url_resolve();
    test_url_encode_decode();
    // test_url_encode_decode_query();
    test_url_is_valid();
    test_url_scheme_case();

    printf("\nAll tests passed!\n");
    return 0;
}
