#pragma once

// Shared request/response helpers used by both the plain HTTP and TLS-backed
// HTTP servers. This include is pulled in after the local server_req_t /
// server_res_t / chunk_parse_status_t definitions in each translation unit.

/// @brief Locate the next `\r\n` line terminator within a length-bounded
///        buffer.
static const char *find_crlf(const char *buf, size_t len) {
    if (len < 2)
        return NULL;
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
            return buf + i;
    }
    return NULL;
}

/// @brief Locate the end of an HTTP request header section (`\r\n\r\n`).
static const char *find_header_end(const char *buf, size_t len) {
    if (len < 4)
        return NULL;
    for (size_t i = 3; i < len; i++) {
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n')
            return buf + i + 1;
    }
    return NULL;
}

/// @brief `memchr`-equivalent for length-bounded buffers.
static const char *find_char_bounded(const char *buf, size_t len, char needle) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == needle)
            return buf + i;
    }
    return NULL;
}

/// @brief Detect any CR/LF byte in a NUL-terminated string.
static int contains_crlf(const char *text) {
    if (!text)
        return 0;
    for (; *text; text++) {
        if (*text == '\r' || *text == '\n')
            return 1;
    }
    return 0;
}

static int http_token_char(unsigned char c) {
    if (c <= 0x20 || c == 0x7f)
        return 0;
    switch (c) {
        case '(':
        case ')':
        case '<':
        case '>':
        case '@':
        case ',':
        case ';':
        case ':':
        case '\\':
        case '"':
        case '/':
        case '[':
        case ']':
        case '?':
        case '=':
        case '{':
        case '}':
            return 0;
        default:
            return 1;
    }
}

static bool http_method_is_valid(const char *method, size_t method_len) {
    if (!method || method_len == 0)
        return false;
    for (size_t i = 0; i < method_len; i++) {
        if (!http_token_char((unsigned char)method[i]))
            return false;
    }
    return true;
}

static bool http_request_target_bytes_are_valid(const char *target, size_t target_len) {
    if (!target || target_len == 0)
        return false;
    for (size_t i = 0; i < target_len; i++) {
        unsigned char c = (unsigned char)target[i];
        if (c <= 0x20 || c == 0x7f || c == '#')
            return false;
    }
    return true;
}

static bool set_request_target(server_req_t *req, const char *uri, size_t uri_len) {
    const char *target = uri;
    size_t target_len = uri_len;
    const char *qmark = NULL;
    size_t path_len = 0;

    if (!req || !http_request_target_bytes_are_valid(uri, uri_len))
        return false;

    if (uri_len == 1 && uri[0] == '*') {
        req->path = strdup("*");
        return req->path != NULL;
    }

    if (uri[0] == '/') {
        target = uri;
        target_len = uri_len;
    } else if ((uri_len >= 7 && strncasecmp(uri, "http://", 7) == 0) ||
               (uri_len >= 8 && strncasecmp(uri, "https://", 8) == 0)) {
        const char *authority = uri + (strncasecmp(uri, "https://", 8) == 0 ? 8 : 7);
        const char *end = uri + uri_len;
        const char *slash = find_char_bounded(authority, (size_t)(end - authority), '/');
        const char *query = find_char_bounded(authority, (size_t)(end - authority), '?');
        const char *host_end = slash ? slash : (query ? query : end);
        if (host_end == authority)
            return false;
        if (slash) {
            target = slash;
            target_len = (size_t)(end - slash);
        } else if (query) {
            target = query;
            target_len = (size_t)(end - query);
        } else {
            req->path = strdup("/");
            return req->path != NULL;
        }
    } else {
        return false;
    }

    qmark = find_char_bounded(target, target_len, '?');
    if (qmark) {
        path_len = (size_t)(qmark - target);
        req->query = (char *)malloc(target_len - path_len);
        if (!req->query)
            return false;
        memcpy(req->query, qmark + 1, target_len - path_len - 1);
        req->query[target_len - path_len - 1] = '\0';
    } else {
        path_len = target_len;
    }

    if (path_len == 0) {
        if (target[0] != '?')
            return false;
        req->path = strdup("/");
        return req->path != NULL;
    }

    req->path = (char *)malloc(path_len + 1);
    if (!req->path)
        return false;
    memcpy(req->path, target, path_len);
    req->path[path_len] = '\0';
    return true;
}

