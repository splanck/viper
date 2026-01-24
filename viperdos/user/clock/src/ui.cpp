//===----------------------------------------------------------------------===//
// Clock UI implementation
//===----------------------------------------------------------------------===//

#include "../include/ui.hpp"
#include <string.h>

// Simple sin/cos lookup for clock hands (fixed point, scaled by 1000)
// Pre-computed for 0-360 degrees in 6-degree increments (60 entries)
static const int sin_table[60] = {
    0,    105,  208,  309,  407,  500,  588,  669,  743,  809,  866,  914,
    951,  978,  995,  1000, 995,  978,  951,  914,  866,  809,  743,  669,
    588,  500,  407,  309,  208,  105,  0,    -105, -208, -309, -407, -500,
    -588, -669, -743, -809, -866, -914, -951, -978, -995, -1000,-995, -978,
    -951, -914, -866, -809, -743, -669, -588, -500, -407, -309, -208, -105};

static const int cos_table[60] = {
    1000, 995,  978,  951,  914,  866,  809,  743,  669,  588,  500,  407,
    309,  208,  105,  0,    -105, -208, -309, -407, -500, -588, -669, -743,
    -809, -866, -914, -951, -978, -995, -1000,-995, -978, -951, -914, -866,
    -809, -743, -669, -588, -500, -407, -309, -208, -105, 0,    105,  208,
    309,  407,  500,  588,  669,  743,  809,  866,  914,  951,  978,  995};

static int lookup_sin(int angle) {
    // Normalize to 0-359
    while (angle < 0) angle += 360;
    angle = angle % 360;
    // Convert to index (6 degrees per entry)
    int idx = angle / 6;
    if (idx >= 60) idx = 59;
    return sin_table[idx];
}

static int lookup_cos(int angle) {
    while (angle < 0) angle += 360;
    angle = angle % 360;
    int idx = angle / 6;
    if (idx >= 60) idx = 59;
    return cos_table[idx];
}

namespace clockapp {

UI::UI(gui_window_t *win) : m_win(win), m_24hour(false) {
}

void UI::render(const Time &time) {
    drawBackground();
    drawClockFace();
    drawHourMarks();
    drawHands(time);
    drawDigitalTime(time);
    drawDate(time);
    gui_present(m_win);
}

void UI::drawBackground() {
    gui_fill_rect(m_win, 0, 0, dims::WIN_WIDTH, dims::WIN_HEIGHT, colors::BACKGROUND);
}

void UI::drawClockFace() {
    // Draw filled circle for clock face
    int cx = dims::CLOCK_CENTER_X;
    int cy = dims::CLOCK_CENTER_Y;
    int r = dims::CLOCK_RADIUS;

    // Simple filled circle using horizontal lines
    for (int y = -r; y <= r; y++) {
        int x = 0;
        while (x * x + y * y <= r * r) x++;
        x--;
        if (x >= 0) {
            gui_draw_hline(m_win, cx - x, cx + x, cy + y, colors::FACE);
        }
    }

    // Draw border circle
    for (int i = 0; i < 60; i++) {
        int angle = i * 6;
        int x = cx + (r * lookup_sin(angle)) / 1000;
        int y = cy - (r * lookup_cos(angle)) / 1000;
        gui_fill_rect(m_win, x, y, 2, 2, colors::FACE_BORDER);
    }
}

void UI::drawHourMarks() {
    int cx = dims::CLOCK_CENTER_X;
    int cy = dims::CLOCK_CENTER_Y;
    int r = dims::CLOCK_RADIUS;

    for (int hour = 0; hour < 12; hour++) {
        int angle = hour * 30;
        int innerR = r - 10;
        int outerR = r - 3;

        int x1 = cx + (innerR * lookup_sin(angle)) / 1000;
        int y1 = cy - (innerR * lookup_cos(angle)) / 1000;
        int x2 = cx + (outerR * lookup_sin(angle)) / 1000;
        int y2 = cy - (outerR * lookup_cos(angle)) / 1000;

        // Draw thick mark for 12, 3, 6, 9
        if (hour % 3 == 0) {
            gui_fill_rect(m_win, x1 - 1, y1 - 1, 3, 3, colors::HOUR_MARKS);
            gui_fill_rect(m_win, x2 - 1, y2 - 1, 3, 3, colors::HOUR_MARKS);
        } else {
            gui_fill_rect(m_win, x1, y1, 2, 2, colors::HOUR_MARKS);
        }
    }
}

void UI::drawHands(const Time &time) {
    // Draw hour hand
    drawHand(hourHandAngle(time), dims::HOUR_HAND_LENGTH, 4, colors::HOUR_HAND);

    // Draw minute hand
    drawHand(minuteHandAngle(time), dims::MINUTE_HAND_LENGTH, 3, colors::MINUTE_HAND);

    // Draw second hand
    drawHand(secondHandAngle(time), dims::SECOND_HAND_LENGTH, 1, colors::SECOND_HAND);

    // Draw center dot
    gui_fill_rect(m_win, dims::CLOCK_CENTER_X - 3, dims::CLOCK_CENTER_Y - 3, 6, 6,
                  colors::CENTER_DOT);
}

void UI::drawHand(int angle, int length, int thickness, uint32_t color) {
    int cx = dims::CLOCK_CENTER_X;
    int cy = dims::CLOCK_CENTER_Y;

    int endX = cx + (length * lookup_sin(angle)) / 1000;
    int endY = cy - (length * lookup_cos(angle)) / 1000;

    // Draw line from center to end
    // Simple Bresenham-style line with thickness
    int dx = endX - cx;
    int dy = endY - cy;
    int steps = (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy) ? (dx > 0 ? dx : -dx)
                                                          : (dy > 0 ? dy : -dy);
    if (steps == 0) steps = 1;

    for (int i = 0; i <= steps; i++) {
        int x = cx + (dx * i) / steps;
        int y = cy + (dy * i) / steps;
        int half = thickness / 2;
        gui_fill_rect(m_win, x - half, y - half, thickness, thickness, color);
    }
}

void UI::drawDigitalTime(const Time &time) {
    char buf[32];

    // Draw background for digital display
    gui_fill_rect(m_win, 20, dims::DIGITAL_Y - 2, dims::WIN_WIDTH - 40, 16, colors::DIGITAL_BG);

    // Format and draw time
    if (m_24hour) {
        formatTime24(time, buf, sizeof(buf));
    } else {
        formatTime12(time, buf, sizeof(buf));
    }

    int textWidth = static_cast<int>(strlen(buf)) * 8;
    int textX = (dims::WIN_WIDTH - textWidth) / 2;
    gui_draw_text(m_win, textX, dims::DIGITAL_Y, buf, colors::DIGITAL_TEXT);
}

void UI::drawDate(const Time &time) {
    char buf[32];
    formatDate(time, buf, sizeof(buf));

    int textWidth = static_cast<int>(strlen(buf)) * 8;
    int textX = (dims::WIN_WIDTH - textWidth) / 2;
    gui_draw_text(m_win, textX, dims::DATE_Y, buf, colors::TEXT);
}

} // namespace clockapp
