//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file image.cpp
 * @brief Image loading implementation for the Viewer application.
 *
 * This file implements the Image class which handles loading and decoding
 * of image files into ARGB32 pixel buffers suitable for display.
 *
 * ## Supported Formats
 *
 * | Format | Extensions | Description                           |
 * |--------|------------|---------------------------------------|
 * | BMP    | .bmp       | Windows Bitmap (24-bit, 32-bit)       |
 * | PPM    | .ppm       | Portable Pixmap (binary P6 format)    |
 *
 * ## BMP Format Details
 *
 * The BMP loader handles:
 * - 24-bit RGB images (no alpha, opaque)
 * - 32-bit RGBA images (with alpha channel)
 * - Bottom-up row order (standard BMP)
 * - Top-down row order (negative height)
 * - Row padding to 4-byte boundary
 *
 * BMP header structure (54 bytes):
 * ```
 * Offset  Size  Description
 * 0       2     Magic "BM"
 * 10      4     Pixel data offset
 * 18      4     Image width
 * 22      4     Image height (negative = top-down)
 * 28      2     Bits per pixel (24 or 32)
 * ```
 *
 * ## PPM Format Details
 *
 * The PPM loader handles binary (P6) format:
 * ```
 * P6
 * # optional comments
 * width height
 * maxval
 * <binary RGB data>
 * ```
 *
 * - Only binary (P6) format supported, not ASCII (P3)
 * - RGB triplets stored as raw bytes
 * - No alpha channel (always opaque)
 *
 * ## Pixel Format
 *
 * All images are converted to ARGB32 format:
 * ```
 * Bits: 31-24  23-16  15-8   7-0
 *       Alpha  Red    Green  Blue
 * ```
 *
 * ## Memory Management
 *
 * - Pixel buffer allocated with malloc()
 * - Maximum image size: 4096x4096 pixels
 * - unload() frees buffer and resets state
 * - Destructor calls unload() automatically
 *
 * ## Error Handling
 *
 * When loading fails:
 * - m_pixels remains nullptr
 * - m_error contains human-readable message
 * - Previous image data is unloaded
 *
 * @see image.hpp for Image class definition
 */
//===----------------------------------------------------------------------===//

#include "../include/image.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace viewer {

Image::Image() : m_pixels(nullptr), m_width(0), m_height(0) {
    m_filename[0] = '\0';
    m_error[0] = '\0';
}

Image::~Image() {
    unload();
}

void Image::unload() {
    if (m_pixels) {
        free(m_pixels);
        m_pixels = nullptr;
    }
    m_width = 0;
    m_height = 0;
    m_filename[0] = '\0';
    m_error[0] = '\0';
}

ImageFormat Image::detectFormat(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        return ImageFormat::Unknown;
    }

    if (strcasecmp(ext, ".bmp") == 0) {
        return ImageFormat::BMP;
    }
    if (strcasecmp(ext, ".ppm") == 0) {
        return ImageFormat::PPM;
    }

    return ImageFormat::Unknown;
}

bool Image::load(const char *filename) {
    unload();

    ImageFormat format = detectFormat(filename);
    if (format == ImageFormat::Unknown) {
        snprintf(m_error, sizeof(m_error), "Unknown image format");
        return false;
    }

    strncpy(m_filename, filename, sizeof(m_filename) - 1);
    m_filename[sizeof(m_filename) - 1] = '\0';

    switch (format) {
    case ImageFormat::BMP:
        return loadBMP(filename);
    case ImageFormat::PPM:
        return loadPPM(filename);
    default:
        return false;
    }
}

