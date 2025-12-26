#pragma once

/**
 * @file netif.hpp
 * @brief Network interface configuration used by the network stack.
 *
 * @details
 * ViperOS' early network stack currently assumes a single network interface
 * backed by the virtio-net driver. This header defines a small configuration
 * object (@ref net::NetIf) that stores:
 * - The interface MAC address.
 * - The local IPv4 address and subnet mask.
 * - The default gateway and DNS server.
 *
 * Routing is minimal: destinations on the local subnet are delivered directly;
 * everything else is routed via the configured gateway.
 */

#include "net.hpp"

namespace net
{

/**
 * @brief Network interface configuration.
 *
 * @details
 * Holds the configuration values required by the Ethernet/IP layers. The class
 * does not perform dynamic configuration (DHCP); instead it is initialized with
 * QEMU user-mode networking defaults and optionally a MAC address obtained from
 * the virtio-net device.
 *
 * This is a simple value holder used during bring-up and is not currently
 * synchronized for concurrent access.
 */
class NetIf
{
  public:
    /**
     * @brief Initialize the interface configuration.
     *
     * @details
     * Populates the MAC address from the virtio-net device if present, then
     * assigns default static IPv4 configuration values suitable for QEMU's
     * user-mode networking:
     * - IP: 10.0.2.15
     * - Netmask: 255.255.255.0
     * - Gateway: 10.0.2.2
     * - DNS: 10.0.2.3
     */
    void init();

    // Get/set configuration
    /** @brief Get the interface MAC address. */
    MacAddr mac() const
    {
        return mac_;
    }

    /** @brief Get the local IPv4 address. */
    Ipv4Addr ip() const
    {
        return ip_;
    }

    /** @brief Get the subnet mask. */
    Ipv4Addr netmask() const
    {
        return netmask_;
    }

    /** @brief Get the default gateway IPv4 address. */
    Ipv4Addr gateway() const
    {
        return gateway_;
    }

    /** @brief Get the configured DNS server IPv4 address. */
    Ipv4Addr dns() const
    {
        return dns_;
    }

    /** @brief Set the interface MAC address. */
    void set_mac(const MacAddr &mac)
    {
        mac_ = mac;
    }

    /** @brief Set the local IPv4 address. */
    void set_ip(const Ipv4Addr &ip)
    {
        ip_ = ip;
    }

    /** @brief Set the subnet mask. */
    void set_netmask(const Ipv4Addr &mask)
    {
        netmask_ = mask;
    }

    /** @brief Set the default gateway. */
    void set_gateway(const Ipv4Addr &gw)
    {
        gateway_ = gw;
    }

    /** @brief Set the DNS server address. */
    void set_dns(const Ipv4Addr &dns)
    {
        dns_ = dns;
    }

    // Check if address is on local subnet
    /**
     * @brief Check whether a destination address is on the local subnet.
     *
     * @details
     * Uses the configured local IP address and netmask to determine whether
     * packets to `addr` should be delivered directly at Layer 2.
     *
     * @param addr Destination IPv4 address.
     * @return `true` if the destination is in the same subnet, otherwise `false`.
     */
    bool is_local(const Ipv4Addr &addr) const
    {
        return ip_.same_subnet(addr, netmask_);
    }

    // Get next hop for destination
    /**
     * @brief Determine the next-hop IPv4 address for a destination.
     *
     * @details
     * If the destination is local (as determined by @ref is_local), the next hop
     * is the destination itself. Otherwise, packets are routed via the configured
     * default gateway.
     *
     * @param dest Destination IPv4 address.
     * @return Next-hop IPv4 address to use for ARP resolution.
     */
    Ipv4Addr next_hop(const Ipv4Addr &dest) const
    {
        if (is_local(dest))
        {
            return dest; // Direct delivery
        }
        return gateway_; // Route via gateway
    }

    // Print configuration
    /**
     * @brief Print the current configuration to the serial console.
     *
     * @details
     * Emits MAC address, IP address, netmask, gateway and DNS values. Intended
     * for debugging and boot-time diagnostics.
     */
    void print_config() const;

  private:
    MacAddr mac_;
    u8 pad_[2]; // Padding to align ip_ to 4-byte boundary
    Ipv4Addr ip_;
    Ipv4Addr netmask_;
    Ipv4Addr gateway_;
    Ipv4Addr dns_;
};

/**
 * @brief Initialize the global network interface configuration.
 *
 * @details
 * Initializes the singleton @ref NetIf instance and prints the resulting
 * configuration.
 */
void netif_init();

/**
 * @brief Access the global network interface configuration.
 *
 * @details
 * Returns a reference to the singleton @ref NetIf used by the network stack.
 *
 * @return Reference to the global NetIf.
 */
NetIf &netif();

} // namespace net
