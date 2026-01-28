//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "text_buffer.hpp" // Includes gui.h which has stdint types

namespace consoled {

// =============================================================================
// ANSI Parser States
// =============================================================================

enum class AnsiState {
    Normal,
    Esc,     // Saw ESC
    Csi,     // Saw ESC[
    CsiPriv, // Saw ESC[? (private sequence)
    Osc,     // Saw ESC]
};

// =============================================================================
// AnsiParser Class
// =============================================================================

class AnsiParser {
  public:
    AnsiParser() = default;

    // Initialize with a text buffer to write to
    void init(TextBuffer *buffer, uint32_t default_fg, uint32_t default_bg);

    // Process text with ANSI escape sequences
    void write(const char *text, size_t len);

    // Color accessors
    uint32_t fg_color() const {
        return m_fg_color;
    }

    uint32_t bg_color() const {
        return m_bg_color;
    }

    void set_colors(uint32_t fg, uint32_t bg) {
        m_fg_color = fg;
        m_bg_color = bg;
    }

    void reset_colors();

    // Mode accessors
    bool bold_mode() const {
        return m_bold_mode;
    }

    bool reverse_mode() const {
        return m_reverse_mode;
    }

  private:
    // CSI parameter handling
    void csi_reset();
    void csi_push_param();
    uint32_t csi_get_param(size_t index, uint32_t default_val);

    // Sequence handlers
    void handle_sgr();
    void handle_csi(char final_char);
    void handle_csi_private(char final_char);

    // Output helpers
    void putchar_at_cursor(char ch);
    void newline();

    TextBuffer *m_buffer = nullptr;

    // Parser state
    AnsiState m_state = AnsiState::Normal;

    // CSI parameter buffer
    static constexpr size_t CSI_MAX_PARAMS = 8;
    uint32_t m_csi_params[CSI_MAX_PARAMS] = {};
    size_t m_csi_param_count = 0;
    uint32_t m_csi_current_param = 0;
    bool m_csi_has_param = false;

    // Colors
    uint32_t m_fg_color = 0;
    uint32_t m_bg_color = 0;
    uint32_t m_default_fg = 0;
    uint32_t m_default_bg = 0;

    // Modes
    bool m_bold_mode = false;
    bool m_reverse_mode = false;
};

} // namespace consoled
