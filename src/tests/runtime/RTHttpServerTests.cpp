//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTHttpServerTests.cpp
// Purpose: Validate HTTP server identity, managed request/response ownership,
//          transactional cleanup, and serialized lifecycle publication.
// Key invariants:
//   - Constructor and request failures restore exact managed-object baselines.
//   - Handler-visible request/response handles carry stable class identities.
//   - Concurrent Start/Stop publishes one listener and never double-joins it.
//   - Replaced binding cleanup runs outside the lifecycle mutex.
// Ownership/Lifetime:
//   - Every managed producer reference created by a test is released locally.
//   - Retained handler snapshots are balanced after post-dispatch validation.
// Links: src/runtime/network/rt_http_server.c,
//        src/runtime/network/rt_http_server.h
//
//===----------------------------------------------------------------------===//

#include "rt_gc.h"
#include "rt_http_server.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <atomic>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if RT_PLATFORM_WINDOWS
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

static int http_server_alloc_countdown = 0;
static int http_server_alloc_observed = 0;
static void *retained_request = nullptr;
static void *retained_response = nullptr;
static rt_string shared_response_body = nullptr;
static int json_fail_at = 0;
static bool json_handler_trapped = false;

/// @brief Print and enforce one focused runtime assertion.
/// @param condition Condition that must hold.
/// @param name Human-readable assertion name.
static void test_result(bool condition, const char *name) {
    std::printf("  %s: %s\n", name, condition ? "PASS" : "FAIL");
    assert(condition);
}

/// @brief Count managed allocations and fail one selected allocation boundary.
/// @param bytes Requested managed payload bytes.
/// @param next Default allocator used when this boundary is accepted.
/// @return Allocated payload, or NULL at the selected countdown.
static void *http_server_countdown_alloc(int64_t bytes, void *(*next)(int64_t)) {
    http_server_alloc_observed++;
    if (http_server_alloc_countdown > 0 && --http_server_alloc_countdown == 0)
        return nullptr;
    return next(bytes);
}

/// @brief Drop one caller-owned runtime object or String reference.
/// @param object Managed value, or NULL.
static void release_managed(void *object) {
    if (object && rt_obj_release_check0(object))
        rt_obj_free(object);
}

/// @brief Create and register one route using balanced temporary Strings.
/// @param server Valid stopped HttpServer.
/// @param pattern Route pattern.
/// @param tag Handler tag.
static void register_get(void *server, const char *pattern, const char *tag) {
    rt_string pattern_string = rt_const_cstr(pattern);
    rt_string tag_string = rt_const_cstr(tag);
    rt_http_server_get(server, pattern_string, tag_string);
    release_managed(tag_string);
    release_managed(pattern_string);
}

/// @brief Create and register one POST route using balanced temporary Strings.
/// @param server Valid stopped HttpServer.
/// @param pattern Route pattern.
/// @param tag Handler tag.
static void register_post(void *server, const char *pattern, const char *tag) {
    rt_string pattern_string = rt_const_cstr(pattern);
    rt_string tag_string = rt_const_cstr(tag);
    rt_http_server_post(server, pattern_string, tag_string);
    release_managed(tag_string);
    release_managed(pattern_string);
}

/// @brief Bind one native route handler using a balanced tag String.
/// @param server Valid stopped HttpServer.
/// @param tag Handler tag.
/// @param handler Native handler function.
static void bind_native(void *server, const char *tag, rt_http_server_handler_fn handler) {
    rt_string tag_string = rt_const_cstr(tag);
    rt_http_server_bind_handler(server, tag_string, reinterpret_cast<void *>(handler));
    release_managed(tag_string);
}

/// @brief Build one managed raw HTTP request String.
/// @param bytes Complete request bytes.
/// @return Caller-owned managed String.
static rt_string request_string(const char *bytes) {
    return rt_string_from_bytes(bytes, std::strlen(bytes));
}

