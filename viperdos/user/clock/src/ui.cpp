//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file ui.cpp
 * @brief Implementation of clock UI rendering.
 *
 * This file implements the graphical rendering for the clock application,
 * including the analog clock face, clock hands, and digital displays. The
 * rendering uses fixed-point trigonometry for efficient clock hand positioning.
 *
 * ## Trigonometry Implementation
 *
 * Rather than using floating-point sine/cosine functions (which may be slow
 * or unavailable on some systems), this implementation uses pre-computed
 * lookup tables with values scaled by 1000 for integer arithmetic.
 *
 * The tables contain 60 entries covering 360° in 6° increments, which
 * aligns perfectly with the clock's second-hand positions. This provides
 * sufficient precision for clock rendering while keeping calculations fast.
 *
 * ## Coordinate System
 *
 * - Origin is at the clock center (CLOCK_CENTER_X, CLOCK_CENTER_Y)
 * - Angles are in degrees with 0° at 12 o'clock (top)
 * - Angles increase clockwise (opposite to mathematical convention)
 * - Y axis is inverted (Y increases downward, as is standard for screen coordinates)
 *
 * ## Drawing Order
 *
 * Elements are drawn back-to-front to achieve proper layering:
 * 1. Background (clears previous frame)
 * 2. Clock face (white circle)
 * 3. Hour marks (tick lines)
 * 4. Hour hand (widest, underneath)
 * 5. Minute hand (medium)
 * 6. Second hand (thin, on top)
 * 7. Center dot (covers hand origins)
 * 8. Digital time display
 * 9. Date display
 *
 * @see ui.hpp for class declarations and documentation
 */
//===----------------------------------------------------------------------===//

#include "../include/ui.hpp"
#include <string.h>

//===----------------------------------------------------------------------===//
// Trigonometry Lookup Tables
//===----------------------------------------------------------------------===//

/**
 * @defgroup ClockTrig Trigonometry Lookup Tables
 * @brief Pre-computed sine and cosine values for clock hand rendering.
 *
 * These tables store sine and cosine values scaled by 1000 (fixed-point
 * with 3 decimal places of precision). The tables have 60 entries covering
 * 0° to 354° in 6° increments, matching clock second positions.
 *
 * ## Usage
 *
 * To convert an angle to table index: `index = angle / 6`
 * To compute x offset: `x = (radius * sin_table[index]) / 1000`
 * To compute y offset: `y = (radius * cos_table[index]) / 1000`
 *
 * ## Note on Coordinate System
 *
 * Because screen Y increases downward but we want 0° at the top:
 * - sin(angle) gives the X offset (positive = right)
 * - cos(angle) gives the negative Y offset (we subtract it)
 *
 * @{
 */

/**
 * @brief Sine lookup table for 0-354° in 6° increments.
 *
 * Values are sine(angle) * 1000, where angle = index * 6 degrees.
 *
 * For a clock display:
 * - sin(0°) = 0 (12 o'clock, no horizontal offset)
 * - sin(90°) = 1000 (3 o'clock, maximum right)
 * - sin(180°) = 0 (6 o'clock, no horizontal offset)
 * - sin(270°) = -1000 (9 o'clock, maximum left)
 */
static const int sin_table[60] = {
    0,    105,  208,  309,  407,  500,  588,  669,  743,  809,  866,  914,
    951,  978,  995,  1000, 995,  978,  951,  914,  866,  809,  743,  669,
    588,  500,  407,  309,  208,  105,  0,    -105, -208, -309, -407, -500,
    -588, -669, -743, -809, -866, -914, -951, -978, -995, -1000,-995, -978,
    -951, -914, -866, -809, -743, -669, -588, -500, -407, -309, -208, -105};

/**
 * @brief Cosine lookup table for 0-354° in 6° increments.
 *
 * Values are cosine(angle) * 1000, where angle = index * 6 degrees.
 *
 * For a clock display (with inverted Y axis):
 * - cos(0°) = 1000 (12 o'clock, maximum up)
 * - cos(90°) = 0 (3 o'clock, no vertical offset)
 * - cos(180°) = -1000 (6 o'clock, maximum down)
 * - cos(270°) = 0 (9 o'clock, no vertical offset)
 */
static const int cos_table[60] = {
    1000, 995,  978,  951,  914,  866,  809,  743,  669,  588,  500,  407,
    309,  208,  105,  0,    -105, -208, -309, -407, -500, -588, -669, -743,
    -809, -866, -914, -951, -978, -995, -1000,-995, -978, -951, -914, -866,
    -809, -743, -669, -588, -500, -407, -309, -208, -105, 0,    105,  208,
    309,  407,  500,  588,  669,  743,  809,  866,  914,  951,  978,  995};

/** @} */ // end ClockTrig

//===----------------------------------------------------------------------===//
// Trigonometry Lookup Functions
//===----------------------------------------------------------------------===//

/**
 * @brief Looks up the sine value for a given angle.
 *
 * Normalizes the angle to 0-359° range, then converts to a table index
 * by dividing by 6 (since entries are 6° apart).
 *
 * @param angle Angle in degrees (any value, will be normalized).
 * @return Sine of angle scaled by 1000 (range: -1000 to +1000).
 */
