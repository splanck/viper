#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file ui.hpp
 * @brief Clock application UI rendering and visual styling.
 *
 * This file defines the visual interface for the ViperDOS clock application,
 * including the analog clock face, digital time display, and date display.
 * The UI class encapsulates all rendering logic and manages display modes.
 *
 * ## Visual Layout
 *
 * ```
 * +---------------------------+
 * |                           |
 * |      .----.               |
 * |     /  12  \              |
 * |    |9  +  3|   Analog     |
 * |     \  6  /    Clock      |
 * |      '----'               |
 * |                           |
 * |  [ 12:34:56 PM ]  Digital |
 * |    Jan 15, 2024    Date   |
 * +---------------------------+
 * ```
 *
 * ## Features
 *
 * - **Analog Clock**: Circular face with hour marks, hour/minute/second hands
 * - **Digital Display**: Time shown in 12-hour or 24-hour format
 * - **Date Display**: Current date below digital time
 * - **Mode Toggle**: Click anywhere to switch between 12/24 hour display
 *
 * ## Rendering Pipeline
 *
 * Each frame is rendered in this order:
 * 1. Background (fills entire window)
 * 2. Clock face (white circle with border)
 * 3. Hour marks (tick marks at each hour position)
 * 4. Clock hands (hour, minute, second in that order)
 * 5. Center dot (covers hand pivot point)
 * 6. Digital time display
 * 7. Date display
 *
 * ## Trigonometry
 *
 * The clock uses pre-computed sine/cosine lookup tables for efficiency.
 * Angles are measured in degrees with 0° at 12 o'clock, increasing clockwise.
 * The lookup tables provide values scaled by 1000 for integer arithmetic.
 *
 * @see clock.hpp for time structure and angle calculations
 */
//===----------------------------------------------------------------------===//

#include "clock.hpp"
#include <gui.h>
#include <stdint.h>

