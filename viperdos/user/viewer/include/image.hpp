#pragma once
//===----------------------------------------------------------------------===//
// Image loading and management
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace viewer {

// Supported image formats
enum class ImageFormat { Unknown, BMP, PPM };

// Image data
class Image {
  public:
    Image();
    ~Image();

    // Loading
    bool load(const char *filename);
    void unload();

    // Accessors
    bool isLoaded() const { return m_pixels != nullptr; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    const uint32_t *pixels() const { return m_pixels; }
    const char *filename() const { return m_filename; }
    const char *errorMessage() const { return m_error; }

  private:
    bool loadBMP(const char *filename);
    bool loadPPM(const char *filename);
    ImageFormat detectFormat(const char *filename);

    uint32_t *m_pixels;
    int m_width;
    int m_height;
    char m_filename[256];
    char m_error[128];
};

} // namespace viewer