/// @brief Simple response handler used by allocation and lifecycle probes.
/// @param req Managed ServerReq handle (unused).
/// @param res Managed ServerRes handle populated with a stable body.
static void simple_handler(void *req, void *res) {
    (void)req;
    rt_server_res_status(res, 200);
    rt_server_res_send(res, shared_response_body);
}

/// @brief Validate all request accessors and retain both handler handles.
/// @details This proves that HTTP callbacks receive managed objects, that route
///          parameters/query/header snapshots are complete, and that a caller
///          retain keeps each object alive after the server drops its reference.
/// @param req Managed ServerReq handle.
/// @param res Managed ServerRes handle.
static void snapshot_handler(void *req, void *res) {
    assert(rt_obj_class_id(req) == RT_SERVER_REQ_CLASS_ID);
    assert(rt_obj_class_id(res) == RT_SERVER_RES_CLASS_ID);
    rt_obj_retain_maybe(req);
    rt_obj_retain_maybe(res);
    retained_request = req;
    retained_response = res;

    rt_string method = rt_server_req_method(req);
    rt_string path = rt_server_req_path(req);
    rt_string body = rt_server_req_body(req);
    rt_string header_name = rt_const_cstr("X-Test");
    rt_string header = rt_server_req_header(req, header_name);
    rt_string param_name = rt_const_cstr("id");
    rt_string param = rt_server_req_param(req, param_name);
    rt_string query_name = rt_const_cstr("q");
    rt_string query = rt_server_req_query(req, query_name);
    assert(std::strcmp(rt_string_cstr(method), "POST") == 0);
    assert(std::strcmp(rt_string_cstr(path), "/items/42") == 0);
    assert(rt_str_len(body) == 3 && std::memcmp(rt_string_cstr(body), "abc", 3) == 0);
    assert(std::strcmp(rt_string_cstr(header), "Value") == 0);
    assert(std::strcmp(rt_string_cstr(param), "42") == 0);
    assert(std::strcmp(rt_string_cstr(query), "hello world") == 0);
    release_managed(query);
    release_managed(query_name);
    release_managed(param);
    release_managed(param_name);
    release_managed(header);
    release_managed(header_name);
    release_managed(body);
    release_managed(path);
    release_managed(method);

    rt_string response_header = rt_const_cstr("X-Reply");
    rt_string response_value = rt_const_cstr("Complete");
    rt_server_res_status(res, 201);
    rt_server_res_header(res, response_header, response_value);
    rt_server_res_send(res, shared_response_body);
    release_managed(response_value);
    release_managed(response_header);
}

/// @brief Seed a response, then exercise the atomic JSON replacement path.
/// @details The allocation hook is enabled only around `ServerRes.Json`, and a
///          handler-local recovery frame converts the injected trap into a
///          normal return. The enclosing server can then serialize the prior
///          response and prove that no partial content-type/body state escaped.
/// @param req Managed ServerReq handle (unused).
/// @param res Managed ServerRes handle under test.
static void transactional_json_handler(void *req, void *res) {
    (void)req;
    rt_string prior_name = rt_const_cstr("X-State");
    rt_string prior_value = rt_const_cstr("prior");
    rt_string prior_body = rt_const_cstr("old-body");
    rt_server_res_header(res, prior_name, prior_value);
    rt_server_res_send(res, prior_body);
    release_managed(prior_body);
    release_managed(prior_value);
    release_managed(prior_name);

    json_handler_trapped = false;
    http_server_alloc_countdown = json_fail_at;
    http_server_alloc_observed = 0;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        rt_set_alloc_hook(http_server_countdown_alloc);
        rt_server_res_json(res, shared_response_body);
        rt_set_alloc_hook(nullptr);
        rt_trap_clear_recovery();
    } else {
        rt_set_alloc_hook(nullptr);
        json_handler_trapped = true;
        rt_trap_clear_recovery();
    }
}

