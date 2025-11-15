//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/term/input.cpp
// Purpose: Decode raw terminal byte streams into structured key, mouse, and
//          paste events for ViperTUI applications.
// Key invariants: Partial UTF-8 sequences survive across feed calls and escape
//                 state machines preserve queued parameters until completion.
// Ownership/Lifetime: The decoder owns event buffers but borrows incoming byte
//                     spans supplied by the caller.
// Links: docs/tools.md#input-decoder
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the terminal input decoder responsible for translating
///        raw escape sequences into higher-level events.
/// @details The decoder maintains a small finite-state machine capable of
///          understanding UTF-8 code points, CSI/SS3 sequences, mouse reporting,
///          and bracketed paste mode.  Callers feed arbitrary byte ranges and
///          drain accumulated events when ready to process them.

#include "tui/term/input.hpp"

#include <string_view>

namespace viper::tui::term
{

/// @brief Construct an input decoder with empty event buffers.
/// @details Initialises the CSI parser with references to the decoder's
///          internal event storage so that parsed escape sequences can append
///          events directly.
InputDecoder::InputDecoder() : csi_parser_(key_events_, mouse_events_, paste_buf_) {}

/// @brief Translate a Unicode code point into a @ref KeyEvent.
/// @details ASCII control characters are mapped to dedicated key codes while
///          printable characters populate the @c codepoint field.  Unknown
///          control codes are surfaced as @c Code::Unknown to keep clients aware
///          of unusual sequences.
/// @param cp Code point produced by the UTF-8 decoder.
void InputDecoder::emit(uint32_t cp)
{
    KeyEvent ev{};
    switch (cp)
    {
        case '\r':
        case '\n':
            ev.code = KeyEvent::Code::Enter;
            break;
        case '\t':
            ev.code = KeyEvent::Code::Tab;
            break;
        case 0x1b:
            ev.code = KeyEvent::Code::Esc;
            break;
        case 0x7f:
            ev.code = KeyEvent::Code::Backspace;
            break;
        default:
            if (cp >= 0x20)
            {
                ev.codepoint = cp;
            }
            else
            {
                ev.code = KeyEvent::Code::Unknown;
            }
            break;
    }
    key_events_.push_back(ev);
}

/// @brief Process the final byte of a CSI sequence.
/// @details Delegates to @ref CsiParser::handle to populate key, mouse, or paste
///          events and transitions the decoder state machine into paste mode
///          when OSC 200/201 markers are observed.
/// @param final Final character identifying the CSI command.
/// @param params Raw parameter substring captured prior to @p final.
/// @return The new decoder state following CSI handling.
InputDecoder::State InputDecoder::handle_csi(char final, std::string_view params)
{
    auto result = csi_parser_.handle(final, params);
    if (result.start_paste)
    {
        return State::Paste;
    }
    return State::Utf8;
}

/// @brief Handle SS3 escape sequences used for legacy function keys.
/// @details SS3 sequences report modifiers in the same format as CSI, so the
///          implementation reuses @ref CsiParser facilities for parameter
///          decoding before emitting the resulting key event.
/// @param final Terminator indicating which key was pressed.
/// @param params Parameter substring that may include modifier information.
void InputDecoder::handle_ss3(char final, std::string_view params)
{
    auto nums = csi_parser_.parse_params(params);
    unsigned mods = 0;
    if (nums.size() >= 2)
    {
        mods = csi_parser_.decode_mod(nums[1]);
    }

    KeyEvent ev{};
    ev.mods = mods;
    switch (final)
    {
        case 'A':
            ev.code = KeyEvent::Code::Up;
            break;
        case 'B':
            ev.code = KeyEvent::Code::Down;
            break;
        case 'C':
            ev.code = KeyEvent::Code::Right;
            break;
        case 'D':
            ev.code = KeyEvent::Code::Left;
            break;
        case 'H':
            ev.code = KeyEvent::Code::Home;
            break;
        case 'F':
            ev.code = KeyEvent::Code::End;
            break;
        case 'P':
            ev.code = KeyEvent::Code::F1;
            break;
        case 'Q':
            ev.code = KeyEvent::Code::F2;
            break;
        case 'R':
            ev.code = KeyEvent::Code::F3;
            break;
        case 'S':
            ev.code = KeyEvent::Code::F4;
            break;
        default:
            return;
    }
    key_events_.push_back(ev);
}

/// @brief Feed raw terminal bytes into the decoder state machine.
/// @details Iterates over each byte, distinguishing UTF-8 payloads from escape
///          sequences, and buffers intermediate data until complete events are
///          formed.  Bracketed paste regions are collected verbatim until the
///          closing terminator arrives.
/// @param bytes Chunk of terminal output to decode.
void InputDecoder::feed(std::string_view bytes)
{
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        unsigned char b = static_cast<unsigned char>(bytes[i]);
        switch (state_)
        {
            case State::Utf8:
            {
                if (utf8_decoder_.idle() && b == 0x1b)
                {
                    state_ = State::Esc;
                }
                else
                {
                    Utf8Result result = utf8_decoder_.feed(b);
                    if (result.has_codepoint)
                    {
                        emit(result.codepoint);
                    }
                    if (result.error)
                    {
                        key_events_.push_back(KeyEvent{});
                    }
                    if (result.replay)
                    {
                        --i;
                    }
                }
                break;
            }
            case State::Esc:
                if (b == '[')
                {
                    state_ = State::CSI;
                    seq_.clear();
                }
                else if (b == 'O')
                {
                    state_ = State::SS3;
                    seq_.clear();
                }
                else
                {
                    emit(0x1b);
                    state_ = State::Utf8;
                    --i;
                }
                break;
            case State::CSI:
                if (b >= 0x40 && b <= 0x7E)
                {
                    state_ = handle_csi(static_cast<char>(b), seq_);
                    seq_.clear();
                }
                else
                {
                    seq_.push_back(static_cast<char>(b));
                }
                break;
            case State::SS3:
                if (b >= 0x40 && b <= 0x7E)
                {
                    handle_ss3(static_cast<char>(b), seq_);
                    state_ = State::Utf8;
                    seq_.clear();
                }
                else
                {
                    seq_.push_back(static_cast<char>(b));
                }
                break;
            case State::Paste:
                if (b == 0x1b)
                {
                    state_ = State::PasteEsc;
                }
                else
                {
                    paste_buf_.push_back(static_cast<char>(b));
                }
                break;
            case State::PasteEsc:
                if (b == '[')
                {
                    state_ = State::PasteCSI;
                    seq_.clear();
                }
                else
                {
                    paste_buf_.push_back('\x1b');
                    paste_buf_.push_back(static_cast<char>(b));
                    state_ = State::Paste;
                }
                break;
            case State::PasteCSI:
                if (b >= 0x40 && b <= 0x7E)
                {
                    if (b == '~' && seq_ == "201")
                    {
                        PasteEvent ev{};
                        ev.text = std::move(paste_buf_);
                        paste_events_.push_back(std::move(ev));
                        paste_buf_.clear();
                        state_ = State::Utf8;
                    }
                    else
                    {
                        paste_buf_.append("\x1b[");
                        paste_buf_.append(seq_);
                        paste_buf_.push_back(static_cast<char>(b));
                        state_ = State::Paste;
                    }
                    seq_.clear();
                }
                else
                {
                    seq_.push_back(static_cast<char>(b));
                }
                break;
        }
    }
}

/// @brief Retrieve and clear the pending key events.
/// @details Moves the accumulated vector out of the decoder, leaving the
///          internal storage empty for future feeds.
/// @return Sequence of key events ready for consumption.
std::vector<KeyEvent> InputDecoder::drain()
{
    auto out = std::move(key_events_);
    key_events_.clear();
    return out;
}

/// @brief Retrieve decoded mouse events.
/// @details Works like @ref drain but operates on the mouse event buffer.
/// @return Vector of mouse events produced since the last drain.
std::vector<MouseEvent> InputDecoder::drain_mouse()
{
    auto out = std::move(mouse_events_);
    mouse_events_.clear();
    return out;
}

/// @brief Retrieve accumulated bracketed paste payloads.
/// @details Returns any completed paste operations gathered from OSC 200/201
///          sequences and clears the internal buffer.
/// @return Vector of paste events ready for processing.
std::vector<PasteEvent> InputDecoder::drain_paste()
{
    auto out = std::move(paste_events_);
    paste_events_.clear();
    return out;
}

} // namespace viper::tui::term
