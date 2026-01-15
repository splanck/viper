/**
 * @file netstat.cpp
 * @brief Network statistics utility for ViperDOS.
 *
 * @details
 * This utility demonstrates the use of network syscalls and displays
 * comprehensive network stack statistics.
 *
 * Usage:
 *   netstat          - Show network statistics
 */

#include "../../include/viperdos/net_stats.hpp"
#include "../syscall.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Format bytes with appropriate units
static void format_bytes(unsigned long long bytes, char *buf, size_t bufsize)
{
    if (bytes < 1024)
    {
        snprintf(buf, bufsize, "%llu B", bytes);
    }
    else if (bytes < 1024 * 1024)
    {
        snprintf(buf, bufsize, "%llu KB", bytes / 1024);
    }
    else if (bytes < 1024ULL * 1024 * 1024)
    {
        snprintf(buf, bufsize, "%llu MB", bytes / (1024 * 1024));
    }
    else
    {
        snprintf(buf, bufsize, "%llu GB", bytes / (1024ULL * 1024 * 1024));
    }
}

// Get network stats via syscall
static int get_net_stats(NetStats *stats)
{
    auto r = sys::syscall1(SYS_NET_STATS, reinterpret_cast<u64>(stats));
    return static_cast<int>(r.error);
}

static void print_separator()
{
    printf("---------------------------------------------\n");
}

static void print_ethernet_stats(const NetStats *stats)
{
    char rx_bytes_str[32], tx_bytes_str[32];
    format_bytes(stats->eth_rx_bytes, rx_bytes_str, sizeof(rx_bytes_str));
    format_bytes(stats->eth_tx_bytes, tx_bytes_str, sizeof(tx_bytes_str));

    printf("\nEthernet Layer\n");
    print_separator();
    printf("  RX Packets:    %llu\n", stats->eth_rx_packets);
    printf("  TX Packets:    %llu\n", stats->eth_tx_packets);
    printf("  RX Bytes:      %s (%llu)\n", rx_bytes_str, stats->eth_rx_bytes);
    printf("  TX Bytes:      %s (%llu)\n", tx_bytes_str, stats->eth_tx_bytes);
    printf("  RX Errors:     %llu\n", stats->eth_rx_errors);
    printf("  TX Errors:     %llu\n", stats->eth_tx_errors);
    printf("  RX Dropped:    %llu\n", stats->eth_rx_dropped);
}

static void print_ip_stats(const NetStats *stats)
{
    char rx_bytes_str[32], tx_bytes_str[32];
    format_bytes(stats->ip_rx_bytes, rx_bytes_str, sizeof(rx_bytes_str));
    format_bytes(stats->ip_tx_bytes, tx_bytes_str, sizeof(tx_bytes_str));

    printf("\nIPv4 Layer\n");
    print_separator();
    printf("  RX Packets:    %llu\n", stats->ip_rx_packets);
    printf("  TX Packets:    %llu\n", stats->ip_tx_packets);
    printf("  RX Bytes:      %s (%llu)\n", rx_bytes_str, stats->ip_rx_bytes);
    printf("  TX Bytes:      %s (%llu)\n", tx_bytes_str, stats->ip_tx_bytes);
}

static void print_arp_stats(const NetStats *stats)
{
    printf("\nARP Layer\n");
    print_separator();
    printf("  Requests:      %llu\n", stats->arp_requests);
    printf("  Replies:       %llu\n", stats->arp_replies);
}

static void print_icmp_stats(const NetStats *stats)
{
    printf("\nICMP Layer\n");
    print_separator();
    printf("  RX Messages:   %llu\n", stats->icmp_rx);
    printf("  TX Messages:   %llu\n", stats->icmp_tx);
}

static void print_udp_stats(const NetStats *stats)
{
    printf("\nUDP Layer\n");
    print_separator();
    printf("  RX Datagrams:  %llu\n", stats->udp_rx_packets);
    printf("  TX Datagrams:  %llu\n", stats->udp_tx_packets);
}

static void print_tcp_stats(const NetStats *stats)
{
    printf("\nTCP Layer\n");
    print_separator();
    printf("  RX Segments:   %llu\n", stats->tcp_rx_segments);
    printf("  TX Segments:   %llu\n", stats->tcp_tx_segments);
    printf("  Retransmits:   %llu\n", stats->tcp_retransmits);
    printf("  Active Conns:  %u\n", stats->tcp_active_conns);
    printf("  Listen Socks:  %u\n", stats->tcp_listen_sockets);
}

static void print_dns_stats(const NetStats *stats)
{
    printf("\nDNS Layer\n");
    print_separator();
    printf("  Queries:       %llu\n", stats->dns_queries);
    printf("  Responses:     %llu\n", stats->dns_responses);
}

static void print_tls_stats(const NetStats *stats)
{
    printf("\nTLS Layer\n");
    print_separator();
    printf("  Handshakes:    %llu\n", stats->tls_handshakes);
    printf("  RX Records:    %llu\n", stats->tls_rx_records);
    printf("  TX Records:    %llu\n", stats->tls_tx_records);
}

static void print_summary(const NetStats *stats)
{
    printf("\nSummary\n");
    print_separator();

    unsigned long long total_rx = stats->eth_rx_packets;
    unsigned long long total_tx = stats->eth_tx_packets;
    unsigned long long total_errors = stats->eth_rx_errors + stats->eth_tx_errors;

    char rx_str[32], tx_str[32];
    format_bytes(stats->eth_rx_bytes, rx_str, sizeof(rx_str));
    format_bytes(stats->eth_tx_bytes, tx_str, sizeof(tx_str));

    printf("  Total RX:      %llu packets (%s)\n", total_rx, rx_str);
    printf("  Total TX:      %llu packets (%s)\n", total_tx, tx_str);
    printf("  Total Errors:  %llu\n", total_errors);

    if (stats->tcp_retransmits > 0)
    {
        unsigned long long total_tcp = stats->tcp_tx_segments;
        if (total_tcp > 0)
        {
            unsigned long long retrans_pct = (stats->tcp_retransmits * 100) / total_tcp;
            printf("  TCP Retrans:   %llu%% (%llu/%llu)\n",
                   retrans_pct,
                   stats->tcp_retransmits,
                   total_tcp);
        }
    }
}

extern "C" void _start()
{
    printf("\n=== ViperDOS Network Statistics ===\n");
    printf("    (netstat utility v1.0)\n");

    NetStats stats;
    memset(&stats, 0, sizeof(stats));

    if (get_net_stats(&stats) != 0)
    {
        printf("\nError: Failed to get network statistics\n");
        printf("       Network stack may not be initialized\n");
        sys::exit(1);
    }

    // Print all stats sections
    print_ethernet_stats(&stats);
    print_ip_stats(&stats);
    print_arp_stats(&stats);
    print_icmp_stats(&stats);
    print_udp_stats(&stats);
    print_tcp_stats(&stats);
    print_dns_stats(&stats);
    print_tls_stats(&stats);
    print_summary(&stats);

    printf("\n");
    sys::exit(0);
}
