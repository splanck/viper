#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file view.hpp
 * @brief Image viewer UI rendering and display.
 *
 * This file defines the View class for the ViperDOS image viewer, handling
 * all rendering including the image display, zoom/pan controls, and status bar.
 *
 * ## Visual Layout
 *
 * ```
 * +--------------------------------------+
 * |                                      |
 * |    +--------------------------+      |
 * |    |                          |      |
 * |    |     Image Display        |      |  Image Area
 * |    |    (with zoom/pan)       |      |  (460px)
 * |    |                          |      |
 * |    +--------------------------+      |
 * |                                      |
 * +--------------------------------------+
 * | filename.bmp     100%    640x480    |  Status Bar (20px)
 * +--------------------------------------+
 * ```
 *
 * ## Features
 *
 * - **Zoom levels**: Fit, 25%, 50%, 100%, 200%, 400%
 * - **Panning**: Click and drag to move the image
 * - **Keyboard navigation**: Arrow keys for panning, +/- for zoom
 * - **Checkerboard background**: Shows transparency for alpha images
 *
 * ## Zoom Behavior
 *
 * - **Fit mode**: Scales the image to fit within the display area while
 *   maintaining aspect ratio
 * - **Fixed percentages**: Display at exact scale relative to original size
 * - Images smaller than the display are centered
 * - Images larger than the display can be panned
 *
 * @see image.hpp for image loading
 */
//===----------------------------------------------------------------------===//

#include "image.hpp"
#include <gui.h>

