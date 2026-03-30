# POLISH-11: HTTP/1.1 Keep-Alive for Server

## Context
**Validated:** `rt_http_server.c` hardcodes `Connection: close` at:
- Line 13: Comment documenting no keep-alive
- Line 430: Buffer size includes `"Connection: close\r\n"`
- Line 479: `APPEND_FORMAT("%s", "Connection: close\r\n")`

**Validated:** NO `select()`/`poll()` calls exist. Current model is
thread-per-request via thread pool, NOT thread-per-connection.

**Impact:** Every HTTP request opens a new TCP connection. All modern
browsers expect keep-alive. Performance degrades significantly for
clients making multiple requests.

**Complexity: L** (requires thread model refactoring) | **Priority: P2**

## Design

### Thread Model Change

**Current:** Accept → dispatch to thread pool → handle request → close socket
**New:** Accept → spawn thread-per-connection → loop on requests → close after timeout

### Request Loop

In the per-connection handler:
```c
void handle_connection(int sock) {
    int requests = 0;
    while (requests < MAX_REQUESTS_PER_CONN) {
        // Wait for data with timeout
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int ready = poll(&pfd, 1, KEEPALIVE_TIMEOUT_MS);
        if (ready <= 0) break;  // Timeout or error → close

        // Read and handle request
        HttpRequest req;
        if (!read_request(sock, &req)) break;
        HttpResponse resp = route_and_handle(&req);

        // Check Connection header
        if (req_has_header(&req, "Connection", "close")) {
            resp_set_header(&resp, "Connection", "close");
            send_response(sock, &resp);
            break;
        }

        // Send with keep-alive
        resp_set_header(&resp, "Connection", "keep-alive");
        resp_set_header(&resp, "Keep-Alive", "timeout=30, max=100");
        send_response(sock, &resp);
        requests++;
    }
    close(sock);
}
```

### Constants
```c
#define KEEPALIVE_TIMEOUT_MS  30000  // 30 seconds idle timeout
#define MAX_REQUESTS_PER_CONN 100    // Max requests per connection
```

### Files to Modify

| File | Change |
|------|--------|
| `src/runtime/network/rt_http_server.c` | Replace thread-per-request with connection loop |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/viperlib/network.md` — Document keep-alive support

## Verification
```bash
# Test with curl (multiple requests on same connection)
curl -v --keepalive http://localhost:8080/test
curl -v --keepalive http://localhost:8080/test2

# Benchmark with wrk
wrk -t2 -c10 -d10s http://localhost:8080/
# Verify connections are reused (check connection count vs request count)
```