/// @brief Verify stable server identity and every constructor allocation edge.
static void test_identity_and_constructor_cleanup() {
    std::printf("\nTesting HttpServer identity and constructor cleanup:\n");
    const int64_t baseline = rt_gc_tracked_count();

    http_server_alloc_countdown = 0;
    http_server_alloc_observed = 0;
    rt_set_alloc_hook(http_server_countdown_alloc);
    void *server = rt_http_server_new(0);
    rt_set_alloc_hook(nullptr);
    const int allocation_count = http_server_alloc_observed;
    test_result(server && rt_obj_class_id(server) == RT_HTTP_SERVER_CLASS_ID,
                "constructor publishes the stable HttpServer class ID");
    release_managed(server);
    test_result(rt_gc_tracked_count() == baseline,
                "successful stopped server releases to its exact baseline");

    bool all_clean = allocation_count > 0;
    int trap_count = 0;
    for (int fail_at = 1; fail_at <= allocation_count; fail_at++) {
        void *volatile failed_server = nullptr;
        volatile bool trapped = false;
        jmp_buf recovery;
        http_server_alloc_countdown = fail_at;
        http_server_alloc_observed = 0;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            rt_set_alloc_hook(http_server_countdown_alloc);
            failed_server = rt_http_server_new(0);
            rt_set_alloc_hook(nullptr);
            rt_trap_clear_recovery();
        } else {
            rt_set_alloc_hook(nullptr);
            trapped = true;
            rt_trap_clear_recovery();
        }
        trap_count += trapped ? 1 : 0;
        all_clean = all_clean && trapped && failed_server == nullptr;
        release_managed((void *)failed_server);
        all_clean = all_clean && rt_gc_tracked_count() == baseline;
    }
    http_server_alloc_countdown = 0;
    test_result(all_clean && trap_count == allocation_count,
                "every managed constructor failure releases partial mutex/router state");
}

/// @brief Ensure forged server/request/response handles trap before payload use.
static void test_forged_handle_rejection() {
    std::printf("\nTesting HttpServer forged-handle rejection:\n");
    const int64_t baseline = rt_gc_tracked_count();
    void *wrong = rt_seq_new();

    volatile bool port_trapped = false;
    jmp_buf port_recovery;
    rt_trap_set_recovery(&port_recovery);
    if (setjmp(port_recovery) == 0) {
        (void)rt_http_server_port(wrong);
        rt_trap_clear_recovery();
    } else {
        port_trapped = true;
        rt_trap_clear_recovery();
    }

    rt_string volatile method = nullptr;
    volatile bool request_trapped = false;
    jmp_buf request_recovery;
    rt_trap_set_recovery(&request_recovery);
    if (setjmp(request_recovery) == 0) {
        method = rt_server_req_method(wrong);
        rt_trap_clear_recovery();
    } else {
        request_trapped = true;
        rt_trap_clear_recovery();
    }

    void *volatile response_result = nullptr;
    volatile bool response_trapped = false;
    jmp_buf response_recovery;
    rt_trap_set_recovery(&response_recovery);
    if (setjmp(response_recovery) == 0) {
        response_result = rt_server_res_status(wrong, 200);
        rt_trap_clear_recovery();
    } else {
        response_trapped = true;
        rt_trap_clear_recovery();
    }

    test_result(port_trapped && request_trapped && response_trapped && method == nullptr &&
                    response_result == nullptr && rt_seq_len(wrong) == 0,
                "unrelated managed objects are never interpreted as server payloads");
    release_managed((void *)method);
    release_managed(wrong);
    test_result(rt_gc_tracked_count() == baseline, "forged-handle probes leave no objects behind");
}

