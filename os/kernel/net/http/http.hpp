#pragma once

/**
 * @file http.hpp
 * @brief Minimal HTTP client built on the TCP and DNS layers.
 *
 * @details
 * Provides a lightweight HTTP/1.0 client used for demonstrations and simple
 * bring-up testing of the TCP/DNS stack. The client:
 * - Resolves a hostname to an IPv4 address using @ref net::dns::resolve.
 * - Establishes a TCP connection to port 80.
 * - Sends a basic `GET` request and reads until headers are present and the
 *   connection closes.
 * - Parses the status code and some headers, then copies a bounded amount of
 *   body data into a caller-provided buffer.
 *
 * This client is intentionally minimal:
 * - No HTTPS/TLS support here (TLS is implemented separately).
 * - No chunked transfer decoding or streaming body support.
 * - Limited header parsing and fixed-size buffers.
 */

#include "../net.hpp"

namespace net
{
namespace http
{

/**
 * @brief Parsed HTTP response returned by the client.
 *
 * @details
 * The client stores a subset of HTTP response information:
 * - `status_code` parsed from the status line.
 * - `content_type` if present.
 * - A bounded copy of the response body.
 *
 * When an error occurs, `success` is set to false and `error` points to a
 * static message describing the failure.
 */
struct HttpResponse
{
    i32 status_code;
    char content_type[64];
    usize content_length;

    static constexpr usize BODY_MAX = 4096;
    char body[BODY_MAX];
    usize body_len;

    bool success;
    const char *error;
};

/**
 * @brief Initialize the HTTP client.
 *
 * @details
 * Currently marks the client initialized and prints diagnostics. Higher-level
 * networking initialization should still be performed via @ref net::network_init.
 */
void http_init();

/**
 * @brief Perform an HTTP GET request.
 *
 * @details
 * Resolves `host`, connects to port 80, sends a basic HTTP/1.0 request with a
 * `Host:` header, then reads response bytes into internal buffers. The function
 * extracts:
 * - HTTP status code.
 * - Content-Type header (if present).
 * - Up to @ref HttpResponse::BODY_MAX bytes of response body.
 *
 * The call waits up to `timeout_ms` milliseconds for response data while
 * polling the network stack.
 *
 * @param host Hostname (e.g., "example.com").
 * @param path Path component beginning with `/` (e.g., "/index.html").
 * @param response Output response structure; fields are initialized by the function.
 * @param timeout_ms Timeout in milliseconds.
 * @return `true` if a response was parsed (even if non-2xx), otherwise `false`.
 */
bool get(const char *host, const char *path, HttpResponse *response, u32 timeout_ms = 10000);

/**
 * @brief Convenience helper to fetch a URL and print results.
 *
 * @details
 * Calls @ref get and prints a summary (status, content type, and up to a
 * bounded amount of response body) to the serial console.
 *
 * @param host Hostname.
 * @param path Path component.
 */
void fetch(const char *host, const char *path);

} // namespace http
} // namespace net
