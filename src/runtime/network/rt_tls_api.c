//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_tls_api.c
// Purpose: Viper-facing wrappers for the TLS runtime — the boxed rt_viper_tls
//          object plus the Viper.Net.TLS methods (connect, send/recv, close,
//          accessors). Split out of rt_tls.c; calls the low-level rt_tls_*
//          API declared in rt_tls.h.
//
// Key invariants:
//   - These wrappers convert between Viper types (rt_string, Bytes) and the C
//     TLS session API; the boxed object owns its session + host string.
//   - The underlying handshake/record engine lives in rt_tls.c and is reached
//     only through the public rt_tls_* functions.
//
// Ownership/Lifetime:
//   - rt_viper_tls_t is a GC object finalized via rt_viper_tls_finalize, which
//     closes the session and frees the duplicated host string.
//
// Links: src/runtime/network/rt_tls.c (TLS protocol engine),
//        src/runtime/network/rt_tls.h (low-level API),
//        src/runtime/network/rt_tls_internal.h (shared TLS types/ids)
//
//===----------------------------------------------------------------------===//

#include "rt_tls.h"
#include "rt_tls_internal.h"

#include "rt_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Viper API Wrappers
//=============================================================================
//
// These functions wrap the low-level TLS API for use by the Viper runtime.
// They handle conversion between Viper types (rt_string, Bytes) and C types.
//
//=============================================================================

#include "rt_bytes.h"
#include "rt_object.h"
#include "rt_string.h"

/// @brief Internal structure for Viper TLS objects.
typedef struct rt_viper_tls {
    rt_tls_session_t *session;
    char *host;
    int64_t port;
} rt_viper_tls_t;

/// @brief Finalizer for TLS objects.
static void rt_viper_tls_finalize(void *obj) {
    if (!obj)
        return;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session) {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
    if (tls->host) {
        free(tls->host);
        tls->host = NULL;
    }
}

static rt_viper_tls_t *rt_viper_tls_require(void *obj) {
    if (!obj)
        return NULL;
    if (rt_obj_class_id(obj) != RT_TLS_CLASS_ID) {
        rt_trap("Tls: invalid object");
        return NULL;
    }
    return (rt_viper_tls_t *)obj;
}

static int rt_viper_tls_string_arg(
    rt_string value, const char **out, size_t *out_len, int allow_empty, size_t max_len) {
    if (!out || !out_len)
        return 0;
    *out = "";
    *out_len = 0;
    if (!value)
        return allow_empty;
    int64_t len64 = rt_str_len(value);
    if (len64 < 0 || (!allow_empty && len64 == 0) || (size_t)len64 >= max_len)
        return 0;
    const char *cstr = rt_string_cstr(value);
    if (!cstr)
        return 0;
    if (strlen(cstr) != (size_t)len64)
        return 0;
    *out = cstr;
    *out_len = (size_t)len64;
    return 1;
}

static void *rt_viper_tls_object_from_session(rt_tls_session_t *session,
                                              const char *host_cstr,
                                              int64_t port) {
    rt_viper_tls_t *tls =
        (rt_viper_tls_t *)rt_obj_new_i64(RT_TLS_CLASS_ID, (int64_t)sizeof(rt_viper_tls_t));
    if (!tls) {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = NULL;
    tls->port = port;
    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    tls->host = strdup(host_cstr ? host_cstr : "");
    if (!tls->host) {
        if (rt_obj_release_check0(tls))
            rt_obj_free(tls);
        return NULL;
    }

    return tls;
}

static void *rt_viper_tls_connect_impl(rt_string host,
                                       int64_t port,
                                       int64_t timeout_ms,
                                       rt_string ca_file,
                                       rt_string alpn,
                                       int verify_cert) {
    if (port < 1 || port > 65535)
        return NULL;
    if (timeout_ms > INT_MAX)
        return NULL;
    if (timeout_ms < 0)
        timeout_ms = 0;

    const char *host_cstr = NULL;
    const char *ca_cstr = NULL;
    const char *alpn_cstr = NULL;
    size_t host_len = 0;
    size_t ca_len = 0;
    size_t alpn_len = 0;
    if (!rt_viper_tls_string_arg(
            host, &host_cstr, &host_len, 0, sizeof(((rt_tls_session_t *)0)->hostname)))
        return NULL;
    if (!rt_viper_tls_string_arg(
            ca_file, &ca_cstr, &ca_len, 1, sizeof(((rt_tls_session_t *)0)->ca_file)))
        return NULL;
    if (!rt_viper_tls_string_arg(
            alpn, &alpn_cstr, &alpn_len, 1, sizeof(((rt_tls_session_t *)0)->alpn_protocols)))
        return NULL;

    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = host_cstr;
    config.timeout_ms = (int)timeout_ms;
    config.verify_cert = verify_cert ? 1 : 0;
    if (ca_len > 0)
        config.ca_file = ca_cstr;
    if (alpn_len > 0)
        config.alpn_protocol = alpn_cstr;

    rt_tls_session_t *session = rt_tls_connect(host_cstr, (uint16_t)port, &config);
    if (!session)
        return NULL;
    return rt_viper_tls_object_from_session(session, host_cstr, port);
}

/// @brief Connect to a TLS server.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect(rt_string host, int64_t port) {
    return rt_viper_tls_connect_impl(host, port, 30000, NULL, NULL, 1);
}