namespace clockapp {

//===----------------------------------------------------------------------===//
// Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup ClockColors Clock Color Palette
 * @brief Color constants for clock UI elements.
 *
 * Colors are defined in ARGB format (0xAARRGGBB). The palette uses
 * the standard ViperDOS Workbench gray for the background, with a white
 * clock face for contrast and colored hands for visual distinction.
 * @{
 */
namespace colors {

/** @brief Window background color (Workbench gray). */
constexpr uint32_t BACKGROUND = 0xFFAAAAAA;

/** @brief Analog clock face fill color (white for contrast). */
constexpr uint32_t FACE = 0xFFFFFFFF;

/** @brief Analog clock face border color (dark gray). */
constexpr uint32_t FACE_BORDER = 0xFF555555;

/** @brief Hour mark tick color (black). */
constexpr uint32_t HOUR_MARKS = 0xFF000000;

/** @brief Hour hand color (black, thickest hand). */
constexpr uint32_t HOUR_HAND = 0xFF000000;

/** @brief Minute hand color (dark gray, medium thickness). */
constexpr uint32_t MINUTE_HAND = 0xFF333333;

/** @brief Second hand color (red, thinnest hand). */
constexpr uint32_t SECOND_HAND = 0xFFCC0000;

/** @brief Center pivot dot color (black). */
constexpr uint32_t CENTER_DOT = 0xFF000000;

/** @brief Date text color (black). */
constexpr uint32_t TEXT = 0xFF000000;

/** @brief Digital time display background color (dark gray/black). */
constexpr uint32_t DIGITAL_BG = 0xFF222222;

/** @brief Digital time display text color (green, LCD-style). */
constexpr uint32_t DIGITAL_TEXT = 0xFF00FF00;

} // namespace colors
/** @} */ // end ClockColors

//===----------------------------------------------------------------------===//
// Dimension Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup ClockDimensions Clock Layout Dimensions
 * @brief Size and position constants for clock UI layout.
 *
 * All dimensions are in pixels. The clock face is centered horizontally
 * in the window, with the digital display and date below it.
 * @{
 */
namespace dims {

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 200;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 240;

/** @brief X coordinate of analog clock center. */
constexpr int CLOCK_CENTER_X = 100;

/** @brief Y coordinate of analog clock center. */
constexpr int CLOCK_CENTER_Y = 100;

/** @brief Radius of the analog clock face in pixels. */
constexpr int CLOCK_RADIUS = 80;

/** @brief Length of the hour hand in pixels from center. */
constexpr int HOUR_HAND_LENGTH = 40;

/** @brief Length of the minute hand in pixels from center. */
constexpr int MINUTE_HAND_LENGTH = 60;

/** @brief Length of the second hand in pixels from center. */
constexpr int SECOND_HAND_LENGTH = 65;

/** @brief Y coordinate for digital time display. */
constexpr int DIGITAL_Y = 200;

/** @brief Y coordinate for date display. */
constexpr int DATE_Y = 220;

} // namespace dims
/** @} */ // end ClockDimensions

//===----------------------------------------------------------------------===//
// UI Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages the clock application's graphical user interface.
 *
 * The UI class encapsulates all rendering logic for the clock display,
 * including the analog clock face with moving hands, digital time readout,
 * and date display. It maintains state for the 12/24 hour display mode.
 *
 * ## Usage
 *
 * @code
 * gui_window_t *win = gui_create_window("Clock", dims::WIN_WIDTH, dims::WIN_HEIGHT);
 * UI ui(win);
 *
 * while (running) {
 *     Time time;
 *     getCurrentTime(time);
 *     ui.render(time);
 *
 *     if (userClickedWindow) {
 *         ui.toggle24Hour();
 *     }
 * }
 * @endcode
 *
 * ## Rendering Details
 *
 * The `render()` method performs a complete redraw of the entire window.
 * This is called once per second when the time changes, or immediately
 * after a mode toggle. The rendering uses immediate-mode drawing to the
 * window's pixel buffer.
 *
 * ## Clock Hand Drawing
 *
 * Clock hands are drawn as lines from the center point outward at the
 * calculated angle. Each hand has a different length and thickness:
 * - Hour hand: 40px, 4px thick, black
 * - Minute hand: 60px, 3px thick, dark gray
 * - Second hand: 65px, 1px thick, red
 *
 * The center dot is drawn last to cover the hand pivot points cleanly.
 */
class UI {
  public:
    /**
     * @brief Constructs a new UI instance for the given window.
     *
     * Initializes the UI with 12-hour display mode by default.
     *
     * @param win Pointer to the GUI window for rendering. Must remain valid
     *            for the lifetime of the UI object.
     *
     * @note The window is not modified during construction; call render()
     *       to draw the initial display.
     */
    UI(gui_window_t *win);

    /**
     * @brief Renders the complete clock display.
     *
     * Draws all clock elements to the window and presents the result:
     * 1. Clears the background
     * 2. Draws the analog clock face and border
     * 3. Draws hour marks at each hour position
     * 4. Draws clock hands at current positions
     * 5. Draws the digital time display
     * 6. Draws the current date
     * 7. Calls gui_present() to show the updated display
     *
     * @param time The current time to display.
     *
     * @note This method performs a complete redraw every call. It should
     *       be called whenever the time changes (once per second) or when
     *       the display mode changes.
     */
    void render(const Time &time);

    /**
     * @brief Toggles between 12-hour and 24-hour display modes.
     *
     * When in 12-hour mode, the digital display shows time with AM/PM suffix.
     * When in 24-hour mode, the digital display shows time in military format.
     * The analog clock display is not affected by this setting.
     *
     * @note Call render() after toggling to update the display.
     *
     * @see is24Hour() to query the current mode
     */
    void toggle24Hour() { m_24hour = !m_24hour; }

    /**
     * @brief Returns whether 24-hour display mode is active.
     *
     * @return true if displaying time in 24-hour format (e.g., "13:45:00")
     * @return false if displaying time in 12-hour format (e.g., "1:45:00 PM")
     *
     * @see toggle24Hour() to change the mode
     */
    bool is24Hour() const { return m_24hour; }

  private:
    /**
     * @brief Fills the window with the background color.
     *
     * Clears the entire window to the Workbench gray background color,
     * preparing for a fresh render of all clock elements.
     */
    void drawBackground();

    /**
     * @brief Draws the analog clock face circle.
     *
     * Renders a filled white circle for the clock face background, then
     * draws a dotted border around the circumference using the face
     * border color.
     *
     * The circle is drawn using horizontal line fills for each row,
     * computing the x-extent using the circle equation x² + y² = r².
     */
    void drawClockFace();

    /**
     * @brief Draws the hour position markers on the clock face.
     *
     * Places tick marks at each of the 12 hour positions around the
     * clock face. The marks at 12, 3, 6, and 9 are larger (3x3 pixels)
     * than the others (2x2 pixels) for easier reading.
     *
     * Markers are positioned near the edge of the clock face, pointing
     * toward the center.
     */
    void drawHourMarks();

    /**
     * @brief Draws all three clock hands at their current positions.
     *
     * Draws the hour, minute, and second hands in that order (back to front),
     * then draws the center dot over the pivot point. The hands are drawn
     * with different colors and thicknesses for visual distinction.
     *
     * @param time The current time, used to calculate hand angles.
     */
    void drawHands(const Time &time);

    /**
     * @brief Draws a single clock hand at the specified angle.
     *
     * Renders a line from the clock center outward at the given angle.
     * The line is drawn with the specified thickness by filling small
     * squares along the line path.
     *
     * ## Angle Convention
     *
     * Angles are in degrees with 0° at 12 o'clock, increasing clockwise.
     * The hand points outward from center toward the specified angle.
     *
     * @param angle     Rotation angle in degrees (0° = 12 o'clock).
     * @param length    Length of the hand in pixels from center.
     * @param thickness Width of the hand in pixels (drawn as squares).
     * @param color     Fill color for the hand (ARGB format).
     */
    void drawHand(int angle, int length, int thickness, uint32_t color);

    /**
     * @brief Draws the digital time display below the analog clock.
     *
     * Renders a dark background strip with the current time displayed
     * in green text (LCD-style). The format depends on the current
     * display mode (12-hour with AM/PM or 24-hour).
     *
     * The time text is centered horizontally within the display area.
     *
     * @param time The current time to display.
     */
    void drawDigitalTime(const Time &time);

    /**
     * @brief Draws the date display at the bottom of the window.
     *
     * Renders the current date in "Mon DD, YYYY" format, centered
     * horizontally below the digital time display.
     *
     * @param time The current time containing the date to display.
     */
    void drawDate(const Time &time);

    gui_window_t *m_win; /**< Pointer to the window for rendering. */
    bool m_24hour;       /**< True if using 24-hour display mode. */
};

} // namespace clockapp