bool Image::loadBMP(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        snprintf(m_error, sizeof(m_error), "Cannot open file");
        return false;
    }

    // Read BMP header
    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Invalid BMP header");
        return false;
    }

    // Check magic
    if (header[0] != 'B' || header[1] != 'M') {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Not a BMP file");
        return false;
    }

    // Extract dimensions
    m_width = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    m_height = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    int bitsPerPixel = header[28] | (header[29] << 8);
    uint32_t dataOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);

    if (m_width <= 0 || m_height <= 0 || m_width > 4096 || m_height > 4096) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Invalid dimensions");
        return false;
    }

    bool bottomUp = (m_height > 0);
    if (m_height < 0) {
        m_height = -m_height;
    }

    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Unsupported bit depth: %d", bitsPerPixel);
        return false;
    }

    // Allocate pixel buffer
    m_pixels = static_cast<uint32_t *>(malloc(m_width * m_height * sizeof(uint32_t)));
    if (!m_pixels) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Out of memory");
        return false;
    }

    // Seek to pixel data
    fseek(f, dataOffset, SEEK_SET);

    // Read pixel data
    int bytesPerPixel = bitsPerPixel / 8;
    int rowSize = ((m_width * bytesPerPixel + 3) / 4) * 4; // Row padding to 4 bytes
    uint8_t *rowBuf = static_cast<uint8_t *>(malloc(rowSize));

    for (int y = 0; y < m_height; y++) {
        int destY = bottomUp ? (m_height - 1 - y) : y;
        if (fread(rowBuf, 1, rowSize, f) != static_cast<size_t>(rowSize)) {
            free(rowBuf);
            unload();
            fclose(f);
            snprintf(m_error, sizeof(m_error), "Read error");
            return false;
        }

        for (int x = 0; x < m_width; x++) {
            uint8_t b = rowBuf[x * bytesPerPixel];
            uint8_t g = rowBuf[x * bytesPerPixel + 1];
            uint8_t r = rowBuf[x * bytesPerPixel + 2];
            uint8_t a = (bytesPerPixel == 4) ? rowBuf[x * bytesPerPixel + 3] : 0xFF;
            m_pixels[destY * m_width + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    free(rowBuf);
    fclose(f);
    return true;
}

// Helper to skip whitespace and comments, then read an integer
static int ppm_read_int(FILE *f) {
    int c;
    // Skip whitespace and comments
    while ((c = fgetc(f)) != EOF) {
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n')
                ;
        } else if (c > ' ') {
            break;
        }
    }
    if (c == EOF || c < '0' || c > '9') {
        return -1;
    }

    int val = 0;
    do {
        val = val * 10 + (c - '0');
        c = fgetc(f);
    } while (c >= '0' && c <= '9');

    // Put back the non-digit
    if (c != EOF) {
        ungetc(c, f);
    }
    return val;
}

bool Image::loadPPM(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        snprintf(m_error, sizeof(m_error), "Cannot open file");
        return false;
    }

    // Read PPM header
    char magic[3];
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != '6') {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Not a PPM file");
        return false;
    }

    // Read dimensions
    m_width = ppm_read_int(f);
    m_height = ppm_read_int(f);
    int maxval = ppm_read_int(f);

    if (m_width < 0 || m_height < 0 || maxval < 0) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Invalid PPM header");
        return false;
    }

    // Skip single whitespace after maxval
    fgetc(f);

    if (m_width <= 0 || m_height <= 0 || m_width > 4096 || m_height > 4096) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Invalid dimensions");
        return false;
    }

    // Allocate pixel buffer
    m_pixels = static_cast<uint32_t *>(malloc(m_width * m_height * sizeof(uint32_t)));
    if (!m_pixels) {
        fclose(f);
        snprintf(m_error, sizeof(m_error), "Out of memory");
        return false;
    }

    // Read pixel data
    for (int i = 0; i < m_width * m_height; i++) {
        uint8_t rgb[3];
        if (fread(rgb, 1, 3, f) != 3) {
            unload();
            fclose(f);
            snprintf(m_error, sizeof(m_error), "Read error");
            return false;
        }
        m_pixels[i] = 0xFF000000 | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
    }

    fclose(f);
    return true;
}

} // namespace viewer
