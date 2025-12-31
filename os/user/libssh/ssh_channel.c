/**
 * @file ssh_channel.c
 * @brief SSH channel implementation (RFC 4254).
 */

#include "include/ssh.h"
#include "ssh_internal.h"
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#define CHANNEL_WINDOW_SIZE (2 * 1024 * 1024)  /* 2MB window */
#define CHANNEL_MAX_PACKET 32768
#define CHANNEL_BUFFER_SIZE (64 * 1024)

/*=============================================================================
 * Channel Management
 *===========================================================================*/

ssh_channel_t *ssh_channel_new(ssh_session_t *session)
{
    if (!session || session->state != SSH_STATE_AUTHENTICATED) {
        return NULL;
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < SSH_MAX_CHANNELS; i++) {
        if (session->channels[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return NULL;

    ssh_channel_t *channel = calloc(1, sizeof(ssh_channel_t));
    if (!channel) return NULL;

    channel->session = session;
    channel->local_channel = session->next_channel_id++;
    channel->local_window = CHANNEL_WINDOW_SIZE;
    channel->local_maxpacket = CHANNEL_MAX_PACKET;
    channel->state = SSH_CHANSTATE_CLOSED;
    channel->exit_status = -1;

    /* Allocate read buffers */
    channel->read_buf = malloc(CHANNEL_BUFFER_SIZE);
    channel->ext_buf = malloc(CHANNEL_BUFFER_SIZE);
    if (!channel->read_buf || !channel->ext_buf) {
        free(channel->read_buf);
        free(channel->ext_buf);
        free(channel);
        return NULL;
    }
    channel->read_buf_size = CHANNEL_BUFFER_SIZE;
    channel->ext_buf_size = CHANNEL_BUFFER_SIZE;

    session->channels[slot] = channel;
    return channel;
}

void ssh_channel_free(ssh_channel_t *channel)
{
    if (!channel) return;

    /* Remove from session */
    if (channel->session) {
        for (int i = 0; i < SSH_MAX_CHANNELS; i++) {
            if (channel->session->channels[i] == channel) {
                channel->session->channels[i] = NULL;
                break;
            }
        }
    }

    free(channel->read_buf);
    free(channel->ext_buf);
    free(channel);
}

static ssh_channel_t *ssh_channel_find(ssh_session_t *session, uint32_t local_id)
{
    for (int i = 0; i < SSH_MAX_CHANNELS; i++) {
        if (session->channels[i] &&
            session->channels[i]->local_channel == local_id) {
            return session->channels[i];
        }
    }
    return NULL;
}

/* Process incoming channel messages */
static int ssh_channel_process_message(ssh_session_t *session, uint8_t msg_type,
                                        const uint8_t *payload, size_t len)
{
    if (len < 4) return SSH_PROTOCOL_ERROR;

    uint32_t channel_id = ssh_buf_read_u32(payload);
    ssh_channel_t *channel = ssh_channel_find(session, channel_id);

    switch (msg_type) {
        case SSH_MSG_CHANNEL_OPEN_CONFIRMATION: {
            if (!channel || len < 16) return SSH_PROTOCOL_ERROR;
            channel->remote_channel = ssh_buf_read_u32(payload + 4);
            channel->remote_window = ssh_buf_read_u32(payload + 8);
            channel->remote_maxpacket = ssh_buf_read_u32(payload + 12);
            channel->state = SSH_CHANSTATE_OPEN;
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_OPEN_FAILURE: {
            if (!channel || len < 8) return SSH_PROTOCOL_ERROR;
            channel->state = SSH_CHANSTATE_CLOSED;
            return SSH_CHANNEL_CLOSED;
        }

        case SSH_MSG_CHANNEL_WINDOW_ADJUST: {
            if (!channel || len < 8) return SSH_PROTOCOL_ERROR;
            uint32_t bytes = ssh_buf_read_u32(payload + 4);
            channel->remote_window += bytes;
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_DATA: {
            if (!channel || len < 8) return SSH_PROTOCOL_ERROR;
            uint32_t data_len = ssh_buf_read_u32(payload + 4);
            if (8 + data_len > len) return SSH_PROTOCOL_ERROR;

            /* Copy to read buffer */
            size_t space = channel->read_buf_size - channel->read_buf_len;
            size_t copy = (data_len < space) ? data_len : space;
            if (copy > 0) {
                memcpy(channel->read_buf + channel->read_buf_len, payload + 8, copy);
                channel->read_buf_len += copy;
            }

            /* Adjust window */
            channel->local_window -= data_len;
            if (channel->local_window < CHANNEL_WINDOW_SIZE / 2) {
                uint8_t adjust[8];
                ssh_buf_write_u32(adjust, channel->remote_channel);
                ssh_buf_write_u32(adjust + 4, CHANNEL_WINDOW_SIZE - channel->local_window);
                ssh_packet_send(session, SSH_MSG_CHANNEL_WINDOW_ADJUST, adjust, 8);
                channel->local_window = CHANNEL_WINDOW_SIZE;
            }
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_EXTENDED_DATA: {
            if (!channel || len < 12) return SSH_PROTOCOL_ERROR;
            uint32_t data_type = ssh_buf_read_u32(payload + 4);
            uint32_t data_len = ssh_buf_read_u32(payload + 8);
            if (12 + data_len > len) return SSH_PROTOCOL_ERROR;

            /* Only handle stderr (type 1) */
            if (data_type == 1) {
                size_t space = channel->ext_buf_size - channel->ext_buf_len;
                size_t copy = (data_len < space) ? data_len : space;
                if (copy > 0) {
                    memcpy(channel->ext_buf + channel->ext_buf_len, payload + 12, copy);
                    channel->ext_buf_len += copy;
                }
            }

            channel->local_window -= data_len;
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_EOF: {
            if (channel) {
                channel->eof_received = true;
            }
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_CLOSE: {
            if (channel) {
                channel->state = SSH_CHANSTATE_CLOSED;
                /* Send close back if we haven't already */
                if (!channel->eof_sent) {
                    uint8_t close_msg[4];
                    ssh_buf_write_u32(close_msg, channel->remote_channel);
                    ssh_packet_send(session, SSH_MSG_CHANNEL_CLOSE, close_msg, 4);
                }
            }
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_REQUEST: {
            if (!channel || len < 5) return SSH_PROTOCOL_ERROR;
            uint32_t req_len = ssh_buf_read_u32(payload + 4);
            if (8 + req_len > len) return SSH_PROTOCOL_ERROR;

            /* Check for exit-status */
            if (req_len == 11 && memcmp(payload + 8, "exit-status", 11) == 0) {
                if (len >= 8 + 11 + 1 + 4) {
                    channel->exit_status = ssh_buf_read_u32(payload + 8 + 11 + 1);
                    channel->exit_status_set = true;
                }
            }
            return SSH_OK;
        }

        case SSH_MSG_CHANNEL_SUCCESS:
        case SSH_MSG_CHANNEL_FAILURE:
            return SSH_OK;

        default:
            return SSH_OK;
    }
}

/* Wait for channel-related messages, processing them */
static int ssh_channel_wait_open(ssh_channel_t *channel)
{
    ssh_session_t *session = channel->session;
    uint8_t payload[SSH_MAX_PAYLOAD_SIZE];
    size_t payload_len;
    uint8_t msg_type;
    int rc;

    while (channel->state == SSH_CHANSTATE_OPENING) {
        rc = ssh_packet_recv(session, &msg_type, payload, &payload_len);
        if (rc < 0) return rc;

        if (msg_type >= SSH_MSG_CHANNEL_OPEN && msg_type <= SSH_MSG_CHANNEL_FAILURE) {
            rc = ssh_channel_process_message(session, msg_type, payload, payload_len);
            if (rc < 0 && rc != SSH_CHANNEL_CLOSED) return rc;
        }
    }

    return (channel->state == SSH_CHANSTATE_OPEN) ? SSH_OK : SSH_ERROR;
}

int ssh_channel_open_session(ssh_channel_t *channel)
{
    if (!channel || !channel->session) return SSH_ERROR;

    uint8_t payload[256];
    size_t pos = 0;

    /* channel type */
    const char *type = "session";
    size_t type_len = strlen(type);
    ssh_buf_write_u32(payload + pos, type_len);
    memcpy(payload + pos + 4, type, type_len);
    pos += 4 + type_len;

    /* sender channel */
    ssh_buf_write_u32(payload + pos, channel->local_channel);
    pos += 4;

    /* initial window size */
    ssh_buf_write_u32(payload + pos, channel->local_window);
    pos += 4;

    /* maximum packet size */
    ssh_buf_write_u32(payload + pos, channel->local_maxpacket);
    pos += 4;

    channel->state = SSH_CHANSTATE_OPENING;

    int rc = ssh_packet_send(channel->session, SSH_MSG_CHANNEL_OPEN, payload, pos);
    if (rc < 0) return rc;

    return ssh_channel_wait_open(channel);
}

static int ssh_channel_request(ssh_channel_t *channel, const char *request,
                               const uint8_t *data, size_t data_len, int want_reply)
{
    if (!channel || channel->state != SSH_CHANSTATE_OPEN) return SSH_ERROR;

    uint8_t payload[1024];
    size_t pos = 0;

    /* recipient channel */
    ssh_buf_write_u32(payload + pos, channel->remote_channel);
    pos += 4;

    /* request type */
    size_t req_len = strlen(request);
    ssh_buf_write_u32(payload + pos, req_len);
    memcpy(payload + pos + 4, request, req_len);
    pos += 4 + req_len;

    /* want reply */
    payload[pos++] = want_reply ? 1 : 0;

    /* request-specific data */
    if (data && data_len > 0) {
        memcpy(payload + pos, data, data_len);
        pos += data_len;
    }

    int rc = ssh_packet_send(channel->session, SSH_MSG_CHANNEL_REQUEST, payload, pos);
    if (rc < 0) return rc;

    if (want_reply) {
        /* Wait for success/failure */
        uint8_t response[256];
        size_t response_len;
        uint8_t msg_type;

        while (1) {
            rc = ssh_packet_recv(channel->session, &msg_type, response, &response_len);
            if (rc < 0) return rc;

            if (msg_type == SSH_MSG_CHANNEL_SUCCESS) {
                return SSH_OK;
            }
            if (msg_type == SSH_MSG_CHANNEL_FAILURE) {
                return SSH_ERROR;
            }

            /* Process other channel messages */
            if (msg_type >= SSH_MSG_CHANNEL_OPEN && msg_type <= SSH_MSG_CHANNEL_FAILURE) {
                ssh_channel_process_message(channel->session, msg_type, response, response_len);
            }
        }
    }

    return SSH_OK;
}

int ssh_channel_request_pty(ssh_channel_t *channel, const char *term,
                            int cols, int rows)
{
    uint8_t data[256];
    size_t pos = 0;

    /* TERM */
    size_t term_len = strlen(term);
    ssh_buf_write_u32(data + pos, term_len);
    memcpy(data + pos + 4, term, term_len);
    pos += 4 + term_len;

    /* width (chars) */
    ssh_buf_write_u32(data + pos, cols);
    pos += 4;

    /* height (rows) */
    ssh_buf_write_u32(data + pos, rows);
    pos += 4;

    /* width (pixels) */
    ssh_buf_write_u32(data + pos, 0);
    pos += 4;

    /* height (pixels) */
    ssh_buf_write_u32(data + pos, 0);
    pos += 4;

    /* terminal modes (empty) */
    ssh_buf_write_u32(data + pos, 1);
    data[pos + 4] = 0;  /* TTY_OP_END */
    pos += 5;

    return ssh_channel_request(channel, "pty-req", data, pos, 1);
}

int ssh_channel_request_shell(ssh_channel_t *channel)
{
    return ssh_channel_request(channel, "shell", NULL, 0, 1);
}

int ssh_channel_request_exec(ssh_channel_t *channel, const char *command)
{
    uint8_t data[1024];
    size_t pos = 0;

    size_t cmd_len = strlen(command);
    ssh_buf_write_u32(data + pos, cmd_len);
    memcpy(data + pos + 4, command, cmd_len);
    pos += 4 + cmd_len;

    return ssh_channel_request(channel, "exec", data, pos, 1);
}

int ssh_channel_request_subsystem(ssh_channel_t *channel, const char *subsystem)
{
    uint8_t data[256];
    size_t pos = 0;

    size_t sub_len = strlen(subsystem);
    ssh_buf_write_u32(data + pos, sub_len);
    memcpy(data + pos + 4, subsystem, sub_len);
    pos += 4 + sub_len;

    return ssh_channel_request(channel, "subsystem", data, pos, 1);
}

ssize_t ssh_channel_write(ssh_channel_t *channel, const void *data, size_t len)
{
    if (!channel || channel->state != SSH_CHANSTATE_OPEN || !data) {
        return SSH_ERROR;
    }

    const uint8_t *ptr = data;
    size_t remaining = len;
    size_t total_sent = 0;

    while (remaining > 0) {
        /* Wait for window space */
        while (channel->remote_window == 0 && channel->state == SSH_CHANSTATE_OPEN) {
            /* Need to read and process incoming packets */
            uint8_t payload[SSH_MAX_PAYLOAD_SIZE];
            size_t payload_len;
            uint8_t msg_type;

            int rc = ssh_packet_recv(channel->session, &msg_type, payload, &payload_len);
            if (rc < 0) return rc;

            if (msg_type >= SSH_MSG_CHANNEL_OPEN && msg_type <= SSH_MSG_CHANNEL_FAILURE) {
                ssh_channel_process_message(channel->session, msg_type, payload, payload_len);
            }
        }

        if (channel->state != SSH_CHANSTATE_OPEN) {
            return total_sent > 0 ? (ssize_t)total_sent : SSH_CHANNEL_CLOSED;
        }

        /* Send data packet */
        size_t chunk = remaining;
        if (chunk > channel->remote_window) chunk = channel->remote_window;
        if (chunk > channel->remote_maxpacket) chunk = channel->remote_maxpacket;

        uint8_t pkt[SSH_MAX_PAYLOAD_SIZE];
        ssh_buf_write_u32(pkt, channel->remote_channel);
        ssh_buf_write_u32(pkt + 4, chunk);
        memcpy(pkt + 8, ptr, chunk);

        int rc = ssh_packet_send(channel->session, SSH_MSG_CHANNEL_DATA, pkt, 8 + chunk);
        if (rc < 0) return rc;

        channel->remote_window -= chunk;
        ptr += chunk;
        remaining -= chunk;
        total_sent += chunk;
    }

    return total_sent;
}

ssize_t ssh_channel_read(ssh_channel_t *channel, void *buffer, size_t len, int *is_stderr)
{
    if (!channel || !buffer) return SSH_ERROR;

    if (is_stderr) *is_stderr = 0;

    /* First check buffers */
    if (channel->read_buf_len > channel->read_buf_pos) {
        size_t avail = channel->read_buf_len - channel->read_buf_pos;
        size_t copy = (avail < len) ? avail : len;
        memcpy(buffer, channel->read_buf + channel->read_buf_pos, copy);
        channel->read_buf_pos += copy;

        /* Reset buffer if fully consumed */
        if (channel->read_buf_pos >= channel->read_buf_len) {
            channel->read_buf_pos = 0;
            channel->read_buf_len = 0;
        }
        return copy;
    }

    /* Check stderr buffer */
    if (channel->ext_buf_len > channel->ext_buf_pos) {
        size_t avail = channel->ext_buf_len - channel->ext_buf_pos;
        size_t copy = (avail < len) ? avail : len;
        memcpy(buffer, channel->ext_buf + channel->ext_buf_pos, copy);
        channel->ext_buf_pos += copy;

        if (channel->ext_buf_pos >= channel->ext_buf_len) {
            channel->ext_buf_pos = 0;
            channel->ext_buf_len = 0;
        }
        if (is_stderr) *is_stderr = 1;
        return copy;
    }

    /* Check for EOF */
    if (channel->eof_received || channel->state == SSH_CHANSTATE_CLOSED) {
        return 0;
    }

    /* Need to read from network */
    uint8_t payload[SSH_MAX_PAYLOAD_SIZE];
    size_t payload_len;
    uint8_t msg_type;

    int rc = ssh_packet_recv(channel->session, &msg_type, payload, &payload_len);
    if (rc < 0) return rc;

    if (msg_type >= SSH_MSG_CHANNEL_OPEN && msg_type <= SSH_MSG_CHANNEL_FAILURE) {
        ssh_channel_process_message(channel->session, msg_type, payload, payload_len);
    }

    /* Try again with potentially filled buffer */
    if (channel->read_buf_len > 0 || channel->ext_buf_len > 0) {
        return ssh_channel_read(channel, buffer, len, is_stderr);
    }

    if (channel->eof_received || channel->state == SSH_CHANSTATE_CLOSED) {
        return 0;
    }

    return SSH_AGAIN;
}

int ssh_channel_send_eof(ssh_channel_t *channel)
{
    if (!channel || channel->state != SSH_CHANSTATE_OPEN || channel->eof_sent) {
        return SSH_ERROR;
    }

    uint8_t payload[4];
    ssh_buf_write_u32(payload, channel->remote_channel);

    int rc = ssh_packet_send(channel->session, SSH_MSG_CHANNEL_EOF, payload, 4);
    if (rc >= 0) {
        channel->eof_sent = true;
    }
    return rc;
}

int ssh_channel_close(ssh_channel_t *channel)
{
    if (!channel) return SSH_ERROR;

    if (channel->state == SSH_CHANSTATE_OPEN && !channel->eof_sent) {
        ssh_channel_send_eof(channel);
    }

    if (channel->state != SSH_CHANSTATE_CLOSED) {
        uint8_t payload[4];
        ssh_buf_write_u32(payload, channel->remote_channel);
        ssh_packet_send(channel->session, SSH_MSG_CHANNEL_CLOSE, payload, 4);
        channel->state = SSH_CHANSTATE_CLOSED;
    }

    return SSH_OK;
}

int ssh_channel_is_open(ssh_channel_t *channel)
{
    return channel && channel->state == SSH_CHANSTATE_OPEN;
}

int ssh_channel_is_eof(ssh_channel_t *channel)
{
    return channel && channel->eof_received;
}

int ssh_channel_get_exit_status(ssh_channel_t *channel)
{
    if (!channel || !channel->exit_status_set) return -1;
    return channel->exit_status;
}

int ssh_channel_change_pty_size(ssh_channel_t *channel, int cols, int rows)
{
    uint8_t data[16];
    size_t pos = 0;

    ssh_buf_write_u32(data + pos, cols);
    pos += 4;
    ssh_buf_write_u32(data + pos, rows);
    pos += 4;
    ssh_buf_write_u32(data + pos, 0);  /* width pixels */
    pos += 4;
    ssh_buf_write_u32(data + pos, 0);  /* height pixels */
    pos += 4;

    return ssh_channel_request(channel, "window-change", data, pos, 0);
}

int ssh_channel_poll(ssh_channel_t *channel, int timeout_ms)
{
    if (!channel) return SSH_ERROR;

    /* Check if data already in buffer */
    if (channel->read_buf_len > channel->read_buf_pos ||
        channel->ext_buf_len > channel->ext_buf_pos) {
        return 1;
    }

    if (channel->eof_received || channel->state == SSH_CHANSTATE_CLOSED) {
        return 0;
    }

    /* Poll the socket */
    struct pollfd pfd;
    pfd.fd = channel->session->socket_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0) return SSH_ERROR;
    if (rc == 0) return 0;  /* Timeout */

    /* Data available - process it */
    uint8_t payload[SSH_MAX_PAYLOAD_SIZE];
    size_t payload_len;
    uint8_t msg_type;

    rc = ssh_packet_recv(channel->session, &msg_type, payload, &payload_len);
    if (rc < 0) return rc;

    if (msg_type >= SSH_MSG_CHANNEL_OPEN && msg_type <= SSH_MSG_CHANNEL_FAILURE) {
        ssh_channel_process_message(channel->session, msg_type, payload, payload_len);
    }

    /* Check again */
    if (channel->read_buf_len > channel->read_buf_pos ||
        channel->ext_buf_len > channel->ext_buf_pos) {
        return 1;
    }

    return 0;
}