/// @brief Test whether a header name is server-managed.
static int is_server_managed_header_name(const char *name) {
    return name &&
           (strcasecmp(name, "Content-Length") == 0 || strcasecmp(name, "Connection") == 0 ||
            strcasecmp(name, "Transfer-Encoding") == 0);
}

/// @brief Parse a decimal `size_t` from a length-bounded substring.
static bool parse_size_decimal(const char *text, size_t len, size_t *out) {
    size_t value = 0;
    size_t i = 0;

    while (i < len && (text[i] == ' ' || text[i] == '\t'))
        i++;
    if (i == len)
        return false;

    for (; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= '0' && c <= '9') {
            size_t digit = (size_t)(c - '0');
            if (value > (SIZE_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
            continue;
        }
        if (c == ' ' || c == '\t') {
            while (i < len && (text[i] == ' ' || text[i] == '\t'))
                i++;
            if (i != len)
                return false;
            *out = value;
            return true;
        }
        return false;
    }

    *out = value;
    return true;
}

/// @brief Return true if a comma-separated header value contains @p token.
static bool header_value_contains_token(const char *value,
                                        size_t value_len,
                                        const char *token,
                                        size_t token_len) {
    const char *p = value;
    const char *end = value + value_len;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == ','))
            p++;
        const char *token_start = p;
        while (p < end && *p != ',' && *p != ';')
            p++;
        const char *token_end = p;
        while (token_end > token_start && (token_end[-1] == ' ' || token_end[-1] == '\t'))
            token_end--;
        if ((size_t)(token_end - token_start) == token_len &&
            strncasecmp(token_start, token, token_len) == 0) {
            return true;
        }
        while (p < end && *p != ',')
            p++;
    }

    return false;
}

/// @brief Parse an HTTP chunk-size line (hex digits with optional extensions).
static bool parse_chunk_size_line(const char *line, size_t len, size_t *chunk_size_out) {
    size_t value = 0;
    size_t i = 0;
    bool saw_digit = false;

    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    for (; i < len; i++) {
        unsigned char c = (unsigned char)line[i];
        unsigned digit = 0;
        if (c == ';')
            break;
        if (c >= '0' && c <= '9')
            digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (unsigned)(c - 'A' + 10);
        else if (c == ' ' || c == '\t') {
            while (i < len && (line[i] == ' ' || line[i] == '\t'))
                i++;
            if (i < len && line[i] != ';')
                return false;
            break;
        } else {
            return false;
        }

        if (value > (SIZE_MAX - digit) / 16)
            return false;
        value = value * 16 + digit;
        saw_digit = true;
    }

    if (!saw_digit || value > HTTP_REQ_MAX_BODY)
        return false;
    *chunk_size_out = value;
    return true;
}

