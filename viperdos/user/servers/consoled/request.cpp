//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "request.hpp"
#include "../../syscall.hpp"
#include "console_protocol.hpp"

using namespace console_protocol;

namespace consoled {

// =============================================================================
// Debug Output
// =============================================================================

static void debug_print(const char *msg) {
    sys::print(msg);
}

static void debug_print_dec(uint64_t val) {
    if (val == 0) {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// =============================================================================
// RequestHandler Implementation
// =============================================================================

void RequestHandler::init(TextBuffer *buffer, AnsiParser *parser) {
    m_buffer = buffer;
    m_parser = parser;
}

void RequestHandler::handle(int32_t client_channel,
                            const uint8_t *data,
                            size_t len,
                            uint32_t *handles,
                            uint32_t handle_count) {
    if (len < 4)
        return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type) {
        case CON_WRITE: {
            if (len < sizeof(WriteRequest))
                return;
            auto *req = reinterpret_cast<const WriteRequest *>(data);

            const char *text = reinterpret_cast<const char *>(data + sizeof(WriteRequest));
            size_t text_len = len - sizeof(WriteRequest);
            if (text_len > req->length)
                text_len = req->length;

            m_parser->write(text, text_len);

            // Only send reply if client provided a reply channel
            if (client_channel >= 0) {
                WriteReply reply;
                reply.type = CON_WRITE_REPLY;
                reply.request_id = req->request_id;
                reply.status = 0;
                reply.written = static_cast<uint32_t>(text_len);
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            }
            break;
        }

        case CON_CLEAR: {
            if (len < sizeof(ClearRequest))
                return;
            auto *req = reinterpret_cast<const ClearRequest *>(data);

            m_buffer->clear();
            m_buffer->set_cursor(0, 0);
            m_buffer->redraw_all();

            ClearReply reply;
            reply.type = CON_CLEAR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SET_CURSOR: {
            if (len < sizeof(SetCursorRequest))
                return;
            auto *req = reinterpret_cast<const SetCursorRequest *>(data);

            m_buffer->set_cursor(req->x, req->y);

            SetCursorReply reply;
            reply.type = CON_SET_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_GET_CURSOR: {
            if (len < sizeof(GetCursorRequest))
                return;
            auto *req = reinterpret_cast<const GetCursorRequest *>(data);

            GetCursorReply reply;
            reply.type = CON_GET_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.x = m_buffer->cursor_x();
            reply.y = m_buffer->cursor_y();
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SET_COLORS: {
            if (len < sizeof(SetColorsRequest))
                return;
            auto *req = reinterpret_cast<const SetColorsRequest *>(data);

            m_parser->set_colors(req->foreground, req->background);

            SetColorsReply reply;
            reply.type = CON_SET_COLORS_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_GET_SIZE: {
            if (len < sizeof(GetSizeRequest))
                return;
            auto *req = reinterpret_cast<const GetSizeRequest *>(data);

            GetSizeReply reply;
            reply.type = CON_GET_SIZE_REPLY;
            reply.request_id = req->request_id;
            reply.cols = m_buffer->cols();
            reply.rows = m_buffer->rows();
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SHOW_CURSOR: {
            if (len < sizeof(ShowCursorRequest))
                return;
            auto *req = reinterpret_cast<const ShowCursorRequest *>(data);

            m_buffer->set_cursor_visible(true);

            ShowCursorReply reply;
            reply.type = CON_SHOW_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_HIDE_CURSOR: {
            if (len < sizeof(HideCursorRequest))
                return;
            auto *req = reinterpret_cast<const HideCursorRequest *>(data);

            m_buffer->set_cursor_visible(false);

            HideCursorReply reply;
            reply.type = CON_HIDE_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_CONNECT: {
            // CON_CONNECT is now legacy - input goes through kernel TTY buffer.
            // Just send back console dimensions for compatibility.
            if (len < sizeof(ConnectRequest))
                return;
            auto *req = reinterpret_cast<const ConnectRequest *>(data);

            ConnectReply reply;
            reply.type = CON_CONNECT_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.cols = m_buffer->cols();
            reply.rows = m_buffer->rows();

            // Use handles[0] as reply channel if provided, else use client_channel
            uint32_t reply_channel = (handle_count > 0 && handles[0] != 0xFFFFFFFF)
                                         ? handles[0]
                                         : static_cast<uint32_t>(client_channel);
            sys::channel_send(
                static_cast<int32_t>(reply_channel), &reply, sizeof(reply), nullptr, 0);
            break;
        }

        default:
            debug_print("[consoled] Unknown message type: ");
            debug_print_dec(msg_type);
            debug_print("\n");
            break;
    }
}

} // namespace consoled