namespace viewer {

//===----------------------------------------------------------------------===//
// Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup ViewerColors Viewer Color Palette
 * @brief Color constants for the image viewer UI.
 *
 * Colors are in ARGB format (0xAARRGGBB). The viewer uses a neutral
 * gray background and a checkerboard pattern to indicate transparency.
 * @{
 */
namespace colors {

/** @brief Window background color (Workbench gray). */
constexpr uint32_t BACKGROUND = 0xFFAAAAAA;

/** @brief 3D border highlight color (light edge). */
constexpr uint32_t BORDER_LIGHT = 0xFFFFFFFF;

/** @brief 3D border shadow color (dark edge). */
constexpr uint32_t BORDER_DARK = 0xFF555555;

/** @brief Normal text color in status bar. */
constexpr uint32_t TEXT = 0xFF000000;

/** @brief Status bar background color. */
constexpr uint32_t STATUSBAR = 0xFFAAAAAA;

/** @brief Error message text color (red). */
constexpr uint32_t ERROR_TEXT = 0xFFCC0000;

/** @brief Light squares in the checkerboard pattern. */
constexpr uint32_t CHECKERBOARD_LIGHT = 0xFFCCCCCC;

/** @brief Dark squares in the checkerboard pattern. */
constexpr uint32_t CHECKERBOARD_DARK = 0xFF999999;

} // namespace colors

/** @} */ // end ViewerColors

//===----------------------------------------------------------------------===//
// Dimension Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup ViewerDimensions Viewer Layout Dimensions
 * @brief Size constants for the viewer window layout.
 * @{
 */
namespace dims {

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 640;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 480;

/** @brief Height of the status bar in pixels. */
constexpr int STATUSBAR_HEIGHT = 20;

/** @brief Height of the image display area. */
constexpr int IMAGE_AREA_HEIGHT = WIN_HEIGHT - STATUSBAR_HEIGHT;

} // namespace dims
/** @} */ // end ViewerDimensions

//===----------------------------------------------------------------------===//
// Zoom Level Enumeration
//===----------------------------------------------------------------------===//

/**
 * @brief Available zoom levels for image display.
 *
 * The Fit level automatically calculates a scale to fit the image
 * within the display area. The numbered levels represent fixed
 * percentages of the original image size.
 */
enum class ZoomLevel {
    Fit,  /**< Scale to fit display area while maintaining aspect ratio. */
    Z25,  /**< 25% of original size. */
    Z50,  /**< 50% of original size. */
    Z100, /**< 100% (actual pixels, 1:1 mapping). */
    Z200, /**< 200% of original size. */
    Z400  /**< 400% of original size. */
};

//===----------------------------------------------------------------------===//
// View Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages the image viewer display.
 *
 * The View class handles all rendering for the image viewer, including
 * the image display with zoom/pan, background, and status bar. It
 * maintains the current zoom level and pan offset.
 *
 * ## Usage
 *
 * @code
 * gui_window_t *win = gui_create_window("Viewer", 640, 480);
 * View view(win);
 * Image image;
 *
 * image.load("photo.bmp");
 * view.render(image);
 *
 * // Zoom in
 * view.zoomIn();
 * view.render(image);
 *
 * // Pan the image
 * view.pan(10, 0);  // Move 10 pixels right
 * view.render(image);
 * @endcode
 */
class View {
  public:
    /**
     * @brief Constructs a View for the given window.
     *
     * Initializes with Fit zoom level and no pan offset.
     *
     * @param win GUI window to render to.
     */
    View(gui_window_t *win);

    //=== Rendering ===//

    /**
     * @brief Renders the complete viewer interface.
     *
     * Draws the background, image (if loaded), and status bar.
     * If no image is loaded or loading failed, displays an
     * appropriate error message.
     *
     * @param image The Image to display.
     */
    void render(const Image &image);

    //=== Zoom Control ===//

    /**
     * @brief Increases the zoom level by one step.
     *
     * Advances through zoom levels: Fit -> 25% -> 50% -> 100% -> 200% -> 400%
     * Does nothing if already at maximum zoom (400%).
     */
    void zoomIn();

    /**
     * @brief Decreases the zoom level by one step.
     *
     * Goes backward through zoom levels: 400% -> 200% -> 100% -> 50% -> 25% -> Fit
     * Does nothing if already at minimum zoom (Fit).
     */
    void zoomOut();

    /**
     * @brief Sets zoom level to Fit mode.
     *
     * Scales the image to fit within the display area while
     * maintaining the aspect ratio.
     */
    void zoomFit() {
        m_zoom = ZoomLevel::Fit;
    }

    /**
     * @brief Sets zoom level to 100% (actual pixels).
     *
     * Displays the image at its native resolution with no scaling.
     */
    void zoom100() {
        m_zoom = ZoomLevel::Z100;
    }

    /**
     * @brief Returns the current zoom level.
     *
     * @return The current ZoomLevel enum value.
     */
    ZoomLevel zoomLevel() const {
        return m_zoom;
    }

    /**
     * @brief Returns the current zoom as a percentage.
     *
     * For Fit mode, this returns the calculated percentage based on
     * the current image and display size.
     *
     * @return Zoom percentage (25, 50, 100, 200, or 400).
     *
     * @note For Fit mode with no image, returns 100.
     */
    int zoomPercent() const;

    //=== Panning ===//

    /**
     * @brief Pans the image by the specified offset.
     *
     * Moves the visible portion of the image. Positive dx moves the
     * image right (view moves left), positive dy moves the image down.
     *
     * @param dx Horizontal pan offset in pixels.
     * @param dy Vertical pan offset in pixels.
     */
    void pan(int dx, int dy);

    /**
     * @brief Resets the pan offset to center the image.
     */
    void resetPan() {
        m_panX = 0;
        m_panY = 0;
    }

  private:
    /**
     * @brief Fills the window with the background color.
     */
    void drawBackground();

    /**
     * @brief Draws the image with current zoom and pan settings.
     *
     * Calculates the visible portion of the image based on zoom level
     * and pan offset, then blits it to the display area.
     *
     * @param image The Image to draw.
     */
    void drawImage(const Image &image);

    /**
     * @brief Draws the status bar with image information.
     *
     * Shows filename, zoom level, and image dimensions.
     *
     * @param image The Image for status information.
     */
    void drawStatusBar(const Image &image);

    /**
     * @brief Draws an error message in the image area.
     *
     * Used when no image is loaded or loading failed.
     *
     * @param message Error text to display.
     */
    void drawError(const char *message);

    /**
     * @brief Draws a checkerboard pattern for transparency.
     *
     * Creates an 8x8 pixel checkerboard pattern commonly used to
     * indicate transparent areas in images.
     *
     * @param x Left edge of the checkerboard area.
     * @param y Top edge of the checkerboard area.
     * @param w Width of the area in pixels.
     * @param h Height of the area in pixels.
     */
    void drawCheckerboard(int x, int y, int w, int h);

    gui_window_t *m_win; /**< Window to render to. */
    ZoomLevel m_zoom;    /**< Current zoom level. */
    int m_panX;          /**< Horizontal pan offset in pixels. */
    int m_panY;          /**< Vertical pan offset in pixels. */
};

} // namespace viewer