/// @brief Decode a chunked request body.
static chunk_parse_status_t decode_chunked_body(const char *encoded,
                                                size_t encoded_len,
                                                char **decoded_out,
                                                size_t *decoded_len_out,
                                                size_t *consumed_len_out) {
    size_t pos = 0;
    size_t decoded_len = 0;
    size_t decoded_cap = 0;
    char *decoded = NULL;

    while (1) {
        const char *line_end = find_crlf(encoded + pos, encoded_len - pos);
        size_t chunk_size = 0;
        if (!line_end) {
            free(decoded);
            return CHUNK_PARSE_INCOMPLETE;
        }
        if (!parse_chunk_size_line(
                encoded + pos, (size_t)(line_end - (encoded + pos)), &chunk_size)) {
            free(decoded);
            return CHUNK_PARSE_INVALID;
        }

        pos = (size_t)((line_end - encoded) + 2);
        if (chunk_size > HTTP_REQ_MAX_BODY || decoded_len + chunk_size > HTTP_REQ_MAX_BODY) {
            free(decoded);
            return CHUNK_PARSE_INVALID;
        }
        if (chunk_size == 0) {
            while (1) {
                const char *trailer_end = find_crlf(encoded + pos, encoded_len - pos);
                if (!trailer_end) {
                    free(decoded);
                    return CHUNK_PARSE_INCOMPLETE;
                }
                if (trailer_end == encoded + pos) {
                    pos += 2;
                    if (decoded_out) {
                        if (!decoded) {
                            decoded = (char *)malloc(1);
                            if (!decoded)
                                return CHUNK_PARSE_INVALID;
                        }
                        decoded[decoded_len] = '\0';
                        *decoded_out = decoded;
                    }
                    if (decoded_len_out)
                        *decoded_len_out = decoded_len;
                    if (consumed_len_out)
                        *consumed_len_out = pos;
                    return CHUNK_PARSE_OK;
                }
                pos = (size_t)((trailer_end - encoded) + 2);
            }
        }
        if (encoded_len - pos < chunk_size + 2) {
            free(decoded);
            return CHUNK_PARSE_INCOMPLETE;
        }

        if (decoded_out && chunk_size > 0) {
            if (decoded_len + chunk_size + 1 > decoded_cap) {
                size_t next_cap = decoded_cap ? decoded_cap : 256;
                while (next_cap < decoded_len + chunk_size + 1)
                    next_cap *= 2;
                char *grown = (char *)realloc(decoded, next_cap);
                if (!grown) {
                    free(decoded);
                    return CHUNK_PARSE_INVALID;
                }
                decoded = grown;
                decoded_cap = next_cap;
            }
            memcpy(decoded + decoded_len, encoded + pos, chunk_size);
        }
        decoded_len += chunk_size;
        pos += chunk_size;

        if (encoded[pos] != '\r' || encoded[pos + 1] != '\n') {
            free(decoded);
            return CHUNK_PARSE_INVALID;
        }
        pos += 2;
    }
}

/// @brief Scan a raw HTTP header block to extract framing hints.
static bool parse_transfer_encoding_value_strict(const char *value,
                                                 size_t value_len,
                                                 bool *chunked_out);

static bool parse_content_length_header_block(const char *headers,
                                              size_t headers_len,
                                              size_t *content_length_out,
                                              bool *chunked_out) {
    const char *p = headers;
    const char *end = headers + headers_len;
    size_t content_length = 0;
    bool saw_content_length = false;
    bool saw_chunked = false;

    while (p < end) {
        const char *line_end = find_crlf(p, (size_t)(end - p));
        if (!line_end)
            return false;
        if (line_end == p)
            break;

        const char *colon = find_char_bounded(p, (size_t)(line_end - p), ':');
        if (colon) {
            size_t name_len = (size_t)(colon - p);
            if (name_len == strlen("Content-Length") &&
                strncasecmp(p, "Content-Length", name_len) == 0) {
                const char *value = colon + 1;
                while (value < line_end && (*value == ' ' || *value == '\t'))
                    value++;
                if (saw_content_length)
                    return false;
                if (!parse_size_decimal(value, (size_t)(line_end - value), &content_length))
                    return false;
                saw_content_length = true;
            } else if (name_len == strlen("Transfer-Encoding") &&
                       strncasecmp(p, "Transfer-Encoding", name_len) == 0) {
                const char *value = colon + 1;
                while (value < line_end && (*value == ' ' || *value == '\t'))
                    value++;
                bool value_chunked = false;
                if (!parse_transfer_encoding_value_strict(
                        value, (size_t)(line_end - value), &value_chunked))
                    return false;
                if (value_chunked) {
                    if (saw_chunked)
                        return false;
                    saw_chunked = true;
                }
            }
        }

        p = line_end + 2;
    }

    if (saw_chunked && saw_content_length)
        return false;
    *content_length_out = saw_content_length ? content_length : 0;
    if (chunked_out)
        *chunked_out = saw_chunked;
    return true;
}

