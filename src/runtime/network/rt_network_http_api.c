//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_network_http_api.c
// Purpose: Viper-facing HTTP classes — the Http static class plus the HttpReq
//          and HttpRes instance classes. Split out of rt_network_http.c; these
//          wrappers build rt_http_req_t requests and drive the HTTP client core
//          (do_http_request etc.) declared in rt_network_http_internal.h.
//
// Key invariants:
//   - Request/response data structures and the client core live in
//     rt_network_http.c; this layer only marshals Viper types in and out.
//   - Header/body builders validate method tokens and reject embedded NULs.
//
// Ownership/Lifetime:
//   - HttpReq/HttpRes are GC objects; their backing rt_http_req_t/rt_http_res_t
//     storage is freed via the client core's free_* helpers.
//
// Links: src/runtime/network/rt_network_http.c (HTTP client core + structs),
//        src/runtime/network/rt_network_http_internal.h (shared types/decls)
//
//===----------------------------------------------------------------------===//

// rt_network_internal.h requires these feature-test macros before inclusion.
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1

#include "rt_network_http_internal.h"
#include "rt_network_internal.h"

#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_entropy_platform.h"
#include "rt_error.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_network.h"
#include "rt_platform.h"
#include "rt_result.h"
#include "rt_trap.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if RT_PLATFORM_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

static size_t g_http_download_temp_counter = 0;

/// @brief Release a temporary object after a container has retained it.
/// @details `Result.Ok` retains the response payload; this helper drops the
///          local ownership reference returned by `rt_http_req_send`.
/// @param obj Runtime object to release, or NULL.
static void http_release_temp_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Return the current trap message as a Result-compatible string.
/// @details The returned value is a runtime constant string and does not
///          transfer ownership to the caller.
/// @param fallback Message used when no trap text is active.
/// @return Runtime string for use with `rt_result_err_str`.
static rt_string http_current_error_message(const char *fallback) {
    const char *err = rt_trap_get_error();
    if (!err || !err[0])
        err = fallback && fallback[0] ? fallback : "HTTP request failed";
    return rt_const_cstr(err);
}

/// @brief Wrap an HTTP response in `Result.Ok` or produce `Result.ErrStr`.
/// @details A NULL response indicates a transport/setup failure rather than an
///          HTTP status; successful HTTP responses of any status code are Ok.
/// @param response HttpRes object returned by the transport.
/// @param null_message Error text used when @p response is NULL.
/// @return Opaque `Viper.Result`.
static void *http_response_result(void *response, const char *null_message) {
    if (!response)
        return rt_result_err_str(
            rt_const_cstr(null_message ? null_message : "HTTP request failed"));
    void *result = rt_result_ok(response);
    http_release_temp_object(response);
    return result;
}

/// @brief Build a sibling temporary path for an HTTP download target.
/// @details The returned path appends a hidden `.viper-download-N.tmp` suffix to
///          the final filename, keeping the temp on the same filesystem so
///          rename-based replacement can be used after a successful transfer.
/// @param dest Final destination path.
/// @param kind Suffix kind such as `"tmp"` or `"bak"`.
/// @return Heap-allocated temp path, or NULL on allocation overflow/OOM.
static char *http_download_temp_path(const char *dest, const char *kind) {
    if (!dest || !kind)
        return NULL;
    size_t id = __atomic_fetch_add(&g_http_download_temp_counter, 1u, __ATOMIC_RELAXED) + 1u;
    uint64_t random_id = 0;
    if (rt_entropy_platform_random_u64(&random_id) != 0)
        return NULL;
    size_t dest_len = strlen(dest);
    size_t kind_len = strlen(kind);
    const char *marker = ".viper-download-";
    size_t marker_len = strlen(marker);
    if (dest_len > SIZE_MAX - marker_len - kind_len - 64u)
        return NULL;
    size_t cap = dest_len + marker_len + kind_len + 64u;
    char *path = (char *)malloc(cap);
    if (!path)
        return NULL;
    snprintf(
        path, cap, "%s%s%016llx-%zu.%s", dest, marker, (unsigned long long)random_id, id, kind);
    return path;
}

