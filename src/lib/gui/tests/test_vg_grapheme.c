//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_grapheme.c
// Purpose: Validate ZannaGUI extended grapheme segmentation against the pinned
//          Unicode conformance corpus and public offset-conversion contracts.
// Key invariants:
//   - Every non-comment GraphemeBreakTest row is checked in both directions.
//   - Tests pass explicit byte lengths, including rows containing U+0000.
// Ownership/Lifetime:
//   - Test buffers and the conformance file are owned by main for the process.
//   - The segmentation API borrows all input buffers and retains no pointers.
// Links: src/lib/gui/include/vg_grapheme.h,
//        src/lib/gui/src/core/vg_grapheme.c,
//        src/lib/gui/tests/data/GraphemeBreakTest-17.0.0.txt
//
//===----------------------------------------------------------------------===//

#include "vg_grapheme.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VG_GRAPHEME_TEST_DATA_FILE
#error "VG_GRAPHEME_TEST_DATA_FILE must name the pinned Unicode conformance data"
#endif

enum { TEST_LINE_CAPACITY = 8192, TEST_TEXT_CAPACITY = 4096, TEST_BOUNDARY_CAPACITY = 1024 };

static int g_failures = 0;

/// @brief Report one failed conformance assertion with its source row.
/// @param line_number One-based line number in the official test corpus.
/// @param detail Human-readable invariant that failed.
/// @param source Original unparsed test row for diagnostic context.
static void report_failure(size_t line_number, const char *detail, const char *source) {
    fprintf(stderr, "%s:%zu: %s\n  %s", VG_GRAPHEME_TEST_DATA_FILE, line_number, detail, source);
    if (!strchr(source, '\n'))
        fputc('\n', stderr);
    g_failures++;
}

/// @brief Encode one Unicode scalar into a caller-provided UTF-8 buffer.
/// @param scalar Unicode scalar value from the conformance corpus.
/// @param output Destination with room for at least four bytes.
/// @return Number of bytes written, or zero for an invalid scalar.
static size_t encode_utf8(uint32_t scalar, unsigned char output[4]) {
    if (scalar <= 0x7Fu) {
        output[0] = (unsigned char)scalar;
        return 1;
    }
    if (scalar <= 0x7FFu) {
        output[0] = (unsigned char)(0xC0u | (scalar >> 6));
        output[1] = (unsigned char)(0x80u | (scalar & 0x3Fu));
        return 2;
    }
    if (scalar <= 0xFFFFu && !(scalar >= 0xD800u && scalar <= 0xDFFFu)) {
        output[0] = (unsigned char)(0xE0u | (scalar >> 12));
        output[1] = (unsigned char)(0x80u | ((scalar >> 6) & 0x3Fu));
        output[2] = (unsigned char)(0x80u | (scalar & 0x3Fu));
        return 3;
    }
    if (scalar <= 0x10FFFFu) {
        output[0] = (unsigned char)(0xF0u | (scalar >> 18));
        output[1] = (unsigned char)(0x80u | ((scalar >> 12) & 0x3Fu));
        output[2] = (unsigned char)(0x80u | ((scalar >> 6) & 0x3Fu));
        output[3] = (unsigned char)(0x80u | (scalar & 0x3Fu));
        return 4;
    }
    return 0;
}

/// @brief Return whether a token is the UTF-8 division marker used for a boundary.
/// @param token Whitespace-delimited token from GraphemeBreakTest.txt.
/// @return true for U+00F7 DIVISION SIGN, false otherwise.
static bool token_is_break(const char *token) {
    return token && strcmp(token, "\xC3\xB7") == 0;
}

/// @brief Return whether a token is either official grapheme boundary marker.
/// @param token Whitespace-delimited token from GraphemeBreakTest.txt.
/// @return true for U+00F7 DIVISION SIGN or U+00D7 MULTIPLICATION SIGN.
static bool token_is_marker(const char *token) {
    return token_is_break(token) || (token && strcmp(token, "\xC3\x97") == 0);
}