static bool parse_transfer_encoding_value_strict(const char *value,
                                                 size_t value_len,
                                                 bool *chunked_out) {
    const char *p = value;
    const char *end = value + value_len;
    bool saw_token = false;
    bool saw_chunked = false;

    if (chunked_out)
        *chunked_out = false;
    if (!value)
        return false;

    while (p < end) {
        const char *token_start;
        const char *token_end;
        bool token_is_chunked = false;

        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end)
            return false;
        if (*p == ',')
            return false;

        token_start = p;
        while (p < end && *p != ',')
            p++;
        token_end = p;
        while (token_end > token_start && (token_end[-1] == ' ' || token_end[-1] == '\t'))
            token_end--;

        if (token_end == token_start)
            return false;
        token_is_chunked =
            (size_t)(token_end - token_start) == 7 && strncasecmp(token_start, "chunked", 7) == 0;
        if (!token_is_chunked)
            return false;
        if (saw_chunked)
            return false;
        saw_token = true;
        saw_chunked = true;

        if (p < end) {
            p++;
            while (p < end && (*p == ' ' || *p == '\t'))
                p++;
            if (p >= end)
                return false;
        }
    }

    if (!saw_token)
        return false;
    if (chunked_out)
        *chunked_out = saw_chunked;
    return true;
}

typedef enum {
    HTTP_REQUEST_FRAME_INCOMPLETE = 0,
    HTTP_REQUEST_FRAME_COMPLETE = 1,
    HTTP_REQUEST_FRAME_INVALID = -1
} http_request_frame_status_t;

static http_request_frame_status_t find_complete_http_request_frame(const char *buf,
                                                                    size_t buf_len,
                                                                    size_t *request_len_out,
                                                                    size_t *desired_cap_out) {
    const char *header_end = find_header_end(buf, buf_len);
    size_t header_end_pos = 0;
    size_t content_length = 0;
    bool chunked = false;

    if (request_len_out)
        *request_len_out = 0;
    if (desired_cap_out)
        *desired_cap_out = 0;
    if (!buf)
        return HTTP_REQUEST_FRAME_INVALID;
    if (!header_end)
        return HTTP_REQUEST_FRAME_INCOMPLETE;

    header_end_pos = (size_t)(header_end - buf);
    if (!parse_content_length_header_block(buf, header_end_pos, &content_length, &chunked) ||
        content_length > HTTP_REQ_MAX_BODY) {
        return HTTP_REQUEST_FRAME_INVALID;
    }

    if (chunked) {
        size_t consumed_len = 0;
        chunk_parse_status_t status = decode_chunked_body(
            buf + header_end_pos, buf_len - header_end_pos, NULL, NULL, &consumed_len);
        if (status == CHUNK_PARSE_OK) {
            if (request_len_out)
                *request_len_out = header_end_pos + consumed_len;
            return HTTP_REQUEST_FRAME_COMPLETE;
        }
        if (status == CHUNK_PARSE_INVALID)
            return HTTP_REQUEST_FRAME_INVALID;
        if (desired_cap_out)
            *desired_cap_out = header_end_pos + HTTP_REQ_MAX_ENCODED_BODY + 1;
        return HTTP_REQUEST_FRAME_INCOMPLETE;
    }

    if (request_len_out)
        *request_len_out = header_end_pos + content_length;
    if (buf_len >= header_end_pos + content_length)
        return HTTP_REQUEST_FRAME_COMPLETE;
    if (desired_cap_out)
        *desired_cap_out = header_end_pos + content_length + 1;
    return HTTP_REQUEST_FRAME_INCOMPLETE;
}

