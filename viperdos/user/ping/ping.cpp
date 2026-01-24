/**
 * @file ping.cpp
 * @brief ICMP ping utility for ViperDOS.
 *
 * @details
 * This utility sends ICMP echo requests to test network connectivity.
 * DNS resolution uses libc gethostbyname() which routes through netd.
 * The SYS_PING syscall performs ICMP operations in the kernel (required
 * for raw socket access).
 *
 * Usage:
 *   ping              - Prompts for IP address
 *   ping <ip>         - Pings the specified IP (via args mechanism when available)
 */

#include "../syscall.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Parse IP address from string (e.g., "192.168.1.1")
static bool parse_ip(const char *str, u32 *ip_out) {
    u32 octets[4] = {0, 0, 0, 0};
    int octet_idx = 0;

    while (*str && octet_idx < 4) {
        if (*str >= '0' && *str <= '9') {
            octets[octet_idx] = octets[octet_idx] * 10 + (*str - '0');
            if (octets[octet_idx] > 255)
                return false;
        } else if (*str == '.') {
            octet_idx++;
        } else {
            return false;
        }
        str++;
    }

    if (octet_idx != 3)
        return false;

    *ip_out = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return true;
}

// DNS resolve wrapper - uses libc gethostbyname() which routes through netd
static bool resolve_host(const char *hostname, u32 *ip_out) {
    struct hostent *he = gethostbyname(hostname);
    if (!he || !he->h_addr_list[0])
        return false;

    // Convert from network byte order to host byte order for display
    u32 ip_be;
    memcpy(&ip_be, he->h_addr_list[0], sizeof(ip_be));
    *ip_out = ntohl(ip_be);
    return true;
}

// Ping syscall wrapper
static i32 do_ping(u32 ip, u32 timeout_ms) {
    auto r = sys::syscall2(SYS_PING, static_cast<u64>(ip), static_cast<u64>(timeout_ms));
    if (r.ok())
        return static_cast<i32>(r.val0);
    return static_cast<i32>(r.error);
}

// Print IP address
static void print_ip(u32 ip) {
    printf("%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

// Read a line from console (simple implementation)
static void read_line(char *buf, size_t max) {
    size_t i = 0;
    while (i < max - 1) {
        char c = sys::getchar();
        if (c == '\r' || c == '\n') {
            sys::putchar('\n');
            break;
        } else if (c == 127 || c == 8) // Backspace
        {
            if (i > 0) {
                i--;
                sys::putchar('\b');
                sys::putchar(' ');
                sys::putchar('\b');
            }
        } else if (c >= 32) {
            buf[i++] = c;
            sys::putchar(c);
        }
    }
    buf[i] = '\0';
}

extern "C" void _start() {
    printf("\n=== ViperDOS Ping Utility ===\n\n");

    char input[128];
    u32 ip = 0;

    // Try to get args from spawn
    i64 args_len = sys::get_args(input, sizeof(input));

    // If no args provided, prompt for target
    if (args_len <= 0 || input[0] == '\0') {
        printf("Enter IP address or hostname: ");
        read_line(input, sizeof(input));
    }

    if (input[0] == '\0') {
        printf("No target specified.\n");
        sys::exit(1);
    }

    // Try parsing as IP first
    if (!parse_ip(input, &ip)) {
        // Try DNS resolution
        printf("Resolving %s...\n", input);
        if (!resolve_host(input, &ip)) {
            printf("Error: Could not resolve '%s'\n", input);
            sys::exit(1);
        }
        printf("Resolved to ");
        print_ip(ip);
        printf("\n");
    }

    // Perform ping
    printf("\nPinging ");
    print_ip(ip);
    printf(" with 4 requests...\n\n");

    int success_count = 0;
    i32 min_rtt = 0x7FFFFFFF;
    i32 max_rtt = 0;
    i32 total_rtt = 0;

    for (int i = 0; i < 4; i++) {
        i32 rtt = do_ping(ip, 5000); // 5 second timeout

        if (rtt >= 0) {
            printf("Reply from ");
            print_ip(ip);
            printf(": time=%dms\n", rtt);

            success_count++;
            total_rtt += rtt;
            if (rtt < min_rtt)
                min_rtt = rtt;
            if (rtt > max_rtt)
                max_rtt = rtt;
        } else {
            printf("Request timed out.\n");
        }

        // Small delay between pings (500ms)
        if (i < 3) {
            auto r = sys::syscall1(SYS_SLEEP, 500);
            (void)r;
        }
    }

    // Print statistics
    printf("\n--- ");
    print_ip(ip);
    printf(" ping statistics ---\n");
    printf("4 packets transmitted, %d received, %d%% packet loss\n",
           success_count,
           ((4 - success_count) * 100) / 4);

    if (success_count > 0) {
        printf("rtt min/avg/max = %d/%d/%dms\n", min_rtt, total_rtt / success_count, max_rtt);
    }

    printf("\n");
    sys::exit(success_count > 0 ? 0 : 1);
}
