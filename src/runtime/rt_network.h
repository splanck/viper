//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_network.h
// Purpose: TCP and UDP networking support for Viper.Network.
// Key invariants: Connections are blocking I/O with optional timeouts.
//                 TCP_NODELAY is enabled by default.
//                 UDP packets are discrete messages (not streams).
// Ownership/Lifetime: Connection objects are managed by GC.
// Links: docs/viperlib/network.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Tcp Client - Connection Creation
    //=========================================================================

    /// @brief Connect to a remote host.
    /// @param host Hostname or IP address.
    /// @param port Port number (1-65535).
    /// @return Tcp connection object.
    /// @note Traps on connection refused, host not found, or network error.
    void *rt_tcp_connect(rt_string host, int64_t port);

    /// @brief Connect to a remote host with timeout.
    /// @param host Hostname or IP address.
    /// @param port Port number (1-65535).
    /// @param timeout_ms Connection timeout in milliseconds.
    /// @return Tcp connection object.
    /// @note Traps on connection refused, host not found, timeout, or error.
    void *rt_tcp_connect_for(rt_string host, int64_t port, int64_t timeout_ms);

    //=========================================================================
    // Tcp Client - Properties
    //=========================================================================

    /// @brief Get the remote host name or IP.
    /// @param obj Tcp connection object.
    /// @return Host name or IP address.
    rt_string rt_tcp_host(void *obj);

    /// @brief Get the remote port number.
    /// @param obj Tcp connection object.
    /// @return Remote port number.
    int64_t rt_tcp_port(void *obj);

    /// @brief Get the local port number.
    /// @param obj Tcp connection object.
    /// @return Local port number.
    int64_t rt_tcp_local_port(void *obj);

    /// @brief Check if connection is open.
    /// @param obj Tcp connection object.
    /// @return 1 if open, 0 if closed.
    int8_t rt_tcp_is_open(void *obj);

    /// @brief Get bytes available to read without blocking.
    /// @param obj Tcp connection object.
    /// @return Number of bytes available.
    int64_t rt_tcp_available(void *obj);

    //=========================================================================
    // Tcp Client - Send Methods
    //=========================================================================

    /// @brief Send bytes over the connection.
    /// @param obj Tcp connection object.
    /// @param data Bytes object to send.
    /// @return Number of bytes actually sent.
    /// @note Traps if connection is closed.
    int64_t rt_tcp_send(void *obj, void *data);

    /// @brief Send string over the connection.
    /// @param obj Tcp connection object.
    /// @param text String to send (as UTF-8 bytes).
    /// @return Number of bytes actually sent.
    /// @note Traps if connection is closed.
    int64_t rt_tcp_send_str(void *obj, rt_string text);

    /// @brief Send all bytes, blocking until complete.
    /// @param obj Tcp connection object.
    /// @param data Bytes object to send.
    /// @note Traps if connection is closed or send fails.
    void rt_tcp_send_all(void *obj, void *data);

    //=========================================================================
    // Tcp Client - Receive Methods
    //=========================================================================

    /// @brief Receive up to maxBytes from the connection.
    /// @param obj Tcp connection object.
    /// @param max_bytes Maximum bytes to receive.
    /// @return Bytes object with received data (may be less than max).
    /// @note Returns empty Bytes if connection closed by peer.
    /// @note Traps on receive error.
    void *rt_tcp_recv(void *obj, int64_t max_bytes);

    /// @brief Receive up to maxBytes as a string.
    /// @param obj Tcp connection object.
    /// @param max_bytes Maximum bytes to receive.
    /// @return String with received data (UTF-8).
    /// @note Returns empty string if connection closed by peer.
    /// @note Traps on receive error.
    rt_string rt_tcp_recv_str(void *obj, int64_t max_bytes);

    /// @brief Receive exactly count bytes, blocking until complete.
    /// @param obj Tcp connection object.
    /// @param count Exact number of bytes to receive.
    /// @return Bytes object with received data.
    /// @note Traps if connection closed before count bytes received.
    void *rt_tcp_recv_exact(void *obj, int64_t count);

    /// @brief Receive until newline (LF or CRLF).
    /// @param obj Tcp connection object.
    /// @return Line without trailing newline.
    /// @note Traps if connection closed before newline.
    rt_string rt_tcp_recv_line(void *obj);

    //=========================================================================
    // Tcp Client - Timeout and Close
    //=========================================================================

    /// @brief Set receive timeout.
    /// @param obj Tcp connection object.
    /// @param timeout_ms Timeout in milliseconds (0 = no timeout).
    void rt_tcp_set_recv_timeout(void *obj, int64_t timeout_ms);

    /// @brief Set send timeout.
    /// @param obj Tcp connection object.
    /// @param timeout_ms Timeout in milliseconds (0 = no timeout).
    void rt_tcp_set_send_timeout(void *obj, int64_t timeout_ms);

    /// @brief Close the connection.
    /// @param obj Tcp connection object.
    void rt_tcp_close(void *obj);

    //=========================================================================
    // TcpServer - Creation
    //=========================================================================

    /// @brief Start listening on a port on all interfaces.
    /// @param port Port number to listen on.
    /// @return TcpServer object.
    /// @note Traps if port is in use or permission denied.
    void *rt_tcp_server_listen(int64_t port);

    /// @brief Start listening on a specific address and port.
    /// @param address Address to bind to (e.g., "127.0.0.1" or "0.0.0.0").
    /// @param port Port number to listen on.
    /// @return TcpServer object.
    /// @note Traps if port is in use, permission denied, or invalid address.
    void *rt_tcp_server_listen_at(rt_string address, int64_t port);

    //=========================================================================
    // TcpServer - Properties
    //=========================================================================

    /// @brief Get the listening port.
    /// @param obj TcpServer object.
    /// @return Port number.
    int64_t rt_tcp_server_port(void *obj);

    /// @brief Get the bound address.
    /// @param obj TcpServer object.
    /// @return Bound address string.
    rt_string rt_tcp_server_address(void *obj);

    /// @brief Check if server is listening.
    /// @param obj TcpServer object.
    /// @return 1 if listening, 0 if closed.
    int8_t rt_tcp_server_is_listening(void *obj);

    //=========================================================================
    // TcpServer - Accept and Close
    //=========================================================================

    /// @brief Accept a new connection, blocking until client connects.
    /// @param obj TcpServer object.
    /// @return Tcp connection object for the new client.
    /// @note Traps if server is closed or accept fails.
    void *rt_tcp_server_accept(void *obj);

    /// @brief Accept a new connection with timeout.
    /// @param obj TcpServer object.
    /// @param timeout_ms Timeout in milliseconds.
    /// @return Tcp connection object, or NULL on timeout.
    /// @note Traps if server is closed or accept fails (except timeout).
    void *rt_tcp_server_accept_for(void *obj, int64_t timeout_ms);

    /// @brief Stop listening and close the server.
    /// @param obj TcpServer object.
    void rt_tcp_server_close(void *obj);

    //=========================================================================
    // Udp - Creation
    //=========================================================================

    /// @brief Create an unbound UDP socket.
    /// @return Udp socket object.
    void *rt_udp_new(void);

    /// @brief Create a UDP socket bound to a port on all interfaces.
    /// @param port Port number to bind to.
    /// @return Udp socket object.
    /// @note Traps if port is in use or permission denied.
    void *rt_udp_bind(int64_t port);

    /// @brief Create a UDP socket bound to a specific address and port.
    /// @param address Address to bind to (e.g., "127.0.0.1" or "0.0.0.0").
    /// @param port Port number to bind to.
    /// @return Udp socket object.
    /// @note Traps if port is in use, permission denied, or invalid address.
    void *rt_udp_bind_at(rt_string address, int64_t port);

    //=========================================================================
    // Udp - Properties
    //=========================================================================

    /// @brief Get the bound port (0 if not bound).
    /// @param obj Udp socket object.
    /// @return Port number or 0.
    int64_t rt_udp_port(void *obj);

    /// @brief Get the bound address.
    /// @param obj Udp socket object.
    /// @return Bound address string (empty if not bound).
    rt_string rt_udp_address(void *obj);

    /// @brief Check if socket is bound.
    /// @param obj Udp socket object.
    /// @return 1 if bound, 0 if not bound.
    int8_t rt_udp_is_bound(void *obj);

    //=========================================================================
    // Udp - Send Methods
    //=========================================================================

    /// @brief Send a UDP packet to a host and port.
    /// @param obj Udp socket object.
    /// @param host Destination hostname or IP address.
    /// @param port Destination port number.
    /// @param data Bytes object to send.
    /// @return Number of bytes sent.
    /// @note Traps on host not found or message too large.
    int64_t rt_udp_send_to(void *obj, rt_string host, int64_t port, void *data);

    /// @brief Send a UDP packet as a string.
    /// @param obj Udp socket object.
    /// @param host Destination hostname or IP address.
    /// @param port Destination port number.
    /// @param text String to send (as UTF-8 bytes).
    /// @return Number of bytes sent.
    /// @note Traps on host not found or message too large.
    int64_t rt_udp_send_to_str(void *obj, rt_string host, int64_t port, rt_string text);

    //=========================================================================
    // Udp - Receive Methods
    //=========================================================================

    /// @brief Receive a UDP packet (blocks until data arrives).
    /// @param obj Udp socket object.
    /// @param max_bytes Maximum bytes to receive.
    /// @return Bytes object with received data.
    /// @note Traps if socket is closed.
    void *rt_udp_recv(void *obj, int64_t max_bytes);

    /// @brief Receive a UDP packet and store sender info.
    /// @param obj Udp socket object.
    /// @param max_bytes Maximum bytes to receive.
    /// @return Bytes object with received data.
    /// @note Sender info accessible via rt_udp_sender_host/port.
    /// @note Traps if socket is closed.
    void *rt_udp_recv_from(void *obj, int64_t max_bytes);

    /// @brief Receive a UDP packet with timeout.
    /// @param obj Udp socket object.
    /// @param max_bytes Maximum bytes to receive.
    /// @param timeout_ms Timeout in milliseconds.
    /// @return Bytes object with received data, or NULL on timeout.
    /// @note Traps if socket is closed (except timeout returns NULL).
    void *rt_udp_recv_for(void *obj, int64_t max_bytes, int64_t timeout_ms);

    /// @brief Get the hostname/IP of the last received packet's sender.
    /// @param obj Udp socket object.
    /// @return Sender's host address.
    rt_string rt_udp_sender_host(void *obj);

    /// @brief Get the port of the last received packet's sender.
    /// @param obj Udp socket object.
    /// @return Sender's port number.
    int64_t rt_udp_sender_port(void *obj);

    //=========================================================================
    // Udp - Options and Close
    //=========================================================================

    /// @brief Enable or disable broadcast.
    /// @param obj Udp socket object.
    /// @param enable 1 to enable, 0 to disable.
    void rt_udp_set_broadcast(void *obj, int8_t enable);

    /// @brief Join a multicast group.
    /// @param obj Udp socket object.
    /// @param group_addr Multicast group address (224.0.0.0 - 239.255.255.255).
    /// @note Traps if invalid multicast address.
    void rt_udp_join_group(void *obj, rt_string group_addr);

    /// @brief Leave a multicast group.
    /// @param obj Udp socket object.
    /// @param group_addr Multicast group address.
    void rt_udp_leave_group(void *obj, rt_string group_addr);

    /// @brief Set receive timeout.
    /// @param obj Udp socket object.
    /// @param timeout_ms Timeout in milliseconds (0 = no timeout).
    void rt_udp_set_recv_timeout(void *obj, int64_t timeout_ms);

    /// @brief Close the UDP socket.
    /// @param obj Udp socket object.
    void rt_udp_close(void *obj);

    //=========================================================================
    // Dns - Static Utility Class
    //=========================================================================

    /// @brief Resolve hostname to first IPv4 address.
    /// @param hostname Hostname to resolve.
    /// @return IPv4 address as string (dotted decimal).
    /// @note Traps if hostname not found.
    rt_string rt_dns_resolve(rt_string hostname);

    /// @brief Resolve hostname to all addresses (IPv4 and IPv6).
    /// @param hostname Hostname to resolve.
    /// @return Seq of address strings.
    /// @note Traps if hostname not found.
    void *rt_dns_resolve_all(rt_string hostname);

    /// @brief Resolve hostname to first IPv4 address only.
    /// @param hostname Hostname to resolve.
    /// @return IPv4 address as string.
    /// @note Traps if hostname not found or no IPv4 address.
    rt_string rt_dns_resolve4(rt_string hostname);

    /// @brief Resolve hostname to first IPv6 address only.
    /// @param hostname Hostname to resolve.
    /// @return IPv6 address as string (colon hex).
    /// @note Traps if hostname not found or no IPv6 address.
    rt_string rt_dns_resolve6(rt_string hostname);

    /// @brief Reverse DNS lookup - get hostname from IP address.
    /// @param ip_address IPv4 or IPv6 address string.
    /// @return Hostname.
    /// @note Traps if address not found.
    rt_string rt_dns_reverse(rt_string ip_address);

    /// @brief Check if string is a valid IPv4 address.
    /// @param address Address string to check.
    /// @return 1 if valid IPv4, 0 otherwise.
    int8_t rt_dns_is_ipv4(rt_string address);

    /// @brief Check if string is a valid IPv6 address.
    /// @param address Address string to check.
    /// @return 1 if valid IPv6, 0 otherwise.
    int8_t rt_dns_is_ipv6(rt_string address);

    /// @brief Check if string is a valid IP address (IPv4 or IPv6).
    /// @param address Address string to check.
    /// @return 1 if valid IP, 0 otherwise.
    int8_t rt_dns_is_ip(rt_string address);

    /// @brief Get local machine hostname.
    /// @return Local hostname.
    rt_string rt_dns_local_host(void);

    /// @brief Get all local IP addresses.
    /// @return Seq of address strings.
    void *rt_dns_local_addrs(void);

    //=========================================================================
    // Http - Static Utility Class (Simple HTTP/1.1 Client)
    //=========================================================================

    /// @brief HTTP GET request, return body as string.
    /// @param url Full URL (http://host/path).
    /// @return Response body as string.
    /// @note Traps on connection error, invalid URL, or HTTP error.
    rt_string rt_http_get(rt_string url);

    /// @brief HTTP GET request, return body as bytes.
    /// @param url Full URL (http://host/path).
    /// @return Response body as Bytes.
    /// @note Traps on connection error, invalid URL, or HTTP error.
    void *rt_http_get_bytes(rt_string url);

    /// @brief HTTP POST request with string body.
    /// @param url Full URL (http://host/path).
    /// @param body Request body as string.
    /// @return Response body as string.
    /// @note Traps on connection error, invalid URL, or HTTP error.
    rt_string rt_http_post(rt_string url, rt_string body);

    /// @brief HTTP POST request with bytes body.
    /// @param url Full URL (http://host/path).
    /// @param body Request body as Bytes.
    /// @return Response body as Bytes.
    /// @note Traps on connection error, invalid URL, or HTTP error.
    void *rt_http_post_bytes(rt_string url, void *body);

    /// @brief Download URL to file.
    /// @param url Full URL (http://host/path).
    /// @param dest_path Destination file path.
    /// @return 1 on success, 0 on failure.
    int8_t rt_http_download(rt_string url, rt_string dest_path);

    /// @brief HTTP HEAD request, return headers.
    /// @param url Full URL (http://host/path).
    /// @return Map of response headers.
    /// @note Traps on connection error, invalid URL, or HTTP error.
    void *rt_http_head(rt_string url);

    //=========================================================================
    // HttpReq - Request Builder (Instance Class)
    //=========================================================================

    /// @brief Create new HTTP request.
    /// @param method HTTP method (GET, POST, PUT, DELETE, etc.).
    /// @param url Full URL (http://host/path).
    /// @return HttpReq object.
    void *rt_http_req_new(rt_string method, rt_string url);

    /// @brief Set request header.
    /// @param obj HttpReq object.
    /// @param name Header name.
    /// @param value Header value.
    /// @return Same HttpReq object (for chaining).
    void *rt_http_req_set_header(void *obj, rt_string name, rt_string value);

    /// @brief Set request body as bytes.
    /// @param obj HttpReq object.
    /// @param data Bytes object.
    /// @return Same HttpReq object (for chaining).
    void *rt_http_req_set_body(void *obj, void *data);

    /// @brief Set request body as string.
    /// @param obj HttpReq object.
    /// @param text String body.
    /// @return Same HttpReq object (for chaining).
    void *rt_http_req_set_body_str(void *obj, rt_string text);

    /// @brief Set request timeout.
    /// @param obj HttpReq object.
    /// @param timeout_ms Timeout in milliseconds.
    /// @return Same HttpReq object (for chaining).
    void *rt_http_req_set_timeout(void *obj, int64_t timeout_ms);

    /// @brief Execute HTTP request.
    /// @param obj HttpReq object.
    /// @return HttpRes response object.
    /// @note Traps on connection error or timeout.
    void *rt_http_req_send(void *obj);

    //=========================================================================
    // HttpRes - Response (Instance Class)
    //=========================================================================

    /// @brief Get response status code.
    /// @param obj HttpRes object.
    /// @return HTTP status code (e.g., 200, 404).
    int64_t rt_http_res_status(void *obj);

    /// @brief Get response status text.
    /// @param obj HttpRes object.
    /// @return Status text (e.g., "OK", "Not Found").
    rt_string rt_http_res_status_text(void *obj);

    /// @brief Get all response headers.
    /// @param obj HttpRes object.
    /// @return Map of header name -> value.
    void *rt_http_res_headers(void *obj);

    /// @brief Get response body as bytes.
    /// @param obj HttpRes object.
    /// @return Response body as Bytes.
    void *rt_http_res_body(void *obj);

    /// @brief Get response body as string.
    /// @param obj HttpRes object.
    /// @return Response body as string.
    rt_string rt_http_res_body_str(void *obj);

    /// @brief Get specific response header.
    /// @param obj HttpRes object.
    /// @param name Header name (case-insensitive).
    /// @return Header value, or empty string if not found.
    rt_string rt_http_res_header(void *obj, rt_string name);

    /// @brief Check if response is success (2xx status).
    /// @param obj HttpRes object.
    /// @return 1 if status 200-299, 0 otherwise.
    int8_t rt_http_res_is_ok(void *obj);

    //=========================================================================
    // Url - URL Parsing and Construction
    //=========================================================================

    /// @brief Parse URL string into Url object.
    /// @param url_str URL string to parse.
    /// @return Url object.
    /// @note Traps on invalid URL format.
    void *rt_url_parse(rt_string url_str);

    /// @brief Create empty Url object for building.
    /// @return Empty Url object.
    void *rt_url_new(void);

    /// @brief Get URL scheme (http, https, ftp, etc.).
    /// @param obj Url object.
    /// @return Scheme string.
    rt_string rt_url_scheme(void *obj);

    /// @brief Set URL scheme.
    /// @param obj Url object.
    /// @param scheme Scheme string.
    void rt_url_set_scheme(void *obj, rt_string scheme);

    /// @brief Get URL host.
    /// @param obj Url object.
    /// @return Host string.
    rt_string rt_url_host(void *obj);

    /// @brief Set URL host.
    /// @param obj Url object.
    /// @param host Host string.
    void rt_url_set_host(void *obj, rt_string host);

    /// @brief Get URL port (0 if not specified).
    /// @param obj Url object.
    /// @return Port number.
    int64_t rt_url_port(void *obj);

    /// @brief Set URL port.
    /// @param obj Url object.
    /// @param port Port number.
    void rt_url_set_port(void *obj, int64_t port);

    /// @brief Get URL path.
    /// @param obj Url object.
    /// @return Path string.
    rt_string rt_url_path(void *obj);

    /// @brief Set URL path.
    /// @param obj Url object.
    /// @param path Path string.
    void rt_url_set_path(void *obj, rt_string path);

    /// @brief Get URL query string (without leading ?).
    /// @param obj Url object.
    /// @return Query string.
    rt_string rt_url_query(void *obj);

    /// @brief Set URL query string.
    /// @param obj Url object.
    /// @param query Query string.
    void rt_url_set_query(void *obj, rt_string query);

    /// @brief Get URL fragment (without leading #).
    /// @param obj Url object.
    /// @return Fragment string.
    rt_string rt_url_fragment(void *obj);

    /// @brief Set URL fragment.
    /// @param obj Url object.
    /// @param fragment Fragment string.
    void rt_url_set_fragment(void *obj, rt_string fragment);

    /// @brief Get URL username.
    /// @param obj Url object.
    /// @return Username string.
    rt_string rt_url_user(void *obj);

    /// @brief Set URL username.
    /// @param obj Url object.
    /// @param user Username string.
    void rt_url_set_user(void *obj, rt_string user);

    /// @brief Get URL password.
    /// @param obj Url object.
    /// @return Password string.
    rt_string rt_url_pass(void *obj);

    /// @brief Set URL password.
    /// @param obj Url object.
    /// @param pass Password string.
    void rt_url_set_pass(void *obj, rt_string pass);

    /// @brief Get URL authority (user:pass@host:port).
    /// @param obj Url object.
    /// @return Authority string.
    rt_string rt_url_authority(void *obj);

    /// @brief Get host:port (port omitted if default for scheme).
    /// @param obj Url object.
    /// @return HostPort string.
    rt_string rt_url_host_port(void *obj);

    /// @brief Get complete URL string.
    /// @param obj Url object.
    /// @return Full URL string.
    rt_string rt_url_full(void *obj);

    /// @brief Set or update query parameter.
    /// @param obj Url object.
    /// @param name Parameter name.
    /// @param value Parameter value.
    /// @return Same Url object for chaining.
    void *rt_url_set_query_param(void *obj, rt_string name, rt_string value);

    /// @brief Get query parameter value.
    /// @param obj Url object.
    /// @param name Parameter name.
    /// @return Parameter value, or empty string if not found.
    rt_string rt_url_get_query_param(void *obj, rt_string name);

    /// @brief Check if query parameter exists.
    /// @param obj Url object.
    /// @param name Parameter name.
    /// @return 1 if exists, 0 otherwise.
    int8_t rt_url_has_query_param(void *obj, rt_string name);

    /// @brief Remove query parameter.
    /// @param obj Url object.
    /// @param name Parameter name.
    /// @return Same Url object for chaining.
    void *rt_url_del_query_param(void *obj, rt_string name);

    /// @brief Get all query parameters as Map.
    /// @param obj Url object.
    /// @return Map of query parameters.
    void *rt_url_query_map(void *obj);

    /// @brief Resolve relative URL against this base URL.
    /// @param obj Base Url object.
    /// @param relative Relative URL string.
    /// @return New Url object with resolved URL.
    void *rt_url_resolve(void *obj, rt_string relative);

    /// @brief Clone URL object.
    /// @param obj Url object.
    /// @return New Url object with same values.
    void *rt_url_clone(void *obj);

    /// @brief Percent-encode text for URL.
    /// @param text Text to encode.
    /// @return Encoded string.
    rt_string rt_url_encode(rt_string text);

    /// @brief Decode percent-encoded text.
    /// @param text Encoded text.
    /// @return Decoded string.
    rt_string rt_url_decode(rt_string text);

    /// @brief Encode Map as query string.
    /// @param map Map of key-value pairs.
    /// @return Query string.
    rt_string rt_url_encode_query(void *map);

    /// @brief Parse query string to Map.
    /// @param query Query string.
    /// @return Map of key-value pairs.
    void *rt_url_decode_query(rt_string query);

    /// @brief Check if URL string is valid/parseable.
    /// @param url_str URL string to validate.
    /// @return 1 if valid, 0 otherwise.
    int8_t rt_url_is_valid(rt_string url_str);

#ifdef __cplusplus
}
#endif