/// @brief Open a path for binary writing with exclusive creation.
/// @details Uses file-descriptor APIs instead of C `fopen("xb")` because that
///          mode is not accepted by every supported C library. The returned
///          stream owns the descriptor and closes it through `fclose`.
/// @param path Filesystem path to create.
/// @return Writable binary stream, or NULL when the file exists or creation fails.
static FILE *http_download_fopen_exclusive(const char *path) {
#if RT_PLATFORM_WINDOWS
    int fd = _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
    if (fd < 0)
        return NULL;
#if RT_PLATFORM_WINDOWS
    FILE *f = _fdopen(fd, "wb");
#else
    FILE *f = fdopen(fd, "wb");
#endif
    if (!f) {
#if RT_PLATFORM_WINDOWS
        _close(fd);
#else
        close(fd);
#endif
        return NULL;
    }
    return f;
}

/// @brief Open a same-directory HTTP download temp file with exclusive creation.
/// @details Retries random sibling names so a pre-existing file or symlink cannot
///          be followed by `fopen("wb")`. The file remains beside the final
///          destination so the completed download can be installed with rename.
/// @param dest Final destination path.
/// @param temp_path_out Receives the allocated temp path on success.
/// @return Writable FILE on success; NULL on allocation, entropy, or create failure.
static FILE *http_download_open_unique_temp(const char *dest, char **temp_path_out) {
    if (temp_path_out)
        *temp_path_out = NULL;
    if (!dest || !temp_path_out)
        return NULL;
    for (int attempt = 0; attempt < 128; attempt++) {
        char *candidate = http_download_temp_path(dest, "tmp");
        if (!candidate)
            return NULL;
        FILE *f = http_download_fopen_exclusive(candidate);
        if (f) {
            *temp_path_out = candidate;
            return f;
        }
        int saved_errno = errno;
        free(candidate);
        if (saved_errno != EEXIST)
            return NULL;
    }
    return NULL;
}

/// @brief Replace a download destination with a completed temp file.
/// @details Existing destination files are first renamed to a backup. If the
///          final rename fails, the backup is restored where possible. This
///          avoids leaving a partial response at the destination path.
/// @param temp_path Fully written temporary file.
/// @param dest_path Final destination path.
/// @param backup_path Sibling backup path.
/// @return 1 on successful replacement; 0 on rename/restore failure.
static int http_download_replace_file(const char *temp_path,
                                      const char *dest_path,
                                      const char *backup_path) {
    int had_backup = 0;
    if (rename(dest_path, backup_path) == 0) {
        had_backup = 1;
    } else if (errno != ENOENT) {
        return 0;
    }
    if (rename(temp_path, dest_path) != 0) {
        if (had_backup)
            (void)rename(backup_path, dest_path);
        return 0;
    }
    if (had_backup)
        (void)remove(backup_path);
    return 1;
}

/// @brief Copy an HTTP response body into a fresh Bytes object.
/// @details Centralizes the allocation check used by all `Http.*Bytes`
///          convenience APIs. Returning NULL after trapping prevents a
///          recoverable allocation failure from being followed by
///          `bytes_data(NULL)` and a secondary crash.
/// @param body Borrowed response body bytes; may be NULL when `body_len` is 0.
/// @param body_len Number of bytes to copy.
/// @param context Trap message for allocation failure.
/// @return Owned Bytes object, or NULL if allocation failed.
static void *http_copy_body_to_bytes(const uint8_t *body, size_t body_len, const char *context) {
    if (body_len > (size_t)INT64_MAX) {
        rt_trap(context ? context : "HTTP: response body too large");
        return NULL;
    }
    void *result = rt_bytes_new((int64_t)body_len);
    if (!result) {
        rt_trap(context ? context : "HTTP: memory allocation failed");
        return NULL;
    }
    uint8_t *result_ptr = bytes_data(result);
    if (body && body_len > 0 && result_ptr)
        memcpy(result_ptr, body, body_len);
    return result;
}