/// @brief Connect to a TLS server with timeout.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @param timeout_ms Timeout in milliseconds.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect_for(rt_string host, int64_t port, int64_t timeout_ms) {
    return rt_viper_tls_connect_impl(host, port, timeout_ms, NULL, NULL, 1);
}

/// @brief Connect to @p host:@p port with explicit verification and ALPN policy.
/// @details Full-feature constructor: caller supplies a CA bundle path (or
///          empty for the system store), an ALPN preference list, the
///          verify-cert flag, and a per-handshake timeout. All four
///          options thread directly into @c rt_tls_connect.
/// @param host TLS hostname (also used for SNI and cert verification).
/// @param port TCP port.
/// @param ca_file Optional PEM bundle path; empty string falls back to the OS store.
/// @param alpn Optional comma-separated ALPN list (e.g. "h2,http/1.1").
/// @param verify_cert 0 to skip chain verification (development only); 1 (default) to enforce.
/// @param timeout_ms Handshake timeout in ms; 0 uses the runtime default (30 s).
/// @return New TLS connection handle, or NULL on failure.
void *rt_viper_tls_connect_options(rt_string host,
                                   int64_t port,
                                   rt_string ca_file,
                                   rt_string alpn,
                                   int8_t verify_cert,
                                   int64_t timeout_ms) {
    return rt_viper_tls_connect_impl(host, port, timeout_ms, ca_file, alpn, verify_cert ? 1 : 0);
}

/// @brief Get the hostname of the TLS connection.
rt_string rt_viper_tls_host(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return rt_string_from_bytes("", 0);
    const char *h = tls->host ? tls->host : "";
    return rt_string_from_bytes(h, strlen(h));
}

/// @brief Get the port of the TLS connection.
int64_t rt_viper_tls_port(void *obj) {
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return 0;
    return tls->port;
}

/// @brief Return the ALPN protocol negotiated on this TLS connection.
/// @details Returns an empty string when no ALPN extension was exchanged
///          or when the peer did not select any advertised protocol.
///          Useful for HTTPS callers that need to know whether the
///          connection is HTTP/2 vs HTTP/1.1.
rt_string rt_viper_tls_negotiated_alpn(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls || !tls->session)
        return rt_string_from_bytes("", 0);
    const char *alpn = rt_tls_get_negotiated_alpn(tls->session);
    return rt_string_from_bytes(alpn ? alpn : "", alpn ? strlen(alpn) : 0);
}

/// @brief Check if the TLS connection is open.
int8_t rt_viper_tls_is_open(void *obj) {
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return 0;
    return tls->session && tls->session->state == TLS_STATE_CONNECTED;
}

/// @brief Send bytes over TLS connection.
/// @param obj TLS object.
/// @param data Bytes object to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send(void *obj, void *data) {
    if (!obj || !data)
        return -1;

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return -1;
    if (!tls->session)
        return -1;

    int64_t len = rt_bytes_len(data);
    if (len < 0)
        return -1;
    if (len == 0)
        return 0;

    // Copy bytes to temporary buffer
    uint8_t *buffer = (uint8_t *)malloc((size_t)len);
    if (!buffer)
        return -1;
    for (int64_t i = 0; i < len; i++)
        buffer[i] = (uint8_t)rt_bytes_get(data, i);

    long result = rt_tls_send(tls->session, buffer, (size_t)len);
    free(buffer);
    return (int64_t)result;
}

/// @brief Send string over TLS connection.
/// @param obj TLS object.
/// @param text String to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send_str(void *obj, rt_string text) {
    if (!obj || !text)
        return -1;

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return -1;
    if (!tls->session)
        return -1;

    const char *cstr = rt_string_cstr(text);
    if (!cstr)
        return 0;
    int64_t len64 = rt_str_len(text);
    if (len64 <= 0)
        return 0;
    size_t len = (size_t)len64;
    if (len == 0)
        return 0;

    long result = rt_tls_send(tls->session, cstr, len);
    return (int64_t)result;
}

