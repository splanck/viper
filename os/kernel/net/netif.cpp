/**
 * @file netif.cpp
 * @brief Implementation of the global network interface configuration.
 *
 * @details
 * Provides the singleton @ref net::NetIf instance used throughout the network
 * stack. Initialization reads the MAC address from virtio-net when available
 * and configures default static IPv4 settings suitable for QEMU networking.
 */

#include "netif.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/net.hpp"

namespace net
{

static NetIf g_netif;

/** @copydoc net::NetIf::init */
void NetIf::init()
{
    // Get MAC from virtio-net device
    if (virtio::net_device())
    {
        virtio::net_device()->get_mac(mac_.bytes);
    }
    else
    {
        mac_ = MacAddr::zero();
    }

    // QEMU user-mode networking defaults
    ip_ = {{10, 0, 2, 15}};
    netmask_ = {{255, 255, 255, 0}};
    gateway_ = {{10, 0, 2, 2}};
    dns_ = {{10, 0, 2, 3}};
}

/** @copydoc net::NetIf::print_config */
void NetIf::print_config() const
{
    serial::puts("[netif] Configuration:\n");

    // MAC address
    serial::puts("  MAC: ");
    const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++)
    {
        if (i > 0)
            serial::putc(':');
        serial::putc(hex[(mac_.bytes[i] >> 4) & 0xf]);
        serial::putc(hex[mac_.bytes[i] & 0xf]);
    }
    serial::puts("\n");

    // IP address
    serial::puts("  IP: ");
    serial::put_dec(ip_.bytes[0]);
    serial::putc('.');
    serial::put_dec(ip_.bytes[1]);
    serial::putc('.');
    serial::put_dec(ip_.bytes[2]);
    serial::putc('.');
    serial::put_dec(ip_.bytes[3]);
    serial::puts("\n");

    // Netmask
    serial::puts("  Netmask: ");
    serial::put_dec(netmask_.bytes[0]);
    serial::putc('.');
    serial::put_dec(netmask_.bytes[1]);
    serial::putc('.');
    serial::put_dec(netmask_.bytes[2]);
    serial::putc('.');
    serial::put_dec(netmask_.bytes[3]);
    serial::puts("\n");

    // Gateway
    serial::puts("  Gateway: ");
    serial::put_dec(gateway_.bytes[0]);
    serial::putc('.');
    serial::put_dec(gateway_.bytes[1]);
    serial::putc('.');
    serial::put_dec(gateway_.bytes[2]);
    serial::putc('.');
    serial::put_dec(gateway_.bytes[3]);
    serial::puts("\n");

    // DNS
    serial::puts("  DNS: ");
    serial::put_dec(dns_.bytes[0]);
    serial::putc('.');
    serial::put_dec(dns_.bytes[1]);
    serial::putc('.');
    serial::put_dec(dns_.bytes[2]);
    serial::putc('.');
    serial::put_dec(dns_.bytes[3]);
    serial::puts("\n");
}

/** @copydoc net::netif_init */
void netif_init()
{
    serial::puts("[netif] Initializing network interface\n");
    g_netif.init();
    g_netif.print_config();
}

/** @copydoc net::netif */
NetIf &netif()
{
    return g_netif;
}

} // namespace net