/// @brief GC finalizer for an HttpReq object. Safe on a partially-built request
///        because every helper either nulls or initialises its target.
static void rt_http_req_finalize(void *obj) {
    if (!obj)
        return;
    rt_http_req_t *req = (rt_http_req_t *)obj;
    free(req->method);
    req->method = NULL;
    free_parsed_url(&req->url);
    free_headers(req->headers);
    req->headers = NULL;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
    if (req->connection_pool && rt_obj_release_check0(req->connection_pool))
        rt_obj_free(req->connection_pool);
    req->connection_pool = NULL;
    req->timeout_ms = 0;
}

/// @brief Duplicate an HTTP method token into request-owned storage.
/// @details The one-shot and builder APIs store `method` in `rt_http_req_t` and later free it
///          through normal request cleanup. This helper centralizes the allocation check so an
///          out-of-memory trap cannot be followed by a request with a NULL method when a test or
///          embedding trap hook returns.
/// @param method Non-NULL HTTP method token to duplicate.
/// @return Heap-allocated method string, or NULL only if the active trap handler returned after
///         reporting allocation failure.
static char *http_dup_method_or_trap(const char *method) {
    char *copy = method ? strdup(method) : NULL;
    if (!copy) {
        rt_trap("HTTP: method allocation failed");
        return NULL;
    }
    return copy;
}

//=============================================================================
// Http Static Class Implementation
//=============================================================================
//
// These one-shot helpers (rt_http_get, rt_http_post, …) build an
// `rt_http_req_t` on the stack, run a single transaction with the
// default timeout (`HTTP_DEFAULT_TIMEOUT_MS`) and redirect cap
// (`HTTP_MAX_REDIRECTS`), then return either the body as a string,
// the body as a Bytes object, or in the case of HEAD, just the
// response object. They `rt_trap_net` on malformed URLs and
// transport failures so callers don't need to error-check.
//=============================================================================

