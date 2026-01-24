#pragma once
//===----------------------------------------------------------------------===//
// Clock UI rendering
//===----------------------------------------------------------------------===//

#include "clock.hpp"
#include <gui.h>
#include <stdint.h>

namespace clockapp {

// Colors
namespace colors {
constexpr uint32_t BACKGROUND = 0xFFAAAAAA;
constexpr uint32_t FACE = 0xFFFFFFFF;
constexpr uint32_t FACE_BORDER = 0xFF555555;
constexpr uint32_t HOUR_MARKS = 0xFF000000;
constexpr uint32_t HOUR_HAND = 0xFF000000;
constexpr uint32_t MINUTE_HAND = 0xFF333333;
constexpr uint32_t SECOND_HAND = 0xFFCC0000;
constexpr uint32_t CENTER_DOT = 0xFF000000;
constexpr uint32_t TEXT = 0xFF000000;
constexpr uint32_t DIGITAL_BG = 0xFF222222;
constexpr uint32_t DIGITAL_TEXT = 0xFF00FF00;
} // namespace colors

// Dimensions
namespace dims {
constexpr int WIN_WIDTH = 200;
constexpr int WIN_HEIGHT = 240;
constexpr int CLOCK_CENTER_X = 100;
constexpr int CLOCK_CENTER_Y = 100;
constexpr int CLOCK_RADIUS = 80;
constexpr int HOUR_HAND_LENGTH = 40;
constexpr int MINUTE_HAND_LENGTH = 60;
constexpr int SECOND_HAND_LENGTH = 65;
constexpr int DIGITAL_Y = 200;
constexpr int DATE_Y = 220;
} // namespace dims

// UI class
class UI {
  public:
    UI(gui_window_t *win);

    // Rendering
    void render(const Time &time);

    // Toggle 12/24 hour mode
    void toggle24Hour() { m_24hour = !m_24hour; }
    bool is24Hour() const { return m_24hour; }

  private:
    void drawBackground();
    void drawClockFace();
    void drawHourMarks();
    void drawHands(const Time &time);
    void drawHand(int angle, int length, int thickness, uint32_t color);
    void drawDigitalTime(const Time &time);
    void drawDate(const Time &time);

    gui_window_t *m_win;
    bool m_24hour;
};

} // namespace clockapp
