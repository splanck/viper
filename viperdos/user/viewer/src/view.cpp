//===----------------------------------------------------------------------===//
// Viewer UI implementation
//===----------------------------------------------------------------------===//

#include "../include/view.hpp"
#include <stdio.h>
#include <string.h>

namespace viewer {

View::View(gui_window_t *win) : m_win(win), m_zoom(ZoomLevel::Fit), m_panX(0), m_panY(0) {
}

int View::zoomPercent() const {
    switch (m_zoom) {
    case ZoomLevel::Fit:
        return 0; // Special case
    case ZoomLevel::Z25:
        return 25;
    case ZoomLevel::Z50:
        return 50;
    case ZoomLevel::Z100:
        return 100;
    case ZoomLevel::Z200:
        return 200;
    case ZoomLevel::Z400:
        return 400;
    default:
        return 100;
    }
}

void View::zoomIn() {
    switch (m_zoom) {
    case ZoomLevel::Fit:
    case ZoomLevel::Z25:
        m_zoom = ZoomLevel::Z50;
        break;
    case ZoomLevel::Z50:
        m_zoom = ZoomLevel::Z100;
        break;
    case ZoomLevel::Z100:
        m_zoom = ZoomLevel::Z200;
        break;
    case ZoomLevel::Z200:
    case ZoomLevel::Z400:
        m_zoom = ZoomLevel::Z400;
        break;
    }
}

void View::zoomOut() {
    switch (m_zoom) {
    case ZoomLevel::Fit:
    case ZoomLevel::Z25:
        m_zoom = ZoomLevel::Z25;
        break;
    case ZoomLevel::Z50:
        m_zoom = ZoomLevel::Z25;
        break;
    case ZoomLevel::Z100:
        m_zoom = ZoomLevel::Z50;
        break;
    case ZoomLevel::Z200:
        m_zoom = ZoomLevel::Z100;
        break;
    case ZoomLevel::Z400:
        m_zoom = ZoomLevel::Z200;
        break;
    }
}

void View::pan(int dx, int dy) {
    m_panX += dx;
    m_panY += dy;
}

void View::render(const Image &image) {
    drawBackground();

    if (image.isLoaded()) {
        drawImage(image);
    } else if (image.errorMessage()[0]) {
        drawError(image.errorMessage());
    } else {
        drawError("No image loaded");
    }

    drawStatusBar(image);
    gui_present(m_win);
}

void View::drawBackground() {
    gui_fill_rect(m_win, 0, 0, dims::WIN_WIDTH, dims::WIN_HEIGHT, colors::BACKGROUND);
}

void View::drawImage(const Image &image) {
    int imgW = image.width();
    int imgH = image.height();

    // Calculate display size based on zoom
    int displayW, displayH;
    if (m_zoom == ZoomLevel::Fit) {
        // Fit to window
        float scaleX = static_cast<float>(dims::WIN_WIDTH) / imgW;
        float scaleY = static_cast<float>(dims::IMAGE_AREA_HEIGHT) / imgH;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;
        if (scale > 1.0f) scale = 1.0f; // Don't upscale in fit mode
        displayW = static_cast<int>(imgW * scale);
        displayH = static_cast<int>(imgH * scale);
    } else {
        int percent = zoomPercent();
        displayW = (imgW * percent) / 100;
        displayH = (imgH * percent) / 100;
    }

    // Center image
    int x = (dims::WIN_WIDTH - displayW) / 2 + m_panX;
    int y = (dims::IMAGE_AREA_HEIGHT - displayH) / 2 + m_panY;

    // Draw checkerboard background for transparency
    drawCheckerboard(x, y, displayW, displayH);

    // Draw image (simple nearest-neighbor scaling)
    const uint32_t *srcPixels = image.pixels();
    for (int dy = 0; dy < displayH; dy++) {
        int screenY = y + dy;
        if (screenY < 0 || screenY >= dims::IMAGE_AREA_HEIGHT) continue;

        int srcY = (dy * imgH) / displayH;
        if (srcY >= imgH) srcY = imgH - 1;

        for (int dx = 0; dx < displayW; dx++) {
            int screenX = x + dx;
            if (screenX < 0 || screenX >= dims::WIN_WIDTH) continue;

            int srcX = (dx * imgW) / displayW;
            if (srcX >= imgW) srcX = imgW - 1;

            uint32_t pixel = srcPixels[srcY * imgW + srcX];
            gui_fill_rect(m_win, screenX, screenY, 1, 1, pixel);
        }
    }

    // Draw border
    gui_draw_hline(m_win, x - 1, x + displayW, y - 1, colors::BORDER_DARK);
    gui_draw_vline(m_win, x - 1, y - 1, y + displayH, colors::BORDER_DARK);
    gui_draw_hline(m_win, x - 1, x + displayW, y + displayH, colors::BORDER_LIGHT);
    gui_draw_vline(m_win, x + displayW, y - 1, y + displayH, colors::BORDER_LIGHT);
}

void View::drawCheckerboard(int x, int y, int w, int h) {
    const int cellSize = 8;
    for (int cy = 0; cy < h; cy += cellSize) {
        for (int cx = 0; cx < w; cx += cellSize) {
            int screenX = x + cx;
            int screenY = y + cy;
            if (screenX < 0 || screenY < 0) continue;
            if (screenX >= dims::WIN_WIDTH || screenY >= dims::IMAGE_AREA_HEIGHT) continue;

            int cellW = cellSize;
            int cellH = cellSize;
            if (cx + cellW > w) cellW = w - cx;
            if (cy + cellH > h) cellH = h - cy;

            bool light = ((cx / cellSize) + (cy / cellSize)) % 2 == 0;
            uint32_t color = light ? colors::CHECKERBOARD_LIGHT : colors::CHECKERBOARD_DARK;
            gui_fill_rect(m_win, screenX, screenY, cellW, cellH, color);
        }
    }
}

void View::drawStatusBar(const Image &image) {
    int y = dims::IMAGE_AREA_HEIGHT;

    // Background
    gui_fill_rect(m_win, 0, y, dims::WIN_WIDTH, dims::STATUSBAR_HEIGHT, colors::STATUSBAR);
    gui_draw_hline(m_win, 0, dims::WIN_WIDTH - 1, y, colors::BORDER_DARK);

    char statusBuf[128];

    if (image.isLoaded()) {
        // Filename
        const char *fname = image.filename();
        const char *basename = strrchr(fname, '/');
        if (basename) {
            basename++;
        } else {
            basename = fname;
        }
        gui_draw_text(m_win, 10, y + 5, basename, colors::TEXT);

        // Dimensions and zoom
        if (m_zoom == ZoomLevel::Fit) {
            snprintf(statusBuf, sizeof(statusBuf), "%dx%d (Fit)", image.width(), image.height());
        } else {
            snprintf(statusBuf, sizeof(statusBuf), "%dx%d @ %d%%", image.width(), image.height(),
                     zoomPercent());
        }
        int infoX = dims::WIN_WIDTH - static_cast<int>(strlen(statusBuf)) * 8 - 10;
        gui_draw_text(m_win, infoX, y + 5, statusBuf, colors::TEXT);
    } else {
        gui_draw_text(m_win, 10, y + 5, "No image", colors::TEXT);
    }
}

void View::drawError(const char *message) {
    int textLen = static_cast<int>(strlen(message));
    int x = (dims::WIN_WIDTH - textLen * 8) / 2;
    int y = dims::IMAGE_AREA_HEIGHT / 2;
    gui_draw_text(m_win, x, y, message, colors::ERROR_TEXT);
}

} // namespace viewer