/// @brief HTTP GET that returns the body decoded as a UTF-8 string.
/// @throws Err_InvalidUrl on empty / unparsable URL,
///         Err_NetworkError on transport failure.
rt_string rt_http_get(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    // Create request
    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("GET");
    if (!req.method)
        return rt_string_from_bytes("", 0);
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    // Execute request
    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP GET that returns the raw response body as a Bytes object.
/// @throws Err_InvalidUrl / Err_NetworkError on failure (see `rt_http_get`).
void *rt_http_get_bytes(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("GET");
    if (!req.method)
        return NULL;
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return NULL;
    }

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return NULL;
    }

    void *result =
        http_copy_body_to_bytes(res->body, res->body_len, "HTTP: response body allocation failed");

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP POST with a string body; returns the response body as a string.
///
/// Adds `Content-Type: text/plain; charset=utf-8` automatically
/// when the body is non-empty. The body is copied into a request-
/// owned buffer so the caller's `rt_string` lifetime is independent.
rt_string rt_http_post(rt_string url, rt_string body) {
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("POST");
    if (!req.method)
        return rt_string_from_bytes("", 0);
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    if (body_str)
        set_request_body_from_string(&req, body);

    // Add Content-Type if not empty body
    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free(req.body);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP POST with a Bytes body; returns the response body as Bytes.
///
/// Adds `Content-Type: application/octet-stream` automatically when
/// the body is non-empty. The Bytes object is referenced directly
/// (not copied), so it must remain alive for the duration of the call.
void *rt_http_post_bytes(rt_string url, void *body) {
    const char *url_str = rt_string_cstr(url);

    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("POST");
    if (!req.method)
        return NULL;
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return NULL;
    }

    if (body) {
        int64_t body_len = rt_bytes_len(body);
        uint8_t *body_ptr = bytes_data(body);
        req.body = body_ptr;
        req.body_len = (size_t)body_len;
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "application/octet-stream");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return NULL;
    }

    void *result =
        http_copy_body_to_bytes(res->body, res->body_len, "HTTP: response body allocation failed");

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP GET that streams the response body to a file at `dest_path`.
///
/// Returns 0 (false) on any failure: bad URL, transport error,
/// non-2xx status, file-open failure, short write, or `fclose`
/// error. The response is written to a same-directory temp file and only
/// renamed over the destination after the transfer and close both succeed.
/// Does not trap, so callers can branch on the boolean instead of installing
/// an exception handler.
/// @return 1 on full successful download, 0 otherwise.
int8_t rt_http_download(rt_string url, rt_string dest_path) {
    const char *url_str = rt_string_cstr(url);
    const char *path_str = rt_string_cstr(dest_path);

    if (!url_str || *url_str == '\0')
        return 0;
    if (!path_str || *path_str == '\0')
        return 0;

    rt_http_req_t req = {0};
    req.method = strdup("GET");
    if (!req.method)
        return 0;
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 0;
    req.decode_gzip = 0;
    req.force_http1 = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        return 0;
    }

    char *temp_path = NULL;
    char *backup_path = http_download_temp_path(path_str, "bak");
    FILE *f = NULL;
    if (!backup_path) {
        free(backup_path);
        free(req.method);
        free_parsed_url(&req.url);
        return 0;
    }

    f = http_download_open_unique_temp(path_str, &temp_path);
    if (!f) {
        free(temp_path);
        free(backup_path);
        free(req.method);
        free_parsed_url(&req.url);
        return 0;
    }

    int ok = do_http_download_request(&req, HTTP_MAX_REDIRECTS, f);
    free(req.method);
    free_parsed_url(&req.url);
    int close_err = fclose(f);

    // RC-14: if fwrite wrote fewer bytes (disk full, etc.) or fclose failed
    // (buffered data flush failure), remove the partial/corrupt file.
    if (!ok || close_err != 0) {
        remove(temp_path);
        free(temp_path);
        free(backup_path);
        return 0;
    }
    if (!http_download_replace_file(temp_path, path_str, backup_path)) {
        remove(temp_path);
        free(temp_path);
        free(backup_path);
        return 0;
    }
    free(temp_path);
    free(backup_path);
    return 1;
}

/// @brief HTTP HEAD request; returns just the response headers map.
///
/// Useful for size/type probes (Content-Length, Content-Type) and
/// existence checks without paying for the body. Throws on
/// transport failure.
void *rt_http_head(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("HEAD");
    if (!req.method)
        return NULL;
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return NULL;
    }

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return NULL;
    }

    void *headers = rt_http_res_headers(res);
    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return headers;
}

