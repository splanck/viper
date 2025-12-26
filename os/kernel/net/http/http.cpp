/**
 * @file http.cpp
 * @brief Minimal HTTP client implementation.
 *
 * @details
 * Implements the simple HTTP client API from `http.hpp`. The client uses the
 * TCP stack directly, resolves hostnames via DNS, and reads the response into a
 * fixed-size buffer for basic parsing.
 */

#include "http.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../dns/dns.hpp"
#include "../ip/tcp.hpp"
#include "../network.hpp"

namespace net
{
namespace http
{

// HTTP port
constexpr u16 HTTP_PORT = 80;

static bool initialized = false;

/**
 * @brief Parse a positive decimal integer from a string prefix.
 *
 * @details
 * Parses consecutive ASCII digits and stops at the first non-digit. Used for
 * parsing HTTP status codes and header values.
 *
 * @param s Pointer to a NUL-terminated string.
 * @return Parsed integer value (0 if the string does not start with digits).
 */
static i32 parse_int(const char *s)
{
    i32 result = 0;
    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result;
}

/**
 * @brief Find a substring within a string.
 *
 * @details
 * Simple freestanding substring search used for parsing HTTP response headers.
 *
 * @param haystack String to search within.
 * @param needle Substring to find.
 * @return Pointer to the first match, or `nullptr` if not found.
 */
static const char *find_str(const char *haystack, const char *needle)
{
    if (!*needle)
        return haystack;
    for (; *haystack; haystack++)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n)
        {
            h++;
            n++;
        }
        if (!*n)
            return haystack;
    }
    return nullptr;
}

/**
 * @brief Case-insensitive ASCII character comparison.
 *
 * @param a First character.
 * @param b Second character.
 * @return `true` if characters match ignoring ASCII case, otherwise `false`.
 */
static bool char_eq_i(char a, char b)
{
    if (a >= 'A' && a <= 'Z')
        a += 32;
    if (b >= 'A' && b <= 'Z')
        b += 32;
    return a == b;
}

/**
 * @brief Case-insensitive ASCII string prefix test.
 *
 * @param str Input string.
 * @param prefix Prefix to check for.
 * @return `true` if `str` begins with `prefix` (ignoring ASCII case).
 */
static bool starts_with_i(const char *str, const char *prefix)
{
    while (*prefix)
    {
        if (!*str || !char_eq_i(*str, *prefix))
            return false;
        str++;
        prefix++;
    }
    return true;
}

/** @copydoc net::http::http_init */
void http_init()
{
    initialized = true;
    serial::puts("[http] HTTP client initialized\n");
}

