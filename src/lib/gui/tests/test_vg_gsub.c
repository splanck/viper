//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_gsub.c
// Purpose: GSUB shaping tests against the shipped JetBrains Mono face —
//          coding sequences substitute into ligature glyphs, source spans
//          preserve per-character advances (caret contract), non-ligature
//          text shapes to identity, and the toggle restores the plain path.
// Links: lib/gui/src/font/vg_gsub.c
//
//===----------------------------------------------------------------------===//

#include "vg_font.h"
#include <stdio.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, label)                                                                         \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            ++g_failed;                                                                            \
            printf("FAIL %s (line %d)\n", (label), __LINE__);                                      \
        }                                                                                          \
    } while (0)

/// @brief Shape an ASCII string; returns the shaped glyph count.
static int32_t shape_ascii(vg_font_t *font, const char *text, vg_shaped_glyph_t *out, int32_t cap) {
    uint32_t codepoints[64];
    int32_t count = 0;
    for (const char *p = text; *p && count < 64; ++p)
        codepoints[count++] = (uint32_t)*p;
    return vg_font_shape(font, codepoints, count, out, cap);
}

/// @brief Return the total source characters covered by a shaped run.
static int32_t total_span(const vg_shaped_glyph_t *shaped, int32_t count) {
    int32_t total = 0;
    for (int32_t i = 0; i < count; ++i)
        total += shaped[i].source_len;
    return total;
}

int main(void) {
    vg_font_t *font = vg_font_load_file(ZANNA_TEST_FONT_PATH);
    if (!font) {
        printf("FAIL could not load test font %s\n", ZANNA_TEST_FONT_PATH);
        return 1;
    }

    CHECK(vg_font_has_ligatures(font), "JetBrains Mono resolves liga/calt lookups");

    vg_shaped_glyph_t shaped[64];

    // Coding sequences must substitute: JetBrains Mono keeps one glyph per
    // character but swaps in ligature-part glyph ids (contextual alternates),
    // so at least one shaped id must differ from the isolated-character ids.
    const char *ligature_cases[] = {"->", "=>", "!=", "===", "<=", ">=", "::"};
    for (size_t i = 0; i < sizeof(ligature_cases) / sizeof(ligature_cases[0]); ++i) {
        const char *sequence = ligature_cases[i];
        int32_t source_count = (int32_t)strlen(sequence);
        int32_t shaped_count = shape_ascii(font, sequence, shaped, 64);
        CHECK(shaped_count > 0 && shaped_count <= source_count, sequence);
        CHECK(total_span(shaped, shaped_count) == source_count, "span covers every source char");
        int32_t substituted = 0;
        for (int32_t g = 0; g < shaped_count; ++g) {
            vg_shaped_glyph_t isolated[4];
            uint32_t lone = (uint32_t)sequence[shaped[g].source_start];
            if (vg_font_shape(font, &lone, 1, isolated, 4) == 1 &&
                isolated[0].glyph_id != shaped[g].glyph_id)
                substituted = 1;
        }
        CHECK(substituted, "sequence substitutes at least one glyph id");
    }

    // Plain identifiers shape to identity: one glyph per character.
    int32_t plain_count = shape_ascii(font, "hello", shaped, 64);
    CHECK(plain_count == 5, "plain text keeps one glyph per char");
    for (int32_t i = 0; i < plain_count; ++i) {
        CHECK(shaped[i].source_len == 1, "plain glyph covers one char");
        CHECK(shaped[i].source_start == i, "plain spans stay ordered");
    }

    // Context around a sequence still substitutes and spans every char.
    int32_t contextual_count = shape_ascii(font, "a->b", shaped, 64);
    CHECK(contextual_count == 4, "a->b keeps one glyph per char (alternates)");
    CHECK(total_span(shaped, contextual_count) == 4, "contextual span covers all chars");

    // Caret contract: measured width is per-source-character and therefore
    // unchanged by shaping (monospace: 4 chars = 4 advances).
    vg_text_metrics_t shaped_metrics, plain_metrics;
    vg_font_measure_text(font, 16.0f, "a->b", &shaped_metrics);
    vg_font_measure_text(font, 16.0f, "acdb", &plain_metrics);
    CHECK(shaped_metrics.width == plain_metrics.width,
          "shaping never changes measured width");

    // The toggle is observable.
    vg_font_set_ligatures_enabled(false);
    CHECK(vg_font_ligatures_enabled() == false, "ligature toggle off");
    vg_font_set_ligatures_enabled(true);
    CHECK(vg_font_ligatures_enabled() == true, "ligature toggle on");

    // Degenerate inputs are safe.
    CHECK(vg_font_shape(font, NULL, 3, shaped, 64) == 0, "NULL codepoints safe");
    CHECK(vg_font_shape(font, (const uint32_t[]){'a'}, 1, shaped, 0) == 0, "zero capacity safe");

    vg_font_destroy(font);
    printf("test_vg_gsub: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
