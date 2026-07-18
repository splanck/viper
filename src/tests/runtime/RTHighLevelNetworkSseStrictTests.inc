//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file RTHighLevelNetworkSseStrictTests.inc
/// @brief Strict SSE framing and EventSource-line integration coverage.
/// @details This include fragment keeps the high-level network test translation
///          unit below the source-health mega-file threshold while sharing its
///          loopback fixture state and trap assertions. It is included exactly
///          once by RTHighLevelNetworkTests.cpp.
//
//===----------------------------------------------------------------------===//

/// @brief Exercise strict HTTP/chunk framing and complete EventSource line semantics.
/// @details Covers bare-LF and folded response heads, duplicate singleton
///          fields, conflicting framing, malformed chunk extensions/trailers,
///          stable class identity, UTF-8 BOM removal, colonless data fields,
///          empty-data joining, lone CR/LF delimiters, and exact Content-Length.
static void test_sse_strict_framing_and_event_lines() {
    printf("\nTesting SseClient strict framing and event-line semantics:\n");

    expect_sse_connect_rejection("HTTP/1.1 200 OK\nContent-Type: text/event-stream\n\n",
                                 "Bare-LF HTTP response is rejected",
                                 "HTTP response");
    expect_sse_connect_rejection("HTTP/1.1 200 OK\r\n"
                                 " Content-Type: text/event-stream\r\n\r\n",
                                 "Obsolete folded response field is rejected",
                                 "header");
    expect_sse_connect_rejection("HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/event-stream\r\n"
                                 "Content-Type: text/event-stream\r\n\r\n",
                                 "Duplicate Content-Type is rejected",
                                 "duplicate Content-Type");
    expect_sse_connect_rejection("HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/event-stream\r\n"
                                 "Transfer-Encoding: chunked\r\n"
                                 "Content-Length: 0\r\n\r\n",
                                 "Transfer-Encoding plus Content-Length is rejected",
                                 "conflicting");

    int port = (int)rt_netutils_get_free_port();
    if (port <= 0) {
        printf("  SKIP: strict event-line probes (local bind unavailable)\n");
        return;
    }
    const std::string body = std::string("\xEF\xBB\xBF") + "data\rdata:\n\r";
    sse_scripted_response = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/event-stream; charset=utf-8\r\n"
                            "Content-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body;
    sse_scripted_hold_ms = 100;
    api_server_ready = false;
    api_server_failed = false;
    std::thread line_server(sse_scripted_server_thread, port);
    wait_for_server();
    if (api_server_failed) {
        line_server.join();
        printf("  SKIP: strict event-line probes (local bind unavailable)\n");
        return;
    }
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/lines", port);
    void *sse = rt_sse_connect(rt_const_cstr(url));
    test_result("SSE client carries its stable class ID",
                sse && rt_obj_class_id(sse) == RT_SSE_CLASS_ID);
    rt_string data = rt_sse_recv_for(sse, 2000);
    test_result("BOM, colonless data, empty data, CR and LF parse exactly",
                data && rt_str_len(data) == 1 && rt_string_cstr(data)[0] == '\n');
    rt_string_unref(data);
    rt_sse_close(sse);
    line_server.join();

    port = (int)rt_netutils_get_free_port();
    api_server_ready = false;
    api_server_failed = false;
    sse_scripted_response = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/event-stream\r\n"
                            "Transfer-Encoding: chunked\r\n\r\n"
                            "1;=bad\r\nx\r\n0\r\n\r\n";
    std::thread extension_server(sse_scripted_server_thread, port);
    wait_for_server();
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunk-extension", port);
    sse = rt_sse_connect(rt_const_cstr(url));
    EXPECT_TRAP(rt_sse_recv_for(sse, 2000));
    test_result("Malformed chunk extension traps and closes",
                g_last_trap && strstr(g_last_trap, "framing") != nullptr &&
                    rt_sse_is_open(sse) == 0);
    rt_sse_close(sse);
    extension_server.join();

    port = (int)rt_netutils_get_free_port();
    api_server_ready = false;
    api_server_failed = false;
    sse_scripted_response = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/event-stream\r\n"
                            "Transfer-Encoding: chunked\r\n\r\n"
                            "1\r\n:\r\n"
                            "0\r\nContent-Length: 0\r\n\r\n";
    std::thread trailer_server(sse_scripted_server_thread, port);
    wait_for_server();
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunk-trailer", port);
    sse = rt_sse_connect(rt_const_cstr(url));
    EXPECT_TRAP(rt_sse_recv_for(sse, 2000));
    test_result("Framing fields in chunk trailers are rejected",
                g_last_trap && strstr(g_last_trap, "framing") != nullptr);
    rt_sse_close(sse);
    trailer_server.join();
}