/// @brief Verify callback handles are managed, complete, and retainable.
static void test_managed_request_response_snapshots() {
    std::printf("\nTesting managed ServerReq/ServerRes snapshots:\n");
    const int64_t baseline = rt_gc_tracked_count();
    shared_response_body = rt_const_cstr("created");
    void *server = rt_http_server_new(0);
    register_post(server, "/items/:id", "snapshot");
    bind_native(server, "snapshot", snapshot_handler);
    rt_string raw = request_string("POST /items/42?q=hello%20world HTTP/1.1\r\n"
                                   "Host: local\r\n"
                                   "X-Test: Value\r\n"
                                   "Connection: close\r\n"
                                   "Content-Length: 3\r\n\r\n"
                                   "abc");
    void *wire_object = rt_http_server_process_request(server, raw);
    std::string wire(rt_string_cstr((rt_string)wire_object),
                     static_cast<size_t>(rt_str_len((rt_string)wire_object)));
    test_result(wire.find("HTTP/1.1 201 Created\r\n") == 0 &&
                    wire.find("X-Reply: Complete\r\n") != std::string::npos &&
                    wire.rfind("created") == wire.size() - 7,
                "handler receives complete snapshots and publishes one complete response");

    rt_string retained_method = rt_server_req_method(retained_request);
    void *retained_status = rt_server_res_status(retained_response, 202);
    test_result(retained_method && std::strcmp(rt_string_cstr(retained_method), "POST") == 0 &&
                    retained_status == retained_response,
                "independently retained request/response handles survive dispatch");
    release_managed(retained_method);
    release_managed(retained_response);
    release_managed(retained_request);
    retained_response = nullptr;
    retained_request = nullptr;
    release_managed(wire_object);
    release_managed(raw);
    release_managed(server);
    release_managed(shared_response_body);
    shared_response_body = nullptr;
    test_result(rt_gc_tracked_count() == baseline,
                "managed request/response snapshots release their complete graphs");
}

/// @brief Sweep every managed allocation in synchronous request processing.
/// @details The route and source request stay alive across the sweep. Each
///          injected failure must propagate through the server transaction,
///          release partial request/response/router snapshots, and leave the
///          same server reusable for a final successful request.
static void test_synchronous_request_allocation_cleanup() {
    std::printf("\nTesting synchronous request allocation cleanup:\n");
    const int64_t entry_baseline = rt_gc_tracked_count();
    shared_response_body = rt_const_cstr("ok");
    void *server = rt_http_server_new(0);
    register_get(server, "/probe", "simple");
    bind_native(server, "simple", simple_handler);
    rt_string raw =
        request_string("GET /probe HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n");
    const int64_t baseline = rt_gc_tracked_count();

    http_server_alloc_countdown = 0;
    http_server_alloc_observed = 0;
    rt_set_alloc_hook(http_server_countdown_alloc);
    void *calibration = rt_http_server_process_request(server, raw);
    rt_set_alloc_hook(nullptr);
    const int allocation_count = http_server_alloc_observed;
    test_result(calibration && allocation_count > 0, "request calibration reaches every layer");
    release_managed(calibration);
    test_result(rt_gc_tracked_count() == baseline,
                "successful synchronous request restores its managed baseline");

    bool all_clean = true;
    int trap_count = 0;
    for (int fail_at = 1; fail_at <= allocation_count; fail_at++) {
        void *volatile result = nullptr;
        volatile bool trapped = false;
        jmp_buf recovery;
        http_server_alloc_countdown = fail_at;
        http_server_alloc_observed = 0;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            rt_set_alloc_hook(http_server_countdown_alloc);
            result = rt_http_server_process_request(server, raw);
            rt_set_alloc_hook(nullptr);
            rt_trap_clear_recovery();
        } else {
            rt_set_alloc_hook(nullptr);
            trapped = true;
            rt_trap_clear_recovery();
        }
        trap_count += trapped ? 1 : 0;
        release_managed((void *)result);
        all_clean = all_clean && trapped && result == nullptr && rt_gc_tracked_count() == baseline;
    }
    http_server_alloc_countdown = 0;
    test_result(all_clean && trap_count == allocation_count,
                "every managed request failure releases the full transaction");

    void *final_wire = rt_http_server_process_request(server, raw);
    test_result(final_wire &&
                    std::strstr(rt_string_cstr((rt_string)final_wire), "200 OK") != nullptr,
                "server remains reusable after every injected request failure");
    release_managed(final_wire);
    release_managed(raw);
    release_managed(server);
    release_managed(shared_response_body);
    shared_response_body = nullptr;
    test_result(rt_gc_tracked_count() == entry_baseline,
                "request allocation sweep releases server, router, and inputs");
}