/// @brief HTTP PATCH with a string body; returns the response body as a string.
///
/// Mirrors `rt_http_post` but sends the `PATCH` method
/// (RFC 5789), used to apply partial updates to a resource.
rt_string rt_http_patch(rt_string url, rt_string body) {
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("PATCH");
    if (!req.method)
        return rt_string_from_bytes("", 0);
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    if (body_str)
        set_request_body_from_string(&req, body);

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free(req.body);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP OPTIONS request; returns the response body as a string.
///
/// Typically used for CORS preflight discovery — the meaningful
/// data lives in the response headers (`Allow`, `Access-Control-*`).
rt_string rt_http_options(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("OPTIONS");
    if (!req.method)
        return rt_string_from_bytes("", 0);
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP PUT with a string body; returns the response body as a string.
///
/// PUT replaces the target resource (RFC 7231 §4.3.4). Body is
/// copied and tagged `Content-Type: text/plain; charset=utf-8`.
rt_string rt_http_put(rt_string url, rt_string body) {
    const char *url_str = rt_string_cstr(url);
    const char *body_str = rt_string_cstr(body);

    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("PUT");
    if (!req.method)
        return rt_string_from_bytes("", 0);
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    if (body_str)
        set_request_body_from_string(&req, body);

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "text/plain; charset=utf-8");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free(req.body);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP PUT with a Bytes body; returns the response body as Bytes.
///
/// As `rt_http_put` but for binary payloads — sets
/// `Content-Type: application/octet-stream` when the body is non-empty.
void *rt_http_put_bytes(rt_string url, void *body) {
    const char *url_str = rt_string_cstr(url);

    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("PUT");
    if (!req.method)
        return NULL;
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return NULL;
    }

    if (body) {
        int64_t body_len = rt_bytes_len(body);
        uint8_t *body_ptr = bytes_data(body);
        req.body = body_ptr;
        req.body_len = (size_t)body_len;
    }

    if (req.body_len > 0)
        add_header(&req, "Content-Type", "application/octet-stream");

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);
    free_headers(req.headers);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return NULL;
    }

    void *result =
        http_copy_body_to_bytes(res->body, res->body_len, "HTTP: response body allocation failed");

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP DELETE; returns the response body as a string.
rt_string rt_http_delete(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("DELETE");
    if (!req.method)
        return rt_string_from_bytes("", 0);
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return rt_string_from_bytes("", 0);
    }

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes((const char *)res->body, res->body_len);

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

/// @brief HTTP DELETE; returns the response body as a Bytes object.
void *rt_http_delete_bytes(rt_string url) {
    const char *url_str = rt_string_cstr(url);
    if (!url_str || *url_str == '\0') {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    rt_http_req_t req = {0};
    req.method = http_dup_method_or_trap("DELETE");
    if (!req.method)
        return NULL;
    req.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req.tls_verify = 1;
    req.follow_redirects = 1;
    req.accept_gzip = 1;
    req.decode_gzip = 1;

    if (parse_url(url_str, &req.url) < 0) {
        free(req.method);
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return NULL;
    }

    rt_http_res_t *res = do_http_request(&req, HTTP_MAX_REDIRECTS);
    free(req.method);
    free_parsed_url(&req.url);

    if (!res) {
        rt_trap_net("HTTP: request failed", Err_NetworkError);
        return NULL;
    }

    void *result =
        http_copy_body_to_bytes(res->body, res->body_len, "HTTP: response body allocation failed");

    if (rt_obj_release_check0(res))
        rt_obj_free(res);

    return result;
}

//=============================================================================
// HttpReq Instance Class Implementation
//
// The builder-style HttpReq class lets callers configure a request
// (headers, body, timeout, redirect policy) before sending. Setters
// return `obj` so they can be chained. All instances are GC-managed
// and finalized via `rt_http_req_finalize`.
//=============================================================================

/// @brief Construct a new HTTP request with the given method and URL.
///
/// The URL is parsed eagerly so callers see invalid-URL traps at
/// construction time rather than when sending. Defaults: 30s
/// timeout, follow redirects with cap `HTTP_MAX_REDIRECTS`.
/// @throws Err_InvalidUrl on bad URL, generic trap on null method.
void *rt_http_req_new(rt_string method, rt_string url) {
    const char *method_str = method ? rt_string_cstr(method) : NULL;
    const char *url_str = url ? rt_string_cstr(url) : NULL;

    if (!method_str || http_rt_string_has_embedded_nul(method) ||
        !http_method_is_token(method_str)) {
        rt_trap("HTTP: invalid method");
        return NULL;
    }
    if (!url_str || *url_str == '\0' || http_rt_string_has_embedded_nul(url)) {
        rt_trap_net("HTTP: invalid URL", Err_InvalidUrl);
        return NULL;
    }

    // Must use rt_obj_new_i64 for GC management
    rt_http_req_t *req = (rt_http_req_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_http_req_t));
    if (!req) {
        rt_trap("HTTP: memory allocation failed");
        return NULL;
    }

    memset(req, 0, sizeof(*req));
    rt_obj_set_finalizer(req, rt_http_req_finalize);
    req->method = http_dup_method_or_trap(method_str);
    if (!req->method)
        return NULL;
    req->timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    req->tls_verify = 1;
    req->follow_redirects = 1;
    req->max_redirects = HTTP_MAX_REDIRECTS;
    req->accept_gzip = 1;
    req->decode_gzip = 1;
    req->keep_alive = 0;
    req->connection_pool = NULL;

    if (parse_url(url_str, &req->url) < 0) {
        free(req->method);
        req->method = NULL;
        // Note: GC-managed object, so we don't free it directly
        rt_trap_net("HTTP: invalid URL format", Err_InvalidUrl);
        return NULL;
    }

    return req;
}

