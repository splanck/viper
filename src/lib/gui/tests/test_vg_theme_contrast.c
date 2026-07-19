//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_theme_contrast.c
// Purpose: WCAG 2 contrast gate for every built-in theme palette. Guards any
//          future palette retune: normal-text pairings must reach 4.5:1 and
//          large-text/UI-component pairings must reach 3.0:1.
// Key invariants:
//   - Palette-agnostic: asserts ratios via theme accessors, never raw values,
//     so retunes stay safe without touching this test.
//   - Disabled text is exempt (WCAG 2 incidental-text exception).
// Links: lib/gui/src/core/vg_theme.c,
//        src/runtime/graphics/gui/rt_gui_theme.c (runtime-side 2-pair gate)
//
//===----------------------------------------------------------------------===//

#include "vg_theme.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int g_passed = 0;
static int g_failed = 0;

/// @brief sRGB channel to linear light, per WCAG 2 relative-luminance math.
static double channel_linear(uint8_t value) {
    double normalized = (double)value / 255.0;
    if (normalized <= 0.04045)
        return normalized / 12.92;
    return pow((normalized + 0.055) / 1.055, 2.4);
}

/// @brief WCAG 2 contrast ratio between two packed 0x00RRGGBB colors.
static double contrast_ratio(uint32_t a, uint32_t b) {
    double la = 0.2126 * channel_linear((uint8_t)(a >> 16)) +
                0.7152 * channel_linear((uint8_t)(a >> 8)) + 0.0722 * channel_linear((uint8_t)a);
    double lb = 0.2126 * channel_linear((uint8_t)(b >> 16)) +
                0.7152 * channel_linear((uint8_t)(b >> 8)) + 0.0722 * channel_linear((uint8_t)b);
    double lighter = la > lb ? la : lb;
    double darker = la > lb ? lb : la;
    return (lighter + 0.05) / (darker + 0.05);
}

static void check_pair(const char *theme_name,
                       const char *pair_name,
                       uint32_t fg,
                       uint32_t bg,
                       double minimum) {
    double ratio = contrast_ratio(fg, bg);
    if (ratio + 1e-9 >= minimum) {
        ++g_passed;
        return;
    }
    ++g_failed;
    printf("FAIL %s %-28s %.2f < %.1f (fg=0x%06X bg=0x%06X)\n",
           theme_name,
           pair_name,
           ratio,
           minimum,
           fg,
           bg);
}

static void check_theme(const vg_theme_t *theme) {
    const vg_color_scheme_t *c = &theme->colors;
    const char *n = theme->name;

    // Normal body text: 4.5:1.
    check_pair(n, "fg_primary/bg_primary", c->fg_primary, c->bg_primary, 4.5);
    check_pair(n, "fg_primary/bg_secondary", c->fg_primary, c->bg_secondary, 4.5);
    check_pair(n, "fg_primary/bg_tertiary", c->fg_primary, c->bg_tertiary, 4.5);
    check_pair(n, "fg_primary/bg_hover", c->fg_primary, c->bg_hover, 4.5);
    check_pair(n, "fg_primary/bg_selected", c->fg_primary, c->bg_selected, 4.5);
    check_pair(n, "fg_primary/bg_active", c->fg_primary, c->bg_active, 4.5);
    check_pair(n, "fg_secondary/bg_primary", c->fg_secondary, c->bg_primary, 4.5);
    check_pair(n, "fg_secondary/bg_secondary", c->fg_secondary, c->bg_secondary, 4.5);
    check_pair(n, "fg_placeholder/bg_primary", c->fg_placeholder, c->bg_primary, 4.5);
    check_pair(n, "fg_link/bg_primary", c->fg_link, c->bg_primary, 4.5);

    // Syntax colors render as normal-size code text on the editor field.
    check_pair(n, "syntax_keyword/bg_primary", c->syntax_keyword, c->bg_primary, 4.5);
    check_pair(n, "syntax_type/bg_primary", c->syntax_type, c->bg_primary, 4.5);
    check_pair(n, "syntax_function/bg_primary", c->syntax_function, c->bg_primary, 4.5);
    check_pair(n, "syntax_variable/bg_primary", c->syntax_variable, c->bg_primary, 4.5);
    check_pair(n, "syntax_string/bg_primary", c->syntax_string, c->bg_primary, 4.5);
    check_pair(n, "syntax_number/bg_primary", c->syntax_number, c->bg_primary, 4.5);
    check_pair(n, "syntax_comment/bg_primary", c->syntax_comment, c->bg_primary, 4.5);
    check_pair(n, "syntax_operator/bg_primary", c->syntax_operator, c->bg_primary, 4.5);
    check_pair(n, "syntax_error/bg_primary", c->syntax_error, c->bg_primary, 4.5);

    // Hint text and non-text UI indicators: 3.0:1.
    check_pair(n, "fg_tertiary/bg_primary", c->fg_tertiary, c->bg_primary, 3.0);
    check_pair(n, "accent_primary/bg_primary", c->accent_primary, c->bg_primary, 3.0);
    check_pair(n, "accent_danger/bg_primary", c->accent_danger, c->bg_primary, 3.0);
    check_pair(n, "accent_warning/bg_primary", c->accent_warning, c->bg_primary, 3.0);
    check_pair(n, "accent_success/bg_primary", c->accent_success, c->bg_primary, 3.0);
    check_pair(n, "accent_info/bg_primary", c->accent_info, c->bg_primary, 3.0);
    check_pair(n, "border_focus/bg_primary", c->border_focus, c->bg_primary, 3.0);

    // Selection visibility: the selected/hover fields must separate from the
    // base editor field so highlighted rows are perceivable (non-text 3.0:1
    // is far too strong for adjacent fills; require a mild 1.2:1 step).
    check_pair(n, "bg_selected vs bg_primary", c->bg_selected, c->bg_primary, 1.2);
    check_pair(n, "bg_hover vs bg_primary", c->bg_hover, c->bg_primary, 1.2);
}

int main(void) {
    check_theme(vg_theme_dark());
    check_theme(vg_theme_light());

    printf("test_vg_theme_contrast: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
