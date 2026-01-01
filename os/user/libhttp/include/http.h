/**
 * @file http.h
 * @brief User-space HTTP client library for ViperOS.
 *
 * Provides HTTP/1.1 and HTTPS client functionality using the TLS library.
 */

#ifndef VIPER_HTTP_H
#define VIPER_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HTTP error codes */
#define HTTP_OK 0
#define HTTP_ERROR -1
#define HTTP_ERROR_CONNECT -2
#define HTTP_ERROR_TLS -3
#define HTTP_ERROR_TIMEOUT -4
#define HTTP_ERROR_PARSE -5
#define HTTP_ERROR_MEMORY -6

/* Maximum sizes */
#define HTTP_MAX_URL 1024
#define HTTP_MAX_HEADER 256
#define HTTP_MAX_HEADERS 32
#define HTTP_MAX_BODY 65536

/* HTTP methods */
typedef enum
{
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD
} http_method_t;

/* HTTP header */
typedef struct
{
    char name[64];
    char value[256];
} http_header_t;

/* HTTP response */
typedef struct http_response
{
    int status_code;
    char status_text[64];
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
    size_t content_length;
    char content_type[128];
    int chunked;
} http_response_t;

/* HTTP request configuration */
typedef struct http_request
{
    http_method_t method;
    const char *url;
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    const void *body;
    size_t body_len;
    int timeout_ms;
    int follow_redirects;
    int max_redirects;
    int verify_tls;
} http_request_t;

/**
 * @brief Initialize an HTTP request with defaults.
 * @param request Request to initialize.
 */
void http_request_init(http_request_t *request);

/**
 * @brief Add a header to an HTTP request.
 * @param request Request to modify.
 * @param name Header name.
 * @param value Header value.
 * @return HTTP_OK on success.
 */
int http_request_add_header(http_request_t *request, const char *name, const char *value);

/**
 * @brief Perform an HTTP GET request.
 * @param url URL to fetch.
 * @param response Output response (caller should free response->body).
 * @return HTTP_OK on success, negative error code on failure.
 */
int http_get(const char *url, http_response_t *response);

/**
 * @brief Perform an HTTP request with full configuration.
 * @param request Request configuration.
 * @param response Output response (caller should free response->body).
 * @return HTTP_OK on success, negative error code on failure.
 */
int http_request(const http_request_t *request, http_response_t *response);

/**
 * @brief Free resources associated with an HTTP response.
 * @param response Response to free.
 */
void http_response_free(http_response_t *response);

/**
 * @brief Get a response header value.
 * @param response Response to search.
 * @param name Header name (case-insensitive).
 * @return Header value or NULL if not found.
 */
const char *http_response_get_header(const http_response_t *response, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_HTTP_H */