/// @brief Append a request header. Silently ignores NULL name/value.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_header(void *obj, rt_string name, rt_string value) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *name_str = name ? rt_string_cstr(name) : NULL;
    const char *value_str = value ? rt_string_cstr(value) : NULL;

    if (!name_str || !value_str)
        return obj;
    if (http_rt_string_has_embedded_nul(name) || http_rt_string_has_embedded_nul(value) ||
        !http_method_is_token(name_str)) {
        rt_trap("HTTP: invalid header");
        return obj;
    }
    for (const char *p = value_str; *p; ++p) {
        if (*p == '\r' || *p == '\n') {
            rt_trap("HTTP: invalid header");
            return obj;
        }
    }
    add_header(req, name_str, value_str);

    return obj;
}

/// @brief Replace the request body with a Bytes object (copied).
///
/// Frees any previously-set body. The bytes are duplicated into a
/// request-owned buffer so the caller's Bytes lifetime is independent.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_body(void *obj, void *data) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;

    if (req->body) {
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    if (data) {
        int64_t len = rt_bytes_len(data);
        uint8_t *ptr = bytes_data(data);
        if (len < 0 || (uint64_t)len > (uint64_t)SIZE_MAX) {
            rt_trap("HTTP: invalid body length");
            return obj;
        }

        if (len > 0) {
            req->body = (uint8_t *)malloc((size_t)len);
            if (!req->body) {
                rt_trap("HTTP: memory allocation failed");
                return obj;
            }
            memcpy(req->body, ptr, (size_t)len);
            req->body_len = (size_t)len;
        }
    }

    return obj;
}

/// @brief Replace the request body with a string (copied).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_body_str(void *obj, rt_string text) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    const char *text_str = text ? rt_string_cstr(text) : NULL;

    if (req->body) {
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    if (text_str) {
        int64_t len64 = rt_str_len(text);
        if (len64 < 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX) {
            rt_trap("HTTP: invalid body length");
            return obj;
        }
        size_t len = (size_t)len64;
        if (len > 0) {
            req->body = (uint8_t *)malloc(len);
            if (!req->body) {
                rt_trap("HTTP: memory allocation failed");
                return obj;
            }
            memcpy(req->body, text_str, len);
            req->body_len = len;
        }
    }

    return obj;
}

/// @brief Set per-request I/O timeout in milliseconds.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_timeout(void *obj, int64_t timeout_ms) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    int timeout_int = 0;
    if (!rt_net_timeout_ms_to_int(timeout_ms, &timeout_int)) {
        rt_trap("HTTP: invalid timeout");
        return obj;
    }
    req->timeout_ms = timeout_int;

    return obj;
}

/// @brief Toggle TLS certificate verification for HTTPS requests.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_tls_verify(void *obj, int8_t verify) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->tls_verify = verify ? 1 : 0;
    return obj;
}

/// @brief Disable TLS certificate verification for explicitly local/test HTTP requests.
/// @details This wrapper is equivalent to `rt_http_req_set_tls_verify(obj, 0)`, but
///          keeps insecure certificate handling visible in source and runtime API
///          dumps. Do not use it for production HTTPS clients.
/// @return `obj` (for fluent chaining).
void *rt_http_req_allow_insecure_certificates_for_testing(void *obj) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->tls_verify = 0;
    return obj;
}