/// @brief Parse and validate one official GraphemeBreakTest data row.
/// @details The parser records expected byte and codepoint offsets at every
///          division marker, then validates count, random-access conversion,
///          forward navigation, and backward navigation against those offsets.
/// @param line Mutable test row; comments are removed in place.
/// @param original Immutable original row used only for failure diagnostics.
/// @param line_number One-based corpus line number.
static void check_conformance_row(char *line, const char *original, size_t line_number) {
    char *comment = strchr(line, '#');
    if (comment)
        *comment = '\0';

    unsigned char text[TEST_TEXT_CAPACITY];
    size_t byte_boundaries[TEST_BOUNDARY_CAPACITY];
    size_t codepoint_boundaries[TEST_BOUNDARY_CAPACITY];
    size_t text_length = 0;
    size_t codepoint_count = 0;
    size_t boundary_count = 0;

    char *token = strtok(line, " \t\r\n");
    while (token) {
        if (!token_is_marker(token)) {
            report_failure(line_number, "expected a boundary marker", original);
            return;
        }
        if (token_is_break(token)) {
            if (boundary_count >= TEST_BOUNDARY_CAPACITY) {
                report_failure(line_number, "too many expected boundaries", original);
                return;
            }
            byte_boundaries[boundary_count] = text_length;
            codepoint_boundaries[boundary_count] = codepoint_count;
            boundary_count++;
        }

        token = strtok(NULL, " \t\r\n");
        if (!token)
            break;
        char *end = NULL;
        unsigned long scalar = strtoul(token, &end, 16);
        if (!end || *end != '\0' || scalar > UINT32_MAX) {
            report_failure(line_number, "invalid scalar token", original);
            return;
        }
        unsigned char encoded[4];
        size_t encoded_length = encode_utf8((uint32_t)scalar, encoded);
        if (encoded_length == 0 || text_length + encoded_length > sizeof(text)) {
            report_failure(line_number, "invalid or oversized UTF-8 row", original);
            return;
        }
        memcpy(text + text_length, encoded, encoded_length);
        text_length += encoded_length;
        codepoint_count++;
        token = strtok(NULL, " \t\r\n");
    }

    if (boundary_count < 1) {
        return;
    }
    size_t expected_graphemes = boundary_count - 1;
    if (vg_grapheme_count((const char *)text, text_length) != expected_graphemes) {
        report_failure(line_number, "grapheme count mismatch", original);
        return;
    }

    for (size_t index = 0; index < boundary_count; index++) {
        if (vg_grapheme_byte_offset((const char *)text, text_length, index) !=
            byte_boundaries[index]) {
            report_failure(line_number, "byte boundary mismatch", original);
            return;
        }
        if (vg_grapheme_codepoint_offset((const char *)text, text_length, index) !=
            codepoint_boundaries[index]) {
            report_failure(line_number, "codepoint boundary mismatch", original);
            return;
        }
        if (vg_grapheme_index_from_codepoint(
                (const char *)text, text_length, codepoint_boundaries[index]) != index) {
            report_failure(line_number, "codepoint-to-grapheme round trip mismatch", original);
            return;
        }
        if (index > 0 && vg_grapheme_previous_codepoint_boundary(
                             (const char *)text, text_length, codepoint_boundaries[index]) !=
                             codepoint_boundaries[index - 1]) {
            report_failure(line_number, "previous boundary mismatch", original);
            return;
        }
        if (index + 1 < boundary_count &&
            vg_grapheme_next_codepoint_boundary(
                (const char *)text, text_length, codepoint_boundaries[index]) !=
                codepoint_boundaries[index + 1]) {
            report_failure(line_number, "next boundary mismatch", original);
            return;
        }
    }
}

/// @brief Run the complete pinned Unicode grapheme boundary conformance corpus.
/// @return Number of parsed non-comment test cases, or zero if the file cannot be opened.
static size_t run_unicode_conformance(void) {
    FILE *file = fopen(VG_GRAPHEME_TEST_DATA_FILE, "rb");
    if (!file) {
        fprintf(stderr, "unable to open %s\n", VG_GRAPHEME_TEST_DATA_FILE);
        g_failures++;
        return 0;
    }

    char line[TEST_LINE_CAPACITY];
    size_t line_number = 0;
    size_t test_count = 0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char original[TEST_LINE_CAPACITY];
        memcpy(original, line, sizeof(original));
        original[sizeof(original) - 1] = '\0';
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (*cursor == '#' || *cursor == '\r' || *cursor == '\n' || *cursor == '\0')
            continue;
        check_conformance_row(cursor, original, line_number);
        test_count++;
    }
    if (!feof(file)) {
        fprintf(stderr,
                "%s: input row exceeded %d bytes or read failed\n",
                VG_GRAPHEME_TEST_DATA_FILE,
                TEST_LINE_CAPACITY);
        g_failures++;
    }
    fclose(file);
    return test_count;
}

/// @brief Verify deterministic behavior for malformed UTF-8 and clamped indices.
/// @details Each malformed lead or continuation byte is treated as one Other
///          scalar so callers can always make forward progress without reading
///          beyond the supplied byte length.
static void test_invalid_utf8_and_clamping(void) {
    static const unsigned char malformed_bytes[] = {'A', 0xF0u, 0x80u, 'B'};
    const char *malformed = (const char *)malformed_bytes;
    if (vg_grapheme_count(malformed, sizeof(malformed_bytes)) != 4 ||
        vg_grapheme_byte_offset(malformed, sizeof(malformed_bytes), 99) !=
            sizeof(malformed_bytes) ||
        vg_grapheme_codepoint_offset(malformed, sizeof(malformed_bytes), 99) != 4 ||
        vg_grapheme_index_from_codepoint(malformed, sizeof(malformed_bytes), 99) != 4) {
        fprintf(stderr, "malformed UTF-8 or index clamping contract failed\n");
        g_failures++;
    }
    if (vg_grapheme_count(NULL, 0) != 0 || vg_grapheme_byte_offset(NULL, 0, 1) != 0) {
        fprintf(stderr, "NULL empty-input contract failed\n");
        g_failures++;
    }
}

/// @brief Execute Unicode conformance and defensive API contract tests.
/// @return EXIT_SUCCESS when all checks pass, otherwise EXIT_FAILURE.
int main(void) {
    size_t conformance_count = run_unicode_conformance();
    test_invalid_utf8_and_clamping();
    if (conformance_count == 0) {
        fprintf(stderr, "no Unicode conformance cases were executed\n");
        g_failures++;
    }
    if (g_failures != 0) {
        fprintf(stderr, "%d grapheme test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    printf("Unicode 17 grapheme conformance: %zu cases passed\n", conformance_count);
    return EXIT_SUCCESS;
}
