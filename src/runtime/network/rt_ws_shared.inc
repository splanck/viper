//===----------------------------------------------------------------------===//
//
// Shared helpers for ws_server.c and wss_server.c.
//
//===----------------------------------------------------------------------===//

static int ws_token_is_valid(const char *value) {
    static const char *kSeparators = "()<>@,;:\\\"/[]?={} \t";
    if (!value || !*value)
        return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p <= 0x20 || *p == 0x7Fu || strchr(kSeparators, (int)*p) != NULL)
            return 0;
    }
    return 1;
}

static int ws_header_has_token_impl(const char *value, const char *token, int case_sensitive) {
    const char *p = value;
    size_t token_len = token ? strlen(token) : 0;
    if (!value || !token || token_len == 0)
        return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if ((size_t)(end - start) == token_len &&
            (case_sensitive ? memcmp(start, token, token_len) == 0
                            : strncasecmp(start, token, token_len) == 0))
            return 1;
    }
    return 0;
}

static int ws_header_has_upgrade_token(const char *value) {
    return ws_header_has_token_impl(value, "Upgrade", 0);
}

static int ws_protocol_header_is_valid(const char *value) {
    const char *p = value;
    int saw_any = 0;

    if (!value)
        return 0;

    while (*p) {
        char token[256];
        size_t len = 0;

        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (!*p)
            break;
        while (*p && *p != ',') {
            if (len + 1 >= sizeof(token))
                return 0;
            token[len++] = *p++;
        }
        while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t'))
            len--;
        token[len] = '\0';
        if (!ws_token_is_valid(token))
            return 0;
        saw_any = 1;
    }

    return saw_any;
}

static int ws_protocol_list_contains(const char *value, const char *token) {
    if (!ws_protocol_header_is_valid(value) || !ws_token_is_valid(token))
        return 0;
    return ws_header_has_token_impl(value, token, 1);
}

static char *ws_strdup_trimmed(const char *text) {
    if (!text)
        return NULL;
    while (*text == ' ' || *text == '\t')
        text++;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t'))
        len--;
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

static int ws_parse_authority(const char *authority, char **host_out, int *port_out) {
    char *trimmed = NULL;
    char *host = NULL;
    int port = -1;
    int saw_port = 0;
    char *cursor;

    if (host_out)
        *host_out = NULL;
    if (port_out)
        *port_out = -1;
    if (!authority || !*authority)
        return 0;

    trimmed = ws_strdup_trimmed(authority);
    if (!trimmed || !*trimmed) {
        free(trimmed);
        return 0;
    }

    cursor = trimmed;
    if (*cursor == '[') {
        char *end = strchr(cursor + 1, ']');
        char *after = NULL;
        size_t host_len;
        if (!end) {
            free(trimmed);
            return 0;
        }
        host_len = (size_t)(end - (cursor + 1));
        if (host_len == 0) {
            free(trimmed);
            return 0;
        }
        host = (char *)malloc(host_len + 1);
        if (!host) {
            free(trimmed);
            return 0;
        }
        memcpy(host, cursor + 1, host_len);
        host[host_len] = '\0';
        after = end + 1;
        if (*after == ':') {
            char *port_text = after + 1;
            port = 0;
            if (!*port_text) {
                free(host);
                free(trimmed);
                return 0;
            }
            while (*port_text) {
                if (*port_text < '0' || *port_text > '9') {
                    free(host);
                    free(trimmed);
                    return 0;
                }
                saw_port = 1;
                port = port * 10 + (*port_text - '0');
                port_text++;
            }
            if (!saw_port) {
                free(host);
                free(trimmed);
                return 0;
            }
        } else if (*after != '\0') {
            free(host);
            free(trimmed);
            return 0;
        }
    } else {
        char *colon = strrchr(cursor, ':');
        if (colon && strchr(colon + 1, ':') == NULL) {
            char *port_text = colon + 1;
            port = 0;
            while (*port_text) {
                if (*port_text < '0' || *port_text > '9') {
                    saw_port = 0;
                    break;
                }
                saw_port = 1;
                port = port * 10 + (*port_text - '0');
                port_text++;
            }
            if (saw_port) {
                *colon = '\0';
            } else {
                port = -1;
            }
        }
        if (*cursor == '\0') {
            free(trimmed);
            return 0;
        }
        host = strdup(cursor);
        if (!host) {
            free(trimmed);
            return 0;
        }
    }

    free(trimmed);
    if (!host || !*host) {
        free(host);
        return 0;
    }

    if (host_out)
        *host_out = host;
    else
        free(host);
    if (port_out)
        *port_out = port;
    return 1;
}

static int ws_host_header_is_valid(const char *host_header) {
    char *trimmed = NULL;
    char *host = NULL;
    int port = -1;
    int ok = 0;

    trimmed = ws_strdup_trimmed(host_header);
    if (!trimmed || !*trimmed) {
        free(trimmed);
        return 0;
    }

    if (strchr(trimmed, ':') != strrchr(trimmed, ':') && trimmed[0] != '[') {
        free(trimmed);
        return 0;
    }

    if (!ws_parse_authority(trimmed, &host, &port)) {
        free(trimmed);
        return 0;
    }

    ok = host && *host && (port < 0 || (port >= 1 && port <= 65535));
    free(trimmed);
    free(host);
    return ok;
}

static int ws_request_line_is_valid(const char *request_line) {
    const char *target = NULL;
    const char *version = NULL;

    if (!request_line || strncmp(request_line, "GET ", 4) != 0)
        return 0;

    target = request_line + 4;
    version = strchr(target, ' ');
    if (!version || version == target || strcmp(version, " HTTP/1.1") != 0)
        return 0;

    if (*target != '/')
        return 0;

    for (const char *p = target; p < version; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch == 0x7F)
            return 0;
    }

    return 1;
}