/// @brief Receive bytes from TLS connection.
/// @param obj TLS object.
/// @param max_bytes Maximum bytes to receive.
/// @return Bytes object with received data, or NULL on error.
void *rt_viper_tls_recv(void *obj, int64_t max_bytes) {
    if (!obj || max_bytes <= 0)
        return NULL;
    if (max_bytes > TLS_MAX_RECORD_SIZE)
        max_bytes = TLS_MAX_RECORD_SIZE;

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return NULL;
    if (!tls->session)
        return NULL;

    // Allocate temporary buffer
    size_t buf_size = (size_t)max_bytes;
    uint8_t *buffer = (uint8_t *)malloc(buf_size);
    if (!buffer)
        return NULL;

    long received = rt_tls_recv(tls->session, buffer, buf_size);
    if (received <= 0) {
        free(buffer);
        return received == 0 ? rt_bytes_new(0) : NULL;
    }

    // Create Bytes object and copy data
    void *result = rt_bytes_new((int64_t)received);
    for (long i = 0; i < received; i++)
        rt_bytes_set(result, i, buffer[i]);

    free(buffer);
    return result;
}

/// @brief Receive string from TLS connection.
/// @param obj TLS object.
/// @param max_bytes Maximum bytes to receive.
/// @return String with received data, or empty string on error.
rt_string rt_viper_tls_recv_str(void *obj, int64_t max_bytes) {
    if (!obj || max_bytes <= 0)
        return rt_string_from_bytes("", 0);
    if (max_bytes > TLS_MAX_RECORD_SIZE)
        max_bytes = TLS_MAX_RECORD_SIZE;

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return rt_string_from_bytes("", 0);
    if (!tls->session)
        return rt_string_from_bytes("", 0);

    // Allocate temporary buffer
    size_t buf_size = (size_t)max_bytes;
    char *buffer = (char *)malloc(buf_size + 1);
    if (!buffer)
        return rt_string_from_bytes("", 0);

    long received = rt_tls_recv(tls->session, buffer, buf_size);
    if (received <= 0) {
        free(buffer);
        return rt_string_from_bytes("", 0);
    }

    buffer[received] = '\0';
    rt_string result = rt_string_from_bytes(buffer, (size_t)received);
    free(buffer);
    return result;
}

/// @brief Read a line (up to \n) from the TLS connection.
rt_string rt_viper_tls_recv_line(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return rt_string_from_bytes("", 0);
    if (!tls->session)
        return rt_string_from_bytes("", 0);

    size_t cap = 256;
    size_t len = 0;
    int saw_newline = 0;
    char *line = (char *)malloc(cap);
    if (!line)
        return rt_string_from_bytes("", 0);

    while (1) {
        char c;
        long received = rt_tls_recv(tls->session, &c, 1);
        if (received <= 0) {
            break;
        }

        if (c == '\n') {
            // Strip trailing CR if present
            if (len > 0 && line[len - 1] == '\r')
                len--;
            saw_newline = 1;
            break;
        }

        // Cap at 64KB to prevent unbounded memory growth from a malicious peer
        // (matches the limit in rt_tcp_recv_line).
        if (len >= 65536) {
            free(line);
            return rt_string_from_bytes("", 0);
        }

        if (len >= cap) {
            cap *= 2;
            if (cap > 65536)
                cap = 65536;
            char *new_line = (char *)realloc(line, cap);
            if (!new_line) {
                free(line);
                return rt_string_from_bytes("", 0);
            }
            line = new_line;
        }
        line[len++] = c;
    }

    if (!saw_newline) {
        free(line);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes(line, len);
    free(line);
    return result;
}

/// @brief Close the TLS connection.
void rt_viper_tls_close(void *obj) {
    if (!obj)
        return;

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls)
        return;
    if (tls->session) {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
}

/// @brief Get the last error message.
rt_string rt_viper_tls_error(void *obj) {
    const char *msg;
    if (!obj) {
        msg = "null object";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    rt_viper_tls_t *tls = rt_viper_tls_require(obj);
    if (!tls) {
        msg = "invalid object";
        return rt_string_from_bytes(msg, strlen(msg));
    }
    if (!tls->session) {
        msg = "connection closed";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    const char *err = rt_tls_get_error(tls->session);
    msg = err ? err : "no error";
    return rt_string_from_bytes(msg, strlen(msg));
}