/// @brief Parse an HTTP request from raw bytes.
static bool parse_http_request(const char *raw, size_t raw_len, server_req_t *req) {
    memset(req, 0, sizeof(*req));

    const char *line_end = find_crlf(raw, raw_len);
    if (!line_end)
        return false;

    const char *p = raw;
    const char *space = find_char_bounded(p, (size_t)(line_end - p), ' ');
    if (!space || space > line_end)
        return false;

    size_t method_len = (size_t)(space - p);
    req->method = (char *)malloc(method_len + 1);
    if (!req->method)
        return false;
    memcpy(req->method, p, method_len);
    req->method[method_len] = '\0';
    if (!http_method_is_valid(req->method, method_len))
        goto fail;

    p = space + 1;
    space = find_char_bounded(p, (size_t)(line_end - p), ' ');
    if (!space || space > line_end)
        goto fail;

    size_t uri_len = (size_t)(space - p);
    char *uri = (char *)malloc(uri_len + 1);
    if (!uri)
        goto fail;
    memcpy(uri, p, uri_len);
    uri[uri_len] = '\0';

    if (!set_request_target(req, uri, uri_len)) {
        free(uri);
        goto fail;
    }
    free(uri);

    p = space + 1;
    if ((size_t)(line_end - p) != strlen("HTTP/1.1") || strncmp(p, "HTTP/1.", 7) != 0 ||
        (p[7] != '0' && p[7] != '1')) {
        goto fail;
    }
    req->http_major = 1;
    req->http_minor = p[7] - '0';

    req->headers = rt_map_new();
    if (!req->headers)
        goto fail;
    p = line_end + 2;
    const char *headers_begin = p;
    size_t content_length = 0;
    const char *body_start = NULL;

    int header_count = 0;
    while (p < raw + raw_len && header_count < HTTP_REQ_MAX_HEADERS) {
        const char *next_end = find_crlf(p, (size_t)(raw + raw_len - p));
        if (!next_end)
            goto fail;
        if (next_end == p) {
            body_start = next_end + 2;
            break;
        }

        const char *colon = find_char_bounded(p, (size_t)(next_end - p), ':');
        if (colon && colon < next_end) {
            size_t name_len = (size_t)(colon - p);
            char *name = (char *)malloc(name_len + 1);
            if (name) {
                memcpy(name, p, name_len);
                name[name_len] = '\0';
                for (char *c = name; *c; c++) {
                    if (*c >= 'A' && *c <= 'Z')
                        *c += 32;
                }

                const char *val = colon + 1;
                while (val < next_end && (*val == ' ' || *val == '\t'))
                    val++;
                size_t val_len = (size_t)(next_end - val);

                rt_string key = rt_string_from_bytes(name, name_len);
                rt_string value = rt_string_from_bytes(val, val_len);
                rt_map_set(req->headers, key, (void *)value);
                rt_string_unref(key);
                rt_string_unref(value);
                free(name);
            }
        }

        p = next_end + 2;
        header_count++;
    }
    if (!body_start && header_count >= HTTP_REQ_MAX_HEADERS)
        goto fail;
    if (!body_start)
        body_start = p;

    bool chunked = false;
    size_t headers_len = body_start > headers_begin ? (size_t)(body_start - headers_begin - 2) : 0;
    if (!parse_content_length_header_block(headers_begin, headers_len, &content_length, &chunked))
        goto fail;

    if (chunked) {
        size_t available = (size_t)(raw + raw_len - body_start);
        size_t consumed_len = 0;
        char *decoded_body = NULL;
        if (decode_chunked_body(
                body_start, available, &decoded_body, &req->body_len, &consumed_len) !=
            CHUNK_PARSE_OK)
            goto fail;
        req->body = decoded_body;
    } else if (content_length > HTTP_REQ_MAX_BODY) {
        goto fail;
    } else if (content_length > 0) {
        size_t available = (size_t)(raw + raw_len - body_start);
        if (available < content_length)
            goto fail;
        req->body = (char *)malloc(content_length + 1);
        if (!req->body)
            goto fail;
        memcpy(req->body, body_start, content_length);
        req->body[content_length] = '\0';
        req->body_len = content_length;
    }

    return true;

fail:
    free_server_req(req);
    memset(req, 0, sizeof(*req));
    return false;
}

static const char *map_get_lower_header_cstr(void *headers, const char *lower_name) {
    if (!headers || !lower_name)
        return NULL;
    rt_string key = rt_const_cstr(lower_name);
    void *val = rt_map_get(headers, key);
    if (!val)
        return NULL;
    return rt_string_cstr((rt_string)val);
}