/** @copydoc net::http::get */
bool get(const char *host, const char *path, HttpResponse *response, u32 timeout_ms)
{
    if (!response)
        return false;

    // Initialize response
    response->status_code = 0;
    response->content_type[0] = '\0';
    response->content_length = 0;
    response->body[0] = '\0';
    response->body_len = 0;
    response->success = false;
    response->error = nullptr;

    // Resolve hostname
    Ipv4Addr server_ip;
    serial::puts("[http] Resolving ");
    serial::puts(host);
    serial::puts("...\n");

    if (!dns::resolve(host, &server_ip, 5000))
    {
        response->error = "DNS resolution failed";
        return false;
    }

    serial::puts("[http] Connecting to ");
    serial::put_dec(server_ip.bytes[0]);
    serial::putc('.');
    serial::put_dec(server_ip.bytes[1]);
    serial::putc('.');
    serial::put_dec(server_ip.bytes[2]);
    serial::putc('.');
    serial::put_dec(server_ip.bytes[3]);
    serial::puts(":80\n");

    // Create TCP connection
    i32 sock = tcp::socket_create();
    if (sock < 0)
    {
        response->error = "Failed to create socket";
        return false;
    }

    if (!tcp::socket_connect(sock, server_ip, HTTP_PORT))
    {
        tcp::socket_close(sock);
        response->error = "Connection failed";
        return false;
    }

    serial::puts("[http] Connected, sending request\n");

    // Build HTTP request
    static char request[512];
    usize pos = 0;

    // GET /path HTTP/1.0
    const char *method = "GET ";
    while (*method)
        request[pos++] = *method++;
    while (*path)
        request[pos++] = *path++;
    const char *version = " HTTP/1.0\r\n";
    while (*version)
        request[pos++] = *version++;

    // Host header
    const char *host_hdr = "Host: ";
    while (*host_hdr)
        request[pos++] = *host_hdr++;
    while (*host)
        request[pos++] = *host++;
    request[pos++] = '\r';
    request[pos++] = '\n';

    // Connection: close
    const char *conn_hdr = "Connection: close\r\n";
    while (*conn_hdr)
        request[pos++] = *conn_hdr++;

    // End of headers
    request[pos++] = '\r';
    request[pos++] = '\n';
    request[pos] = '\0';

    // Send request
    i32 sent = tcp::socket_send(sock, request, pos);
    if (sent <= 0)
    {
        tcp::socket_close(sock);
        response->error = "Failed to send request";
        return false;
    }

    serial::puts("[http] Request sent, waiting for response\n");

    // Receive response
    static char recv_buf[8192];
    usize recv_total = 0;
    u64 start = timer::get_ticks();

    while (timer::get_ticks() - start < timeout_ms)
    {
        i32 n = tcp::socket_recv(sock, recv_buf + recv_total, sizeof(recv_buf) - recv_total - 1);
        if (n > 0)
        {
            recv_total += n;
            recv_buf[recv_total] = '\0';

            // Check if we've received the end of response
            if (find_str(recv_buf, "\r\n\r\n"))
            {
                // Give a bit more time for body
                u64 body_start = timer::get_ticks();
                while (timer::get_ticks() - body_start < 1000)
                {
                    i32 m = tcp::socket_recv(
                        sock, recv_buf + recv_total, sizeof(recv_buf) - recv_total - 1);
                    if (m > 0)
                    {
                        recv_total += m;
                        recv_buf[recv_total] = '\0';
                    }
                    else
                    {
                        break;
                    }
                    if (recv_total >= sizeof(recv_buf) - 1)
                        break;
                }
                break;
            }
        }
        else if (n < 0)
        {
            break; // Connection closed
        }
        asm volatile("wfi");
    }

    tcp::socket_close(sock);

    if (recv_total == 0)
    {
        response->error = "No response received";
        return false;
    }

    serial::puts("[http] Received ");
    serial::put_dec(recv_total);
    serial::puts(" bytes\n");

    // Parse HTTP response
    // Status line: HTTP/1.x NNN Reason
    if (recv_total < 12 || recv_buf[0] != 'H' || recv_buf[1] != 'T' || recv_buf[2] != 'T' ||
        recv_buf[3] != 'P')
    {
        response->error = "Invalid HTTP response";
        return false;
    }

    // Find status code
    const char *status_start = find_str(recv_buf, " ");
    if (!status_start)
    {
        response->error = "Invalid status line";
        return false;
    }
    status_start++;
    response->status_code = parse_int(status_start);

    // Find headers end
    const char *headers_end = find_str(recv_buf, "\r\n\r\n");
    if (!headers_end)
    {
        response->error = "Invalid headers";
        return false;
    }

    // Parse Content-Type header
    const char *ct = recv_buf;
    while ((ct = find_str(ct, "\r\n")) != nullptr && ct < headers_end)
    {
        ct += 2;
        if (starts_with_i(ct, "Content-Type:"))
        {
            ct += 13;
            while (*ct == ' ')
                ct++;
            usize i = 0;
            while (*ct && *ct != '\r' && i < 63)
            {
                response->content_type[i++] = *ct++;
            }
            response->content_type[i] = '\0';
            break;
        }
    }

    // Copy body
    const char *body_start = headers_end + 4;
    usize body_avail = recv_total - (body_start - recv_buf);
    response->body_len =
        body_avail < HttpResponse::BODY_MAX - 1 ? body_avail : HttpResponse::BODY_MAX - 1;
    for (usize i = 0; i < response->body_len; i++)
    {
        response->body[i] = body_start[i];
    }
    response->body[response->body_len] = '\0';

    response->success = (response->status_code >= 200 && response->status_code < 300);
    return true;
}

/** @copydoc net::http::fetch */
void fetch(const char *host, const char *path)
{
    serial::puts("[http] Fetching http://");
    serial::puts(host);
    serial::puts(path);
    serial::puts("\n");

    HttpResponse response;
    if (get(host, path, &response))
    {
        serial::puts("[http] Status: ");
        serial::put_dec(response.status_code);
        serial::puts("\n");

        if (response.content_type[0])
        {
            serial::puts("[http] Content-Type: ");
            serial::puts(response.content_type);
            serial::puts("\n");
        }

        serial::puts("[http] Body (");
        serial::put_dec(response.body_len);
        serial::puts(" bytes):\n");

        // Print first 500 chars of body
        for (usize i = 0; i < response.body_len && i < 500; i++)
        {
            serial::putc(response.body[i]);
        }
        if (response.body_len > 500)
        {
            serial::puts("\n[...truncated...]\n");
        }
        serial::puts("\n");
    }
    else
    {
        serial::puts("[http] Failed: ");
        serial::puts(response.error ? response.error : "Unknown error");
        serial::puts("\n");
    }
}

} // namespace http
} // namespace net
