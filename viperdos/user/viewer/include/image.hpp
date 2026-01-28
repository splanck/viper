#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file image.hpp
 * @brief Image loading and management for the ViperDOS image viewer.
 *
 * This file defines the Image class which handles loading and storing
 * image data for display. The viewer supports BMP and PPM image formats.
 *
 * ## Supported Formats
 *
 * - **BMP**: Windows Bitmap format
 *   - 24-bit uncompressed (RGB)
 *   - 32-bit uncompressed (RGBA)
 *   - Bottom-up and top-down orientations
 *
 * - **PPM**: Portable Pixmap format
 *   - P6 binary format (RGB)
 *   - 8-bit per channel
 *
 * ## Pixel Format
 *
 * Loaded images are converted to a uniform ARGB32 format (0xAARRGGBB)
 * for easy rendering to the GUI surface. The alpha channel is set to
 * 0xFF (fully opaque) for formats that don't support transparency.
 *
 * ## Memory Management
 *
 * The Image class owns its pixel buffer, which is allocated on load()
 * and freed on unload() or destruction. The buffer size is
 * width * height * 4 bytes.
 *
 * @see view.hpp for image display
 */
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace viewer {

//===----------------------------------------------------------------------===//
// Image Format Enumeration
//===----------------------------------------------------------------------===//

/**
 * @brief Supported image file formats.
 *
 * The format is detected automatically from the file extension and/or
 * file header magic bytes.
 */
enum class ImageFormat {
    Unknown, /**< Format could not be determined. */
    BMP,     /**< Windows Bitmap format (.bmp). */
    PPM      /**< Portable Pixmap format (.ppm). */
};

//===----------------------------------------------------------------------===//
// Image Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages loading and storage of image data.
 *
 * The Image class provides a simple interface for loading image files
 * and accessing their pixel data. It handles format detection and
 * conversion to a standard pixel format.
 *
 * ## Usage
 *
 * @code
 * Image img;
 *
 * if (img.load("/path/to/image.bmp")) {
 *     printf("Loaded %dx%d image\n", img.width(), img.height());
 *
 *     // Access pixel data
 *     const uint32_t *pixels = img.pixels();
 *     for (int y = 0; y < img.height(); y++) {
 *         for (int x = 0; x < img.width(); x++) {
 *             uint32_t pixel = pixels[y * img.width() + x];
 *             // Process pixel...
 *         }
 *     }
 * } else {
 *     printf("Error: %s\n", img.errorMessage());
 * }
 * @endcode
 *
 * ## Thread Safety
 *
 * The Image class is not thread-safe. Access from multiple threads
 * requires external synchronization.
 */
class Image {
  public:
    /**
     * @brief Constructs an empty Image with no loaded data.
     *
     * The image starts in an unloaded state. Call load() to load
     * image data from a file.
     */
    Image();

    /**
     * @brief Destroys the Image, freeing any loaded pixel data.
     */
    ~Image();

    //=== Loading ===//

    /**
     * @brief Loads an image from a file.
     *
     * Detects the image format from the filename extension and loads
     * the pixel data. Any previously loaded image is automatically
     * unloaded first.
     *
     * ## Format Detection
     *
     * The format is determined by the filename extension:
     * - `.bmp` -> BMP format
     * - `.ppm` -> PPM format
     * - Other -> Unknown (load fails)
     *
     * ## Error Handling
     *
     * If loading fails, an error message is stored and can be retrieved
     * with errorMessage(). Common errors include:
     * - File not found
     * - Unsupported format
     * - Invalid file structure
     * - Out of memory
     *
     * @param filename Path to the image file to load.
     * @return true if loading succeeded, false on error.
     *
     * @see errorMessage() to get error details on failure
     */
    bool load(const char *filename);

    /**
     * @brief Unloads the current image, freeing memory.
     *
     * After calling unload(), isLoaded() will return false and all
     * dimension accessors will return 0.
     */
    void unload();

    //=== Accessors ===//

    /**
     * @brief Returns whether an image is currently loaded.
     *
     * @return true if an image is loaded and pixels() is valid.
     */
    bool isLoaded() const {
        return m_pixels != nullptr;
    }

    /**
     * @brief Returns the image width in pixels.
     *
     * @return Width in pixels, or 0 if no image is loaded.
     */
    int width() const {
        return m_width;
    }

    /**
     * @brief Returns the image height in pixels.
     *
     * @return Height in pixels, or 0 if no image is loaded.
     */
    int height() const {
        return m_height;
    }

    /**
     * @brief Returns a pointer to the pixel data.
     *
     * Pixels are stored in row-major order (left-to-right, top-to-bottom)
     * in ARGB32 format (0xAARRGGBB). The buffer contains
     * width * height pixels.
     *
     * @return Pointer to pixel data, or nullptr if no image is loaded.
     *
     * @note The returned pointer is valid until the image is unloaded
     *       or a new image is loaded.
     */
    const uint32_t *pixels() const {
        return m_pixels;
    }

    /**
     * @brief Returns the filename of the loaded image.
     *
     * @return Path that was passed to load(), or empty string if none.
     */
    const char *filename() const {
        return m_filename;
    }

    /**
     * @brief Returns the last error message.
     *
     * If load() returned false, this provides details about the failure.
     *
     * @return Error description, or empty string if no error.
     */
    const char *errorMessage() const {
        return m_error;
    }

  private:
    /**
     * @brief Loads a Windows Bitmap (BMP) file.
     *
     * Handles 24-bit and 32-bit uncompressed BMPs. Converts from
     * BGR/BGRA to ARGB format and handles bottom-up orientation.
     *
     * @param filename Path to the BMP file.
     * @return true on success, false on error.
     */
    bool loadBMP(const char *filename);

    /**
     * @brief Loads a Portable Pixmap (PPM) file.
     *
     * Supports P6 (binary RGB) format with 8-bit per channel.
     * Converts from RGB to ARGB format.
     *
     * @param filename Path to the PPM file.
     * @return true on success, false on error.
     */
    bool loadPPM(const char *filename);

    /**
     * @brief Detects the image format from the filename.
     *
     * Uses the file extension to determine format.
     *
     * @param filename Path to check.
     * @return Detected format, or ImageFormat::Unknown.
     */
    ImageFormat detectFormat(const char *filename);

    uint32_t *m_pixels;   /**< Pixel data in ARGB32 format. */
    int m_width;          /**< Image width in pixels. */
    int m_height;         /**< Image height in pixels. */
    char m_filename[256]; /**< Path of loaded file. */
    char m_error[128];    /**< Last error message. */
};

} // namespace viewer
