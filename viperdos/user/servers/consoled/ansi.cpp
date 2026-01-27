//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "ansi.hpp"
#include "../../include/viper_colors.h"

namespace consoled {

// =============================================================================
// ANSI Color Tables
// =============================================================================

// Standard ANSI colors (index 0-7)
static constexpr uint32_t ansi_colors[8] = {
    ANSI_COLOR_BLACK,
    ANSI_COLOR_RED,
    ANSI_COLOR_GREEN,
    ANSI_COLOR_YELLOW,
    ANSI_COLOR_BLUE,
    ANSI_COLOR_MAGENTA,
    ANSI_COLOR_CYAN,
    ANSI_COLOR_WHITE,
};

// Bright ANSI colors (index 8-15)
static constexpr uint32_t ansi_bright_colors[8] = {
    ANSI_COLOR_BRIGHT_BLACK,
    ANSI_COLOR_BRIGHT_RED,
    ANSI_COLOR_BRIGHT_GREEN,
    ANSI_COLOR_BRIGHT_YELLOW,
    ANSI_COLOR_BRIGHT_BLUE,
    ANSI_COLOR_BRIGHT_MAGENTA,
    ANSI_COLOR_BRIGHT_CYAN,
    ANSI_COLOR_BRIGHT_WHITE,
};

// =============================================================================
// Initialization
// =============================================================================

void AnsiParser::init(TextBuffer *buffer, uint32_t default_fg, uint32_t default_bg) {
    m_buffer = buffer;
    m_default_fg = default_fg;
    m_default_bg = default_bg;
    m_fg_color = default_fg;
    m_bg_color = default_bg;
}

void AnsiParser::reset_colors() {
    m_fg_color = m_default_fg;
    m_bg_color = m_default_bg;
    m_bold_mode = false;
    m_reverse_mode = false;
}

// =============================================================================
// CSI Parameter Handling
// =============================================================================

void AnsiParser::csi_reset() {
    m_csi_param_count = 0;
    m_csi_current_param = 0;
    m_csi_has_param = false;
    for (size_t i = 0; i < CSI_MAX_PARAMS; i++)
        m_csi_params[i] = 0;
}

void AnsiParser::csi_push_param() {
    if (m_csi_param_count < CSI_MAX_PARAMS) {
        m_csi_params[m_csi_param_count++] = m_csi_has_param ? m_csi_current_param : 0;
    }
    m_csi_current_param = 0;
    m_csi_has_param = false;
}

uint32_t AnsiParser::csi_get_param(size_t index, uint32_t default_val) {
    if (index < m_csi_param_count && m_csi_params[index] > 0)
        return m_csi_params[index];
    return default_val;
}

// =============================================================================
// SGR (Select Graphic Rendition) - Colors and Attributes
// =============================================================================

void AnsiParser::handle_sgr() {
    // If no parameters, treat as reset
    if (m_csi_param_count == 0) {
        reset_colors();
        return;
    }

    for (size_t i = 0; i < m_csi_param_count; i++) {
        uint32_t param = m_csi_params[i];

        switch (param) {
            case 0: // Reset all attributes
                reset_colors();
                break;

            case 1: // Bold / bright
                m_bold_mode = true;
                break;

            case 7: // Reverse video
                m_reverse_mode = true;
                break;

            case 22: // Normal intensity (not bold)
                m_bold_mode = false;
                break;

            case 27: // Reverse off
                m_reverse_mode = false;
                break;

            // Foreground colors 30-37
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                if (m_bold_mode)
                    m_fg_color = ansi_bright_colors[param - 30];
                else
                    m_fg_color = ansi_colors[param - 30];
                break;

            case 39: // Default foreground
                m_fg_color = m_default_fg;
                break;

            // Background colors 40-47
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                m_bg_color = ansi_colors[param - 40];
                break;

            case 49: // Default background
                m_bg_color = m_default_bg;
                break;

            // Bright foreground colors 90-97
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                m_fg_color = ansi_bright_colors[param - 90];
                break;

            // Bright background colors 100-107
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                m_bg_color = ansi_bright_colors[param - 100];
                break;

            default:
                // Ignore unknown SGR parameters
                break;
        }
    }
}

// =============================================================================
// CSI Sequence Handler
// =============================================================================

void AnsiParser::handle_csi(char final_char) {
    // Push the last parameter if there is one
    if (m_csi_has_param || m_csi_param_count > 0)
        csi_push_param();

    uint32_t n, m;

    switch (final_char) {
        case 'A': // Cursor Up
            n = csi_get_param(0, 1);
            m_buffer->move_cursor(0, -static_cast<int32_t>(n));
            break;

        case 'B': // Cursor Down
            n = csi_get_param(0, 1);
            m_buffer->move_cursor(0, static_cast<int32_t>(n));
            break;

        case 'C': // Cursor Forward
            n = csi_get_param(0, 1);
            m_buffer->move_cursor(static_cast<int32_t>(n), 0);
            break;

        case 'D': // Cursor Back
            n = csi_get_param(0, 1);
            m_buffer->move_cursor(-static_cast<int32_t>(n), 0);
            break;

        case 'H': // Cursor Position (also 'f')
        case 'f':
            n = csi_get_param(0, 1); // Row (1-based)
            m = csi_get_param(1, 1); // Column (1-based)
            // Convert to 0-based
            m_buffer->set_cursor((m > 0) ? m - 1 : 0, (n > 0) ? n - 1 : 0);
            break;

        case 'J': // Erase in Display
            n = csi_get_param(0, 0);
            m_buffer->set_colors(m_fg_color, m_bg_color);
            switch (n) {
                case 0:
                    m_buffer->clear_to_eos();
                    break;
                case 1:
                    m_buffer->clear_to_bos();
                    break;
                case 2: // Clear entire screen
                case 3: // Clear entire screen and scrollback (treat same as 2)
                    m_buffer->clear();
                    m_buffer->set_cursor(0, 0);
                    m_buffer->redraw_all();
                    break;
            }
            break;

        case 'K': // Erase in Line
            n = csi_get_param(0, 0);
            m_buffer->set_colors(m_fg_color, m_bg_color);
            switch (n) {
                case 0:
                    m_buffer->clear_to_eol();
                    break;
                case 1:
                    m_buffer->clear_to_bol();
                    break;
                case 2:
                    m_buffer->clear_line();
                    break;
            }
            break;

        case 'm': // SGR (Select Graphic Rendition)
            handle_sgr();
            break;

        case 's': // Save cursor position
            m_buffer->save_cursor();
            break;

        case 'u': // Restore cursor position
            m_buffer->restore_cursor();
            break;

        case 'n': // Device Status Report
            // We ignore DSR requests for now
            break;

        default:
            // Unknown CSI sequence - ignore
            break;
    }
}

// =============================================================================
// Private CSI Sequence Handler (ESC[?...)
// =============================================================================

void AnsiParser::handle_csi_private(char final_char) {
    // Push the last parameter if there is one
    if (m_csi_has_param || m_csi_param_count > 0)
        csi_push_param();

    uint32_t n = csi_get_param(0, 0);

    switch (final_char) {
        case 'h': // Set Mode
            if (n == 25) {
                // Show cursor
                m_buffer->set_cursor_visible(true);
            }
            break;

        case 'l': // Reset Mode
            if (n == 25) {
                // Hide cursor
                m_buffer->set_cursor_visible(false);
            }
            break;

        default:
            // Unknown private sequence - ignore
            break;
    }
}

// =============================================================================
// Output Helpers
// =============================================================================

void AnsiParser::putchar_at_cursor(char ch) {
    m_buffer->set_colors(m_fg_color, m_bg_color);
    m_buffer->putchar(ch);
}

void AnsiParser::newline() {
    m_buffer->newline();
}

// =============================================================================
// Main Write Function
// =============================================================================

void AnsiParser::write(const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\0')
            break;

        // ANSI escape sequence handling
        switch (m_state) {
            case AnsiState::Normal:
                if (c == '\x1B') {
                    m_state = AnsiState::Esc;
                    continue;
                }
                break;

            case AnsiState::Esc:
                if (c == '[') {
                    m_state = AnsiState::Csi;
                    csi_reset();
                    continue;
                } else if (c == ']') {
                    m_state = AnsiState::Osc;
                    continue;
                } else {
                    // Unknown escape, return to normal
                    m_state = AnsiState::Normal;
                    continue;
                }

            case AnsiState::Csi:
                // Check for private sequence indicator
                if (c == '?') {
                    m_state = AnsiState::CsiPriv;
                    continue;
                }
                // Collect parameters
                if (c >= '0' && c <= '9') {
                    m_csi_current_param = m_csi_current_param * 10 + (c - '0');
                    m_csi_has_param = true;
                    continue;
                }
                if (c == ';') {
                    csi_push_param();
                    continue;
                }
                // Final character (0x40-0x7E)
                if (c >= 0x40 && c <= 0x7E) {
                    handle_csi(c);
                    m_state = AnsiState::Normal;
                    continue;
                }
                // Intermediate bytes (0x20-0x2F) - ignore for now
                if (c >= 0x20 && c <= 0x2F) {
                    continue;
                }
                // Unknown - abort sequence
                m_state = AnsiState::Normal;
                continue;

            case AnsiState::CsiPriv:
                // Collect parameters
                if (c >= '0' && c <= '9') {
                    m_csi_current_param = m_csi_current_param * 10 + (c - '0');
                    m_csi_has_param = true;
                    continue;
                }
                if (c == ';') {
                    csi_push_param();
                    continue;
                }
                // Final character
                if (c >= 0x40 && c <= 0x7E) {
                    handle_csi_private(c);
                    m_state = AnsiState::Normal;
                    continue;
                }
                // Unknown - abort sequence
                m_state = AnsiState::Normal;
                continue;

            case AnsiState::Osc:
                // OSC sequence ends with BEL or ST (ESC \)
                if (c == '\x07' || c == '\\') {
                    m_state = AnsiState::Normal;
                }
                continue;
        }

        // Handle control characters and printable text
        if (c == '\n') {
            newline();
        } else if (c == '\r') {
            m_buffer->carriage_return();
        } else if (c == '\t') {
            m_buffer->tab();
        } else if (c == '\b') {
            m_buffer->backspace();
        } else if (c >= 0x20 && c < 0x7F) {
            putchar_at_cursor(c);
        }
    }
}

} // namespace consoled