/// @brief Verify `ServerRes.Json` publishes its cloned headers and body atomically.
/// @details Every managed allocation boundary is failed independently. After a
///          recovered failure, the wire response must contain the complete
///          pre-call header/body state and no JSON content type; a successful
///          call must contain the complete new JSON state. Managed-object count
///          returns to the same baseline after every request.
static void test_transactional_json_response() {
    std::printf("\nTesting transactional JSON response publication:\n");
    const int64_t entry_baseline = rt_gc_tracked_count();
    shared_response_body = rt_const_cstr("{\"ok\":true}");
    void *server = rt_http_server_new(0);
    register_get(server, "/json", "json");
    bind_native(server, "json", transactional_json_handler);
    rt_string raw =
        request_string("GET /json HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n");
    const int64_t baseline = rt_gc_tracked_count();

    json_fail_at = 0;
    void *calibration = rt_http_server_process_request(server, raw);
    const int allocation_count = http_server_alloc_observed;
    std::string calibration_wire(rt_string_cstr((rt_string)calibration),
                                 static_cast<size_t>(rt_str_len((rt_string)calibration)));
    test_result(!json_handler_trapped && allocation_count > 0 &&
                    calibration_wire.find("Content-Type: application/json\r\n") !=
                        std::string::npos &&
                    calibration_wire.rfind("{\"ok\":true}") ==
                        calibration_wire.size() - std::strlen("{\"ok\":true}"),
                "successful JSON replacement publishes matching headers and body");
    release_managed(calibration);
    test_result(rt_gc_tracked_count() == baseline,
                "successful JSON replacement restores the request baseline");

    bool all_atomic = true;
    for (int fail_at = 1; fail_at <= allocation_count; fail_at++) {
        json_fail_at = fail_at;
        void *wire_object = rt_http_server_process_request(server, raw);
        std::string wire(rt_string_cstr((rt_string)wire_object),
                         static_cast<size_t>(rt_str_len((rt_string)wire_object)));
        all_atomic = all_atomic && json_handler_trapped &&
                     wire.find("X-State: prior\r\n") != std::string::npos &&
                     wire.find("Content-Type:") == std::string::npos &&
                     wire.rfind("old-body") == wire.size() - std::strlen("old-body");
        release_managed(wire_object);
        all_atomic = all_atomic && rt_gc_tracked_count() == baseline;
    }
    test_result(all_atomic, "every JSON allocation failure preserves the complete prior response");

    json_fail_at = 0;
    http_server_alloc_countdown = 0;
    release_managed(raw);
    release_managed(server);
    release_managed(shared_response_body);
    shared_response_body = nullptr;
    test_result(rt_gc_tracked_count() == entry_baseline,
                "JSON allocation sweep releases server, maps, strings, and response snapshots");
}

struct reentrant_cleanup_context {
    void *server;
    std::atomic<int> calls;
};

/// @brief Probe dispatcher installed with a cleanup-bearing context.
static void cleanup_probe_dispatch(void *ctx, void *req, void *res) {
    (void)ctx;
    (void)req;
    (void)res;
}

/// @brief Re-enter binding APIs from a replaced context's cleanup callback.
/// @details Successful completion proves replacement cleanup runs only after
///          the lifecycle mutex is released.
/// @param opaque Pointer to @ref reentrant_cleanup_context.
static void reentrant_cleanup(void *opaque) {
    auto *context = static_cast<reentrant_cleanup_context *>(opaque);
    context->calls.fetch_add(1);
    assert(rt_http_server_port(context->server) == 0);
    bind_native(context->server, "aux", simple_handler);
}