static int lookup_sin(int angle) {
    // Normalize to 0-359
    while (angle < 0) angle += 360;
    angle = angle % 360;
    // Convert to index (6 degrees per entry)
    int idx = angle / 6;
    if (idx >= 60) idx = 59;
    return sin_table[idx];
}

/**
 * @brief Looks up the cosine value for a given angle.
 *
 * Normalizes the angle to 0-359° range, then converts to a table index
 * by dividing by 6 (since entries are 6° apart).
 *
 * @param angle Angle in degrees (any value, will be normalized).
 * @return Cosine of angle scaled by 1000 (range: -1000 to +1000).
 */
static int lookup_cos(int angle) {
    while (angle < 0) angle += 360;
    angle = angle % 360;
    int idx = angle / 6;
    if (idx >= 60) idx = 59;
    return cos_table[idx];
}

namespace clockapp {

//===----------------------------------------------------------------------===//
// UI Class Implementation
//===----------------------------------------------------------------------===//

/**
 * @brief Constructs a UI instance attached to the given window.
 *
 * Stores the window pointer and initializes to 12-hour display mode.
 *
 * @param win The GUI window to render to.
 */
UI::UI(gui_window_t *win) : m_win(win), m_24hour(false) {
}

/**
 * @brief Renders the complete clock display.
 *
 * Performs a full redraw of all clock elements in proper z-order,
 * then presents the result to the display server.
 *
 * @param time The current time to display.
 */
void UI::render(const Time &time) {
    drawBackground();
    drawClockFace();
    drawHourMarks();
    drawHands(time);
    drawDigitalTime(time);
    drawDate(time);
    gui_present(m_win);
}

/**
 * @brief Fills the entire window with the background color.
 *
 * Uses a single rectangle fill covering the full window dimensions.
 */
void UI::drawBackground() {
    gui_fill_rect(m_win, 0, 0, dims::WIN_WIDTH, dims::WIN_HEIGHT, colors::BACKGROUND);
}

/**
 * @brief Draws the analog clock face circle.
 *
 * The face is drawn in two steps:
 * 1. A filled white circle using horizontal line segments
 * 2. A dotted border around the circumference
 *
 * ## Circle Fill Algorithm
 *
 * For each row from -radius to +radius:
 * 1. Calculate the x extent using circle equation: x² + y² ≤ r²
 * 2. Draw a horizontal line from -x to +x, offset from center
 *
 * This is more efficient than per-pixel checks because it only
 * calls the line drawing function once per row.
 */
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

/**
 * @brief Draws hour position markers around the clock face.
 *
 * Places tick marks at each of the 12 hour positions. The marks at
 * cardinal positions (12, 3, 6, 9) are drawn larger for emphasis.
 *
 * Each mark is positioned between an inner and outer radius near
 * the edge of the clock face, creating a short line pointing inward.
 */
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

/**
 * @brief Draws all clock hands at their current angles.
 *
 * Draws hands in order from back to front:
 * 1. Hour hand (thickest, drawn first so others overlay it)
 * 2. Minute hand (medium thickness)
 * 3. Second hand (thinnest, on top)
 * 4. Center dot (covers the pivot point where hands meet)
 *
 * @param time The current time for calculating hand positions.
 */
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

/**
 * @brief Draws a single clock hand as a thick line.
 *
 * Uses a simplified line drawing algorithm:
 * 1. Calculate the endpoint using trigonometry
 * 2. Determine the number of steps (max of dx, dy)
 * 3. Draw small squares along the line at each step
 *
 * The "thickness" parameter controls the size of the squares drawn
 * at each step, creating a wider or narrower line.
 *
 * ## Line Algorithm
 *
 * This is a simplified Bresenham-style approach that samples the line
 * at regular intervals and draws filled squares at each point. While
 * not as precise as true Bresenham for thin lines, it works well for
 * thick lines where minor position errors are hidden by the width.
 *
 * @param angle     Angle in degrees (0° = 12 o'clock).
 * @param length    Length from center to tip in pixels.
 * @param thickness Width of the hand in pixels.
 * @param color     ARGB color to draw the hand.
 */
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

/**
 * @brief Draws the digital time display below the analog clock.
 *
 * Renders a dark rectangle background, then draws the formatted time
 * string centered within it. The text uses a green color for an LCD-style
 * appearance.
 *
 * The time format depends on the m_24hour flag:
 * - 12-hour: "HH:MM:SS AM/PM"
 * - 24-hour: "HH:MM:SS"
 *
 * @param time The time to display.
 */
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

/**
 * @brief Draws the date string at the bottom of the window.
 *
 * Formats the date as "Mon DD, YYYY" and centers it horizontally
 * below the digital time display.
 *
 * @param time The time structure containing the date to display.
 */
void UI::drawDate(const Time &time) {
    char buf[32];
    formatDate(time, buf, sizeof(buf));

    int textWidth = static_cast<int>(strlen(buf)) * 8;
    int textX = (dims::WIN_WIDTH - textWidth) / 2;
    gui_draw_text(m_win, textX, dims::DATE_Y, buf, colors::TEXT);
}

} // namespace clockapp