/// @brief Toggle automatic redirect following (3xx Location handling).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_follow_redirects(void *obj, int8_t follow) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->follow_redirects = follow ? 1 : 0;
    return obj;
}

/// @brief Override the per-request redirect cap (negative values clamp to 0).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_max_redirects(void *obj, int64_t max_redirects) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    if (max_redirects < 0)
        max_redirects = 0;
    if (max_redirects > INT_MAX) {
        rt_trap("HTTP: invalid redirect limit");
        return obj;
    }
    req->max_redirects = (int)max_redirects;
    return obj;
}

/// @brief Toggle keep-alive / pooled transport for this request.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_keep_alive(void *obj, int8_t keep_alive) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->keep_alive = keep_alive ? 1 : 0;
    return obj;
}

/// @brief Restrict this request to HTTP/1.1 transport even over TLS.
/// @details When set, the TLS client advertises only `http/1.1` via
///          ALPN instead of the default `h2,http/1.1`, forcing the
///          server to select HTTP/1.1. Useful for code that depends
///          on HTTP/1.1 framing semantics (e.g. the `Connection:
///          keep-alive` response header, which HTTP/2 omits because
///          persistence is implicit). Default is `0` (allow HTTP/2).
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_force_http1(void *obj, int8_t force) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    req->force_http1 = force ? 1 : 0;
    return obj;
}

/// @brief Attach an internal connection pool to this request.
/// @return `obj` (for fluent chaining).
void *rt_http_req_set_connection_pool(void *obj, void *pool) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;
    if (req->connection_pool == pool)
        return obj;
    if (pool)
        rt_obj_retain_maybe(pool);
    if (req->connection_pool && rt_obj_release_check0(req->connection_pool))
        rt_obj_free(req->connection_pool);
    req->connection_pool = pool;
    return obj;
}

/// @brief Execute a configured HttpReq and return the resulting HttpRes.
///
/// Auto-tags a non-empty body with
/// `Content-Type: application/octet-stream` if the caller didn't
/// already set one. Honours the request's redirect cap (which may
/// be 0 to disable following).
/// @return Newly-allocated `rt_http_res_t*` (GC-managed) or NULL on
///         transport failure (`do_http_request` returns NULL).
void *rt_http_req_send(void *obj) {
    if (!obj) {
        rt_trap("HTTP: NULL request");
        return NULL;
    }

    rt_http_req_t *req = (rt_http_req_t *)obj;

    // Add Content-Type for POST with body if not set
    if (req->body && req->body_len > 0 && !has_header(req, "Content-Type")) {
        add_header(req, "Content-Type", "application/octet-stream");
    }

    rt_http_res_t *res = do_http_request(req, req->max_redirects);
    return res;
}

/// @brief Execute a configured HttpReq and return `Result<HttpRes>`.
/// @details This is the production-friendly companion to `rt_http_req_send`:
///          transport/setup traps and NULL transport results are converted to
///          `Result.ErrStr`, while received HTTP responses are returned as
///          `Result.Ok(HttpRes)` regardless of status code.
void *rt_http_req_send_result(void *obj) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        rt_string message = http_current_error_message("HTTP request failed");
        rt_trap_clear_recovery();
        return rt_result_err_str(message);
    }
    void *response = rt_http_req_send(obj);
    rt_trap_clear_recovery();
    return http_response_result(response, "HTTP request failed");
}

//=============================================================================
// HttpRes Instance Class Implementation
//
// Read-only accessors over the response. Header lookups are
// case-insensitive: the parser stores keys lower-cased so the
// lookup helper just down-cases the request name.
//=============================================================================

/// @brief Status code (e.g. 200, 404). Returns 0 if `obj` is NULL.
int64_t rt_http_res_status(void *obj) {
    if (!obj)
        return 0;
    return ((rt_http_res_t *)obj)->status;
}

