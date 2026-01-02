#pragma once

#include "servers/netd/net_protocol.hpp"
#include "syscall.hpp"

namespace netclient
{

class Client
{
  public:
    Client() = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    ~Client()
    {
        if (event_channel_recv_ >= 0)
        {
            sys::channel_close(event_channel_recv_);
            event_channel_recv_ = -1;
        }
        if (channel_ >= 0)
        {
            sys::channel_close(channel_);
            channel_ = -1;
        }
    }

    i32 connect();
    i32 ensure_events();

    i32 event_channel_recv() const
    {
        return event_channel_recv_;
    }

    // Socket operations
    i32 socket_create(u16 family, u16 sock_type, u32 protocol, u32 *out_socket_id);
    i32 socket_connect(u32 socket_id, u32 ip_be, u16 port_be);
    i64 socket_send(u32 socket_id, const void *data, u32 len);
    i64 socket_recv(u32 socket_id, void *buf, u32 max_len);
    i32 socket_close(u32 socket_id);
    i32 socket_status(u32 socket_id, u32 *out_flags, u32 *out_rx_available);

    // DNS
    i32 dns_resolve(const char *hostname, u32 *out_ip_be);

  private:
    i32 channel_ = -1;            // NETD service channel (send endpoint)
    i32 event_channel_recv_ = -1; // Client-side receive endpoint for NETD events
    u32 next_request_id_ = 1;
};

} // namespace netclient