static int request_allows_keep_alive(const server_req_t *req) {
    const char *connection = req ? map_get_lower_header_cstr(req->headers, "connection") : NULL;
    if (!req)
        return 0;
    if (req->http_major > 1 || (req->http_major == 1 && req->http_minor >= 1)) {
        if (!connection)
            return 1;
        return !header_value_contains_token(connection, strlen(connection), "close", 5);
    }
    if (!connection)
        return 0;
    return header_value_contains_token(connection, strlen(connection), "keep-alive", 10);
}

static int response_forces_close(const server_res_t *res) {
    const char *connection = res ? map_get_lower_header_cstr(res->headers, "connection") : NULL;
    if (!connection)
        return 0;
    return header_value_contains_token(connection, strlen(connection), "close", 5);
}

/// @brief Map an HTTP status code to its canonical reason-phrase.
static const char *status_text_for_code(int code) {
    switch (code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

/// @brief Build an HTTP response string.
static char *build_response(server_res_t *res, int keep_alive, size_t *out_len) {
    size_t body_len = res->body_len;
    size_t cap = (size_t)snprintf(
        NULL, 0, "HTTP/1.1 %d %s\r\n", res->status_code, status_text_for_code(res->status_code));
    cap += (size_t)snprintf(NULL, 0, "Content-Length: %zu\r\n", body_len);
    cap += strlen(keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");

    if (res->headers) {
        void *keys = rt_map_keys(res->headers);
        int64_t count = rt_seq_len(keys);
        for (int64_t i = 0; i < count; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *val = rt_map_get(res->headers, key);
            if (val) {
                const char *k = rt_string_cstr(key);
                const char *v = rt_string_cstr((rt_string)val);
                if (k && v && !contains_crlf(k) && !contains_crlf(v) &&
                    !is_server_managed_header_name(k)) {
                    size_t k_len = strlen(k);
                    size_t v_len = strlen(v);
                    if (cap > SIZE_MAX - (k_len + v_len + 4)) {
                        if (rt_obj_release_check0(keys))
                            rt_obj_free(keys);
                        return NULL;
                    }
                    cap += k_len + v_len + 4;
                }
            }
        }
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }
    if (cap > SIZE_MAX - (body_len + 3))
        return NULL;
    cap += body_len + 2 + 1;

    char *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;

    char *cursor = buf;
    size_t remaining = cap;
#define APPEND_FORMAT(...)                                                                         \
    do {                                                                                           \
        int written = snprintf(cursor, remaining, __VA_ARGS__);                                    \
        if (written < 0 || (size_t)written >= remaining) {                                         \
            free(buf);                                                                             \
            return NULL;                                                                           \
        }                                                                                          \
        cursor += written;                                                                         \
        remaining -= (size_t)written;                                                              \
    } while (0)

    APPEND_FORMAT("HTTP/1.1 %d %s\r\n", res->status_code, status_text_for_code(res->status_code));
    APPEND_FORMAT("Content-Length: %zu\r\n", body_len);
    APPEND_FORMAT("%s", keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");

    if (res->headers) {
        void *keys = rt_map_keys(res->headers);
        int64_t count = rt_seq_len(keys);
        for (int64_t i = 0; i < count; i++) {
            rt_string key = (rt_string)rt_seq_get(keys, i);
            void *val = rt_map_get(res->headers, key);
            if (val) {
                const char *k = rt_string_cstr(key);
                const char *v = rt_string_cstr((rt_string)val);
                if (k && v && !contains_crlf(k) && !contains_crlf(v) &&
                    !is_server_managed_header_name(k)) {
                    int written = snprintf(cursor, remaining, "%s: %s\r\n", k, v);
                    if (written < 0 || (size_t)written >= remaining) {
                        if (rt_obj_release_check0(keys))
                            rt_obj_free(keys);
                        free(buf);
                        return NULL;
                    }
                    cursor += written;
                    remaining -= (size_t)written;
                }
            }
        }
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }

    APPEND_FORMAT("%s", "\r\n");

    if (res->body && body_len > 0) {
        if (body_len > remaining) {
            free(buf);
            return NULL;
        }
        memcpy(cursor, res->body, body_len);
        cursor += body_len;
    }

    *out_len = (size_t)(cursor - buf);
#undef APPEND_FORMAT
    return buf;
}