static int ws_sec_key_is_valid(const char *key) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = 0;

    if (!key)
        return 0;

    len = strlen(key);
    if (len != 24)
        return 0;

    for (size_t i = 0; i < 22; ++i) {
        if (!strchr(base64_chars, key[i]))
            return 0;
    }

    return key[22] == '=' && key[23] == '=';
}

static int ws_origin_matches_expected(const char *origin,
                                      const char *host_header,
                                      const char *expected_scheme,
                                      int default_port) {
    const char *scheme_sep = NULL;
    const char *authority = NULL;
    const char *authority_end = NULL;
    size_t scheme_len = 0;
    size_t authority_len = 0;
    char *origin_authority = NULL;
    char *origin_host = NULL;
    char *request_host = NULL;
    int origin_port = -1;
    int request_port = -1;
    int matches = 0;

    if (!origin || !*origin)
        return 1;
    if (!host_header || !*host_header || !expected_scheme || default_port <= 0)
        return 0;

    scheme_sep = strstr(origin, "://");
    if (!scheme_sep)
        return 0;
    scheme_len = (size_t)(scheme_sep - origin);
    if (scheme_len != strlen(expected_scheme) ||
        strncasecmp(origin, expected_scheme, scheme_len) != 0) {
        return 0;
    }

    authority = scheme_sep + 3;
    authority_end = authority;
    while (*authority_end && *authority_end != '/' && *authority_end != '?' && *authority_end != '#')
        authority_end++;
    authority_len = (size_t)(authority_end - authority);
    origin_authority = (char *)malloc(authority_len + 1);
    if (!origin_authority)
        return 0;
    memcpy(origin_authority, authority, authority_len);
    origin_authority[authority_len] = '\0';

    if (!ws_parse_authority(origin_authority, &origin_host, &origin_port) ||
        !ws_parse_authority(host_header, &request_host, &request_port)) {
        free(request_host);
        free(origin_host);
        free(origin_authority);
        return 0;
    }

    if (origin_port < 0)
        origin_port = default_port;
    if (request_port < 0)
        request_port = default_port;

    if (strcasecmp(origin_host, request_host) == 0 && origin_port == request_port)
        matches = 1;

    free(request_host);
    free(origin_host);
    free(origin_authority);
    return matches;
}

static int ws_is_valid_opcode(uint8_t opcode) {
    switch (opcode) {
        case WS_OP_CONTINUATION:
        case WS_OP_TEXT:
        case WS_OP_BINARY:
        case WS_OP_CLOSE:
        case WS_OP_PING:
        case WS_OP_PONG:
            return 1;
        default:
            return 0;
    }
}

static void ws_encode_u64_len(uint8_t out[8], size_t len) {
    uint64_t value = (uint64_t)len;
    for (int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(value & 0xFFu);
        value >>= 8;
    }
}

static int ws_decode_u64_len(const uint8_t in[8], size_t *len_out) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value = (value << 8) | in[i];
    if (value > (uint64_t)SIZE_MAX)
        return 0;
    *len_out = (size_t)value;
    return 1;
}

static int ws_is_valid_utf8(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i++];
        if (c <= 0x7F)
            continue;
        if (c >= 0xC2 && c <= 0xDF) {
            if (i >= len || (data[i] & 0xC0) != 0x80)
                return 0;
            i++;
            continue;
        }
        if (c == 0xE0) {
            if (i + 1 >= len || data[i] < 0xA0 || data[i] > 0xBF || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }
        if (c >= 0xE1 && c <= 0xEC) {
            if (i + 1 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }
        if (c == 0xED) {
            if (i + 1 >= len || data[i] < 0x80 || data[i] > 0x9F || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }
        if (c >= 0xEE && c <= 0xEF) {
            if (i + 1 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
            continue;
        }
        if (c == 0xF0) {
            if (i + 2 >= len || data[i] < 0x90 || data[i] > 0xBF || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }
        if (c >= 0xF1 && c <= 0xF3) {
            if (i + 2 >= len || (data[i] & 0xC0) != 0x80 || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }
        if (c == 0xF4) {
            if (i + 2 >= len || data[i] < 0x80 || data[i] > 0x8F || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return 0;
            i += 3;
            continue;
        }
        return 0;
    }
    return 1;
}