/// @brief Reason phrase from the status line (e.g. "Not Found"). Empty if unset.
rt_string rt_http_res_status_text(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->status_text)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(res->status_text, strlen(res->status_text));
}

/// @brief Return a *copy* of the response headers as a fresh Map(String→String).
///
/// Copying defends the response's internal map against caller
/// mutation. Header keys are lower-case (already canonicalised by
/// the parser). Empty map is returned if `obj` is NULL or the
/// response has no headers.
void *rt_http_res_headers(void *obj) {
    if (!obj)
        return rt_map_new();

    rt_http_res_t *res = (rt_http_res_t *)obj;
    void *copy = rt_map_new();
    if (!res->headers || !copy)
        return copy ? copy : rt_map_new();

    void *keys = rt_map_keys(res->headers);
    int64_t count = rt_seq_len(keys);
    for (int64_t i = 0; i < count; i++) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *boxed = rt_map_get(res->headers, key);
        if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
            continue;
        rt_string value = rt_unbox_str(boxed);
        rt_map_set(copy, key, value);
        rt_string_unref(value);
    }
    if (keys && rt_obj_release_check0(keys))
        rt_obj_free(keys);
    return copy;
}

/// @brief Return a copy of the raw response body as a Bytes object.
void *rt_http_res_body(void *obj) {
    if (!obj)
        return rt_bytes_new(0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    return http_copy_body_to_bytes(
        res->body, res->body_len, "HTTP: response body allocation failed");
}

/// @brief Return the response body decoded as a UTF-8 string.
///
/// No charset detection — bytes are passed through as if UTF-8.
/// Empty string for null/empty bodies.
rt_string rt_http_res_body_str(void *obj) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;
    if (!res->body || res->body_len == 0)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes((const char *)res->body, res->body_len);
}

/// @brief Look up a single response header by name (case-insensitive).
///
/// Down-cases `name` and probes the parser-canonicalised header
/// map. Returns an empty string on miss or null arguments.
rt_string rt_http_res_header(void *obj, rt_string name) {
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_http_res_t *res = (rt_http_res_t *)obj;

    // Convert name to lowercase for lookup
    const char *name_str = name ? rt_string_cstr(name) : NULL;
    if (!name_str || http_rt_string_has_embedded_nul(name))
        return rt_string_from_bytes("", 0);

    size_t len = strlen(name_str);
    char *lower_name = (char *)malloc(len + 1);
    if (!lower_name)
        return rt_string_from_bytes("", 0);

    for (size_t i = 0; i <= len; i++) {
        char c = name_str[i];
        if (c >= 'A' && c <= 'Z')
            lower_name[i] = c + ('a' - 'A');
        else
            lower_name[i] = c;
    }

    rt_string lower_key = rt_string_from_bytes(lower_name, len);
    free(lower_name);

    void *boxed = rt_map_get(res->headers, lower_key);
    rt_string_unref(lower_key);
    if (!boxed || rt_box_type(boxed) != RT_BOX_STR)
        return rt_string_from_bytes("", 0);

    rt_string value = rt_unbox_str(boxed);
    const char *value_cstr = value ? rt_string_cstr(value) : NULL;
    int64_t value_len64 = value ? rt_str_len(value) : 0;
    if (value_len64 < 0 || (uint64_t)value_len64 > (uint64_t)SIZE_MAX)
        value_len64 = 0;
    rt_string copy = rt_string_from_bytes(
        value_cstr ? value_cstr : "", (value_cstr && value_len64 > 0) ? (size_t)value_len64 : 0);
    rt_string_unref(value);
    return copy;
}

/// @brief Convenience predicate: 2xx success status?
/// @return 1 if `200 <= status < 300`, 0 otherwise (and on NULL).
int8_t rt_http_res_is_ok(void *obj) {
    if (!obj)
        return 0;

    int status = ((rt_http_res_t *)obj)->status;
    return (status >= 200 && status < 300) ? 1 : 0;
}
