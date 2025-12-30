/**
 * @file cmd_misc.cpp
 * @brief Miscellaneous shell commands for vinit (run, assign, path, fetch).
 */
#include "vinit.hpp"

void cmd_run(const char *path)
{
    if (!path || *path == '\0')
    {
        print_str("Run: missing program path\n");
        last_rc = RC_ERROR;
        last_error = "No path specified";
        return;
    }

    u64 pid = 0;
    u64 tid = 0;
    i64 err = sys::spawn(path, nullptr, &pid, &tid);

    // If not found and not an absolute/relative path, try C: directory
    if (err < 0 && path[0] != '/' && !strstart(path, "./") && !strstart(path, "../"))
    {
        // Build path: /c/<name> or /c/<name>.elf
        char search_path[256];
        usize i = 0;

        // Add /c/ prefix
        search_path[i++] = '/';
        search_path[i++] = 'c';
        search_path[i++] = '/';

        // Copy the command name
        const char *p = path;
        while (*p && i < 250)
            search_path[i++] = *p++;
        search_path[i] = '\0';

        // Try with the name as-is first
        err = sys::spawn(search_path, nullptr, &pid, &tid);

        // If still not found and doesn't end in .elf, try adding .elf
        if (err < 0)
        {
            usize len = i;
            if (len < 4 || !streq(search_path + len - 4, ".elf"))
            {
                if (len + 4 < 255)
                {
                    search_path[len++] = '.';
                    search_path[len++] = 'e';
                    search_path[len++] = 'l';
                    search_path[len++] = 'f';
                    search_path[len] = '\0';
                    err = sys::spawn(search_path, nullptr, &pid, &tid);
                }
            }
        }

        if (err >= 0)
        {
            path = search_path; // Update for display
        }
    }

    if (err < 0)
    {
        print_str("Run: failed to spawn \"");
        print_str(path);
        print_str("\" (error ");
        put_num(err);
        print_str(")\n");
        last_rc = RC_FAIL;
        last_error = "Spawn failed";
        return;
    }

    print_str("Started process ");
    put_num(static_cast<i64>(pid));
    print_str(" (task ");
    put_num(static_cast<i64>(tid));
    print_str(")\n");

    // Wait for the child process to exit
    i32 status = 0;
    i64 exited_pid = sys::waitpid(pid, &status);

    if (exited_pid < 0)
    {
        print_str("Run: wait failed (error ");
        put_num(exited_pid);
        print_str(")\n");
        last_rc = RC_FAIL;
        last_error = "Wait failed";
        return;
    }

    print_str("Process ");
    put_num(exited_pid);
    print_str(" exited with status ");
    put_num(static_cast<i64>(status));
    print_str("\n");
    last_rc = RC_OK;
}

void cmd_assign(const char *args)
{
    if (!args || *args == '\0')
    {
        // List all assigns
        sys::AssignInfo assigns[16];
        usize count = 0;

        i32 result = sys::assign_list(assigns, 16, &count);
        if (result < 0)
        {
            print_str("Assign: failed to list assigns\n");
            last_rc = RC_ERROR;
            return;
        }

        print_str("Current assigns:\n");
        print_str("  Name         Handle     Flags\n");
        print_str("  -----------  ---------  ------\n");

        for (usize i = 0; i < count; i++)
        {
            print_str("  ");
            print_str(assigns[i].name);
            print_str(":");
            usize namelen = strlen(assigns[i].name) + 1;
            while (namelen < 11) { print_char(' '); namelen++; }
            print_str("  ");

            put_hex(assigns[i].handle);
            print_str("   ");

            if (assigns[i].flags & sys::ASSIGN_SYSTEM)
                print_str("SYS");
            if (assigns[i].flags & sys::ASSIGN_MULTI)
            {
                if (assigns[i].flags & sys::ASSIGN_SYSTEM) print_str(",");
                print_str("MULTI");
            }
            if (assigns[i].flags == 0)
                print_str("-");
            print_str("\n");
        }

        if (count == 0)
            print_str("  (no assigns defined)\n");

        print_str("\n");
        put_num(static_cast<i64>(count));
        print_str(" assign");
        if (count != 1) print_str("s");
        print_str(" defined\n");

        last_rc = RC_OK;
    }
    else
    {
        print_str("Usage: Assign           - List all assigns\n");
        print_str("       Assign NAME: DIR - Set assign (not yet implemented)\n");
        last_rc = RC_WARN;
    }
}

void cmd_path(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Current path: SYS:\n");
        last_rc = RC_OK;
    }
    else
    {
        u32 handle = 0;
        i32 result = sys::assign_resolve(args, &handle);
        if (result < 0)
        {
            print_str("Path: cannot resolve \"");
            print_str(args);
            print_str("\" - not found or invalid assign\n");
            last_rc = RC_ERROR;
            return;
        }

        print_str("Path \"");
        print_str(args);
        print_str("\"\n");
        print_str("  Handle: ");
        put_hex(handle);
        print_str("\n");

        CapInfo cap_info;
        if (sys::cap_query(handle, &cap_info) == 0)
        {
            print_str("  Kind:   ");
            print_str(sys::cap_kind_name(cap_info.kind));
            print_str("\n");

            print_str("  Rights: ");
            char rights[16];
            sys::cap_rights_str(cap_info.rights, rights, sizeof(rights));
            print_str(rights);
            print_str("\n");
        }

        sys::fs_close(handle);
        last_rc = RC_OK;
    }
}

// URL parsing helper
struct ParsedUrl {
    char host[128];
    char path[256];
    u16 port;
    bool is_https;
};

