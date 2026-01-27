//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ansi.hpp"
#include "text_buffer.hpp"  // Includes gui.h which has stdint types

namespace consoled {

// =============================================================================
// RequestHandler Class
// =============================================================================

/**
 * @brief Handles IPC requests from clients.
 *
 * Processes console protocol messages (write, clear, cursor, etc.)
 * and sends appropriate responses.
 */
class RequestHandler {
public:
    RequestHandler() = default;

    /**
     * @brief Initialize with buffer and parser.
     */
    void init(TextBuffer *buffer, AnsiParser *parser);

    /**
     * @brief Handle an incoming IPC request.
     * @param client_channel Channel to send replies (or -1)
     * @param data Request data
     * @param len Data length
     * @param handles Passed handles
     * @param handle_count Number of handles
     */
    void handle(int32_t client_channel,
                const uint8_t *data,
                size_t len,
                uint32_t *handles,
                uint32_t handle_count);

private:
    TextBuffer *m_buffer = nullptr;
    AnsiParser *m_parser = nullptr;
};

} // namespace consoled