/// @brief Verify binding replacement publication and cleanup lock discipline.
static void test_reentrant_binding_cleanup() {
    std::printf("\nTesting binding replacement cleanup:\n");
    const int64_t baseline = rt_gc_tracked_count();
    shared_response_body = rt_const_cstr("aux");
    void *server = rt_http_server_new(0);
    register_get(server, "/aux", "aux");
    rt_string replace_tag = rt_const_cstr("replace");
    reentrant_cleanup_context context{server, 0};
    rt_http_server_bind_handler_dispatch(server,
                                         replace_tag,
                                         reinterpret_cast<void *>(cleanup_probe_dispatch),
                                         &context,
                                         reinterpret_cast<void *>(reentrant_cleanup));
    rt_http_server_bind_handler(server, replace_tag, reinterpret_cast<void *>(simple_handler));
    test_result(context.calls.load() == 1,
                "replaced context cleanup re-enters server APIs without deadlock");

    rt_string raw = request_string("GET /aux HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n");
    void *wire = rt_http_server_process_request(server, raw);
    test_result(wire && std::strstr(rt_string_cstr((rt_string)wire), "\r\n\r\naux") != nullptr,
                "reentrant cleanup publishes an independently usable binding");
    release_managed(wire);
    release_managed(raw);
    release_managed(replace_tag);
    release_managed(server);
    release_managed(shared_response_body);
    shared_response_body = nullptr;
    test_result(rt_gc_tracked_count() == baseline,
                "binding replacement releases old and reentrant state exactly once");
}

/// @brief Stress concurrent idempotent Start and Stop calls on one server.
static void test_concurrent_lifecycle_publication() {
    std::printf("\nTesting concurrent HttpServer lifecycle publication:\n");
    const int64_t baseline = rt_gc_tracked_count();
    shared_response_body = rt_const_cstr("live");
    void *server = rt_http_server_new(0);
    register_get(server, "/live", "simple");
    bind_native(server, "simple", simple_handler);

    std::vector<std::thread> starters;
    for (int index = 0; index < 8; index++)
        starters.emplace_back([server]() { rt_http_server_start(server); });
    for (std::thread &thread : starters)
        thread.join();
    test_result(rt_http_server_is_running(server) == 1 && rt_http_server_port(server) > 0,
                "concurrent Start calls publish one live listener");

    std::vector<std::thread> stoppers;
    for (int index = 0; index < 8; index++)
        stoppers.emplace_back([server]() { rt_http_server_stop(server); });
    for (std::thread &thread : stoppers)
        thread.join();
    test_result(rt_http_server_is_running(server) == 0,
                "concurrent Stop calls close and join the listener exactly once");

    rt_http_server_start(server);
    bool restarted = rt_http_server_is_running(server) == 1;
    rt_http_server_stop(server);
    test_result(restarted && rt_http_server_is_running(server) == 0,
                "serialized server remains restartable with its reusable worker pool");
    release_managed(server);
    release_managed(shared_response_body);
    shared_response_body = nullptr;
    test_result(rt_gc_tracked_count() == baseline,
                "lifecycle stress releases listener, pool, routes, and server");
}

/// @brief Run the focused HttpServer correctness and concurrency suite.
/// @return Zero after every assertion succeeds.
int main() {
#if RT_PLATFORM_WINDOWS
    WSADATA winsock_data{};
    assert(WSAStartup(MAKEWORD(2, 2), &winsock_data) == 0);
#endif

    test_identity_and_constructor_cleanup();
    test_forged_handle_rejection();
    test_managed_request_response_snapshots();
    test_synchronous_request_allocation_cleanup();
    test_transactional_json_response();
    test_reentrant_binding_cleanup();
    test_concurrent_lifecycle_publication();

#if RT_PLATFORM_WINDOWS
    WSACleanup();
#endif
    std::printf("\nAll HttpServer tests passed.\n");
    return 0;
}
