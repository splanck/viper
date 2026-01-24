#pragma once
//===----------------------------------------------------------------------===//
// Viewer UI
//===----------------------------------------------------------------------===//

#include "image.hpp"
#include <gui.h>

namespace viewer {

// Colors
namespace colors {
constexpr uint32_t BACKGROUND = 0xFFAAAAAA;
constexpr uint32_t BORDER_LIGHT = 0xFFFFFFFF;
constexpr uint32_t BORDER_DARK = 0xFF555555;
constexpr uint32_t TEXT = 0xFF000000;
constexpr uint32_t STATUSBAR = 0xFFAAAAAA;
constexpr uint32_t ERROR_TEXT = 0xFFCC0000;
constexpr uint32_t CHECKERBOARD_LIGHT = 0xFFCCCCCC;
constexpr uint32_t CHECKERBOARD_DARK = 0xFF999999;
} // namespace colors

// Dimensions
namespace dims {
constexpr int WIN_WIDTH = 640;
constexpr int WIN_HEIGHT = 480;
constexpr int STATUSBAR_HEIGHT = 20;
constexpr int IMAGE_AREA_HEIGHT = WIN_HEIGHT - STATUSBAR_HEIGHT;
} // namespace dims

// Zoom levels (percentage)
enum class ZoomLevel { Fit, Z25, Z50, Z100, Z200, Z400 };

// View class
class View {
  public:
    View(gui_window_t *win);

    // Rendering
    void render(const Image &image);

    // Zoom control
    void zoomIn();
    void zoomOut();
    void zoomFit() { m_zoom = ZoomLevel::Fit; }
    void zoom100() { m_zoom = ZoomLevel::Z100; }
    ZoomLevel zoomLevel() const { return m_zoom; }
    int zoomPercent() const;

    // Panning
    void pan(int dx, int dy);
    void resetPan() { m_panX = 0; m_panY = 0; }

  private:
    void drawBackground();
    void drawImage(const Image &image);
    void drawStatusBar(const Image &image);
    void drawError(const char *message);
    void drawCheckerboard(int x, int y, int w, int h);

    gui_window_t *m_win;
    ZoomLevel m_zoom;
    int m_panX;
    int m_panY;
};

} // namespace viewer