static bool parse_url(const char *url, ParsedUrl *out)
{
    out->host[0] = '\0';
    out->port = 80;
    out->path[0] = '/';
    out->path[1] = '\0';
    out->is_https = false;

    const char *p = url;

    if (strstart(p, "https://"))
    {
        out->is_https = true;
        out->port = 443;
        p += 8;
    }
    else if (strstart(p, "http://"))
    {
        p += 7;
    }

    usize host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < 127)
        out->host[host_len++] = *p++;
    out->host[host_len] = '\0';

    if (host_len == 0) return false;

    if (*p == ':')
    {
        p++;
        u16 port = 0;
        while (*p >= '0' && *p <= '9')
        {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port > 0) out->port = port;
    }

    if (*p == '/')
    {
        usize path_len = 0;
        while (*p && path_len < 255)
            out->path[path_len++] = *p++;
        out->path[path_len] = '\0';
    }

    return true;
}

void cmd_fetch(const char *url)
{
    if (!url || *url == '\0')
    {
        print_str("Fetch: usage: Fetch <url>\n");
        print_str("  Examples:\n");
        print_str("    Fetch example.com\n");
        print_str("    Fetch http://example.com/page\n");
        print_str("    Fetch https://example.com\n");
        last_rc = RC_ERROR;
        last_error = "Missing URL";
        return;
    }

    ParsedUrl parsed;

    if (!strstart(url, "http://") && !strstart(url, "https://"))
    {
        usize i = 0;
        while (url[i] && url[i] != '/' && i < 127)
        {
            parsed.host[i] = url[i];
            i++;
        }
        parsed.host[i] = '\0';
        parsed.port = 80;
        parsed.path[0] = '/';
        parsed.path[1] = '\0';
        parsed.is_https = false;
    }
    else
    {
        if (!parse_url(url, &parsed))
        {
            print_str("Fetch: invalid URL\n");
            last_rc = RC_ERROR;
            return;
        }
    }

    print_str("Resolving ");
    print_str(parsed.host);
    print_str("...\n");

    u32 ip = 0;
    if (sys::dns_resolve(parsed.host, &ip) != 0)
    {
        print_str("Fetch: DNS resolution failed\n");
        last_rc = RC_ERROR;
        return;
    }

    print_str("Connecting to ");
    put_num((ip >> 24) & 0xFF);
    print_char('.');
    put_num((ip >> 16) & 0xFF);
    print_char('.');
    put_num((ip >> 8) & 0xFF);
    print_char('.');
    put_num(ip & 0xFF);
    print_char(':');
    put_num(parsed.port);
    if (parsed.is_https) print_str(" (HTTPS)");
    print_str("...\n");

    i32 sock = sys::socket_create();
    if (sock < 0)
    {
        print_str("Fetch: failed to create socket\n");
        last_rc = RC_FAIL;
        return;
    }

    if (sys::socket_connect(sock, ip, parsed.port) != 0)
    {
        print_str("Fetch: connection failed\n");
        sys::socket_close(sock);
        last_rc = RC_ERROR;
        return;
    }

    print_str("Connected!");

    i32 tls_session = -1;
    if (parsed.is_https)
    {
        print_str(" Starting TLS handshake...\n");
        tls_session = sys::tls_create(sock, parsed.host, false);
        if (tls_session < 0)
        {
            print_str("Fetch: TLS session creation failed\n");
            sys::socket_close(sock);
            last_rc = RC_ERROR;
            return;
        }

        if (sys::tls_handshake(tls_session) != 0)
        {
            print_str("Fetch: TLS handshake failed\n");
            sys::tls_close(tls_session);
            sys::socket_close(sock);
            last_rc = RC_ERROR;
            return;
        }

        print_str("TLS handshake complete. ");
    }

    print_str(" Sending request...\n");

    // Build HTTP request
    char request[512];
    usize pos = 0;
    const char *get = "GET ";
    while (*get) request[pos++] = *get++;
    const char *path = parsed.path;
    while (*path && pos < 400) request[pos++] = *path++;
    const char *proto = " HTTP/1.0\r\nHost: ";
    while (*proto) request[pos++] = *proto++;
    const char *host = parsed.host;
    while (*host && pos < 450) request[pos++] = *host++;
    const char *tail = "\r\nUser-Agent: ViperOS/0.2\r\nConnection: close\r\n\r\n";
    while (*tail) request[pos++] = *tail++;
    request[pos] = '\0';

    i64 sent;
    if (parsed.is_https)
        sent = sys::tls_send(tls_session, request, pos);
    else
        sent = sys::socket_send(sock, request, pos);

    if (sent <= 0)
    {
        print_str("Fetch: send failed\n");
        if (parsed.is_https) sys::tls_close(tls_session);
        sys::socket_close(sock);
        last_rc = RC_ERROR;
        return;
    }

    print_str("Request sent, receiving response...\n\n");

    char buf[512];
    usize total = 0;
    for (int tries = 0; tries < 100; tries++)
    {
        i64 n;
        if (parsed.is_https)
            n = sys::tls_recv(tls_session, buf, sizeof(buf) - 1);
        else
            n = sys::socket_recv(sock, buf, sizeof(buf) - 1);

        if (n > 0)
        {
            buf[n] = '\0';
            print_str(buf);
            total += n;
        }
        else if (total > 0)
        {
            break;
        }
        for (int i = 0; i < 100000; i++)
            asm volatile("" ::: "memory");
    }

    print_str("\n\n[Received ");
    put_num(static_cast<i64>(total));
    print_str(" bytes");
    if (parsed.is_https) print_str(", encrypted");
    print_str("]\n");

    if (parsed.is_https) sys::tls_close(tls_session);
    sys::socket_close(sock);
    last_rc = RC_OK;
}
