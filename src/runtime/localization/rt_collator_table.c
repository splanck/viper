//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_collator_table.c
// Purpose: DUCET-lite weight classifier for the Collator class. Maps a
//          Unicode codepoint to a (primary, secondary, tertiary) weight
//          triple using a compact switch/range implementation rather than
//          a giant baked table — keeps binary size down while covering
//          basic Latin, Latin-1 Supplement, Latin Extended-A, and common
//          diacritic composite characters.
//
// Key invariants:
//   - Primary weights use a sparse 16-bit space organized so base Latin
//     letters sort before punctuation and punctuation sorts before digits.
//     This matches Unicode default collation order at the primary level.
//   - Secondary weight 0 means "no diacritic"; 1..7 encode the common
//     Latin diacritic families (grave/acute/circumflex/tilde/diaeresis/
//     ring/cedilla) so strength-2 comparisons distinguish accented forms.
//   - Tertiary weight 0 = lowercase / neutral; 1 = uppercase. Strength-3
//     comparisons differentiate case.
//   - Locale patches are tiny data tables keyed by BCP-47 tag. They apply
//     position overrides — e.g. Swedish places å after z by overriding
//     its primary weight.
//
// Ownership/Lifetime:
//   - All tables are static immortal data.
//
// Links: src/runtime/localization/rt_collator.h (consumer),
//        src/runtime/localization/rt_collator.c (SortKey/Compare impl).
//
//===----------------------------------------------------------------------===//

#include "rt_collator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Primary weight bands (sparse, ordered)
//===----------------------------------------------------------------------===//
//
// Layout (big-endian for byte-wise comparison):
//   0x0001..0x00FF    control + whitespace + structural
//   0x0100..0x01FF    punctuation
//   0x0200..0x020F    digits 0-9
//   0x0300..0x031F    base Latin letters (a/A = 0x0300, b/B = 0x0301, …)
//   0x0400..           Latin Extended / other
//   0x8000..          codepoint-order fallback: primary = cp itself
//
//===----------------------------------------------------------------------===//

#define PRI_SPACE 0x0010u
#define PRI_PUNCT 0x0100u
#define PRI_DIGIT0 0x0200u
#define PRI_LETTER0 0x0300u

//===----------------------------------------------------------------------===//
// Codepoint classifier
//===----------------------------------------------------------------------===//

/// @brief Resolve the base Latin letter (lowercase, A-Z primary column) and
///        the diacritic family for a Latin-1 Supplement / Extended-A
///        composite codepoint.
static int decompose_latin(uint32_t cp, char *base_out, uint16_t *sec_out) {
    char base = 0;
    uint16_t sec = 0;
    switch (cp) {
        // Latin-1 Supplement — lowercase with diacritics
        case 0x00E0:
            base = 'a';
            sec = 1;
            break; // à grave
        case 0x00E1:
            base = 'a';
            sec = 2;
            break; // á acute
        case 0x00E2:
            base = 'a';
            sec = 3;
            break; // â circumflex
        case 0x00E3:
            base = 'a';
            sec = 4;
            break; // ã tilde
        case 0x00E4:
            base = 'a';
            sec = 5;
            break; // ä diaeresis
        case 0x00E5:
            base = 'a';
            sec = 6;
            break; // å ring
        case 0x00E7:
            base = 'c';
            sec = 7;
            break; // ç cedilla
        case 0x00E8:
            base = 'e';
            sec = 1;
            break;
        case 0x00E9:
            base = 'e';
            sec = 2;
            break;
        case 0x00EA:
            base = 'e';
            sec = 3;
            break;
        case 0x00EB:
            base = 'e';
            sec = 5;
            break;
        case 0x00EC:
            base = 'i';
            sec = 1;
            break;
        case 0x00ED:
            base = 'i';
            sec = 2;
            break;
        case 0x00EE:
            base = 'i';
            sec = 3;
            break;
        case 0x00EF:
            base = 'i';
            sec = 5;
            break;
        case 0x00F1:
            base = 'n';
            sec = 4;
            break;
        case 0x00F2:
            base = 'o';
            sec = 1;
            break;
        case 0x00F3:
            base = 'o';
            sec = 2;
            break;
        case 0x00F4:
            base = 'o';
            sec = 3;
            break;
        case 0x00F5:
            base = 'o';
            sec = 4;
            break;
        case 0x00F6:
            base = 'o';
            sec = 5;
            break;
        case 0x00F9:
            base = 'u';
            sec = 1;
            break;
        case 0x00FA:
            base = 'u';
            sec = 2;
            break;
        case 0x00FB:
            base = 'u';
            sec = 3;
            break;
        case 0x00FC:
            base = 'u';
            sec = 5;
            break;
        case 0x00FD:
            base = 'y';
            sec = 2;
            break;
        case 0x00FF:
            base = 'y';
            sec = 5;
            break;
        // Latin-1 Supplement — uppercase with diacritics (tertiary handled outside)
        case 0x00C0:
            base = 'a';
            sec = 1;
            break;
        case 0x00C1:
            base = 'a';
            sec = 2;
            break;
        case 0x00C2:
            base = 'a';
            sec = 3;
            break;
        case 0x00C3:
            base = 'a';
            sec = 4;
            break;
        case 0x00C4:
            base = 'a';
            sec = 5;
            break;
        case 0x00C5:
            base = 'a';
            sec = 6;
            break;
        case 0x00C7:
            base = 'c';
            sec = 7;
            break;
        case 0x00C8:
            base = 'e';
            sec = 1;
            break;
        case 0x00C9:
            base = 'e';
            sec = 2;
            break;
        case 0x00CA:
            base = 'e';
            sec = 3;
            break;
        case 0x00CB:
            base = 'e';
            sec = 5;
            break;
        case 0x00CC:
            base = 'i';
            sec = 1;
            break;
        case 0x00CD:
            base = 'i';
            sec = 2;
            break;
        case 0x00CE:
            base = 'i';
            sec = 3;
            break;
        case 0x00CF:
            base = 'i';
            sec = 5;
            break;
        case 0x00D1:
            base = 'n';
            sec = 4;
            break;
        case 0x00D2:
            base = 'o';
            sec = 1;
            break;
        case 0x00D3:
            base = 'o';
            sec = 2;
            break;
        case 0x00D4:
            base = 'o';
            sec = 3;
            break;
        case 0x00D5:
            base = 'o';
            sec = 4;
            break;
        case 0x00D6:
            base = 'o';
            sec = 5;
            break;
        case 0x00D9:
            base = 'u';
            sec = 1;
            break;
        case 0x00DA:
            base = 'u';
            sec = 2;
            break;
        case 0x00DB:
            base = 'u';
            sec = 3;
            break;
        case 0x00DC:
            base = 'u';
            sec = 5;
            break;
        case 0x00DD:
            base = 'y';
            sec = 2;
            break;
        case 0x00E6:
            base = 'a';
            sec = 8;
            break; // æ -> treat as a-with-special
        case 0x00C6:
            base = 'a';
            sec = 8;
            break; // Æ
        case 0x00F8:
            base = 'o';
            sec = 8;
            break; // ø
        case 0x00D8:
            base = 'o';
            sec = 8;
            break; // Ø
        case 0x00DF:
            base = 's';
            sec = 8;
            break; // ß -> treat as s-special for now
        default:
            return 0;
    }
    if (base_out)
        *base_out = base;
    if (sec_out)
        *sec_out = sec;
    return 1;
}

int rt_collator_codepoint_weights(uint32_t cp,
                                  uint32_t *primary,
                                  uint16_t *secondary,
                                  uint16_t *tertiary) {
    uint32_t p = 0;
    uint16_t s = 0;
    uint16_t t = 0;

    if (cp == 0) {
        if (primary)
            *primary = 0;
        if (secondary)
            *secondary = 0;
        if (tertiary)
            *tertiary = 0;
        return 0;
    }

    // Whitespace / control — sort before printables.
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
        p = PRI_SPACE;
    }
    // ASCII punctuation.
    else if (cp < 0x30 || (cp >= 0x3A && cp <= 0x40) || (cp >= 0x5B && cp <= 0x60) ||
             (cp >= 0x7B && cp <= 0x7E)) {
        p = PRI_PUNCT + (cp & 0x7F);
    }
    // ASCII digits.
    else if (cp >= '0' && cp <= '9') {
        p = PRI_DIGIT0 + (cp - '0');
    }
    // ASCII letters.
    else if (cp >= 'a' && cp <= 'z') {
        p = PRI_LETTER0 + (cp - 'a');
    } else if (cp >= 'A' && cp <= 'Z') {
        p = PRI_LETTER0 + (cp - 'A');
        t = 1; // uppercase marker
    }
    // Latin-1 composite letters.
    else if (cp >= 0x00C0 && cp <= 0x00FF) {
        char base = 0;
        uint16_t sec = 0;
        if (decompose_latin(cp, &base, &sec)) {
            p = PRI_LETTER0 + (uint32_t)(base - 'a');
            s = sec;
            // Uppercase Latin-1 letters occupy 0xC0-0xDE; the odd ×/÷ are
            // handled by the decompose table not returning 1. Detect case:
            if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7)
                t = 1;
        } else {
            // Fallback (e.g. × multiplication sign).
            p = 0x8000u + cp;
        }
    }
    // Latin Extended-A (simplified: treat as fallback to keep the table
    // compact; adding full coverage is a Phase-6+ improvement).
    else if (cp >= 0x0100 && cp <= 0x017F) {
        // Best-effort: attempt base-letter recovery for common pairs.
        if ((cp >= 0x0100 && cp <= 0x0105)) {
            p = PRI_LETTER0 + 0;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0106 && cp <= 0x010D) {
            p = PRI_LETTER0 + 2;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x010E && cp <= 0x0111) {
            p = PRI_LETTER0 + 3;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0112 && cp <= 0x011B) {
            p = PRI_LETTER0 + 4;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x011C && cp <= 0x0123) {
            p = PRI_LETTER0 + 6;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0124 && cp <= 0x0127) {
            p = PRI_LETTER0 + 7;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0128 && cp <= 0x0131) {
            p = PRI_LETTER0 + 8;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0139 && cp <= 0x0142) {
            p = PRI_LETTER0 + 11;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0143 && cp <= 0x0148) {
            p = PRI_LETTER0 + 13;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x014C && cp <= 0x0151) {
            p = PRI_LETTER0 + 14;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0154 && cp <= 0x0159) {
            p = PRI_LETTER0 + 17;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x015A && cp <= 0x0161) {
            p = PRI_LETTER0 + 18;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0162 && cp <= 0x0167) {
            p = PRI_LETTER0 + 19;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0168 && cp <= 0x0173) {
            p = PRI_LETTER0 + 20;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0174 && cp <= 0x0175) {
            p = PRI_LETTER0 + 22;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0176 && cp <= 0x0178) {
            p = PRI_LETTER0 + 24;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else if (cp >= 0x0179 && cp <= 0x017E) {
            p = PRI_LETTER0 + 25;
            s = 9;
            t = (cp & 1) ? 0 : 1;
        } else {
            p = 0x8000u + cp;
        }
    }
    // Everything else: codepoint-order fallback.
    else {
        p = 0x8000u + cp;
        if (primary)
            *primary = p;
        if (secondary)
            *secondary = 0;
        if (tertiary)
            *tertiary = 0;
        return 1;
    }

    if (primary)
        *primary = p;
    if (secondary)
        *secondary = s;
    if (tertiary)
        *tertiary = t;
    return 0;
}

//===----------------------------------------------------------------------===//
// Locale tailorings
//===----------------------------------------------------------------------===//
//
// A tailoring is a small array of (codepoint, primary, secondary, tertiary)
// records that override the default weights for specific characters. This
// lets Swedish push å/ä/ö to the end of the alphabet without disturbing
// en-US comparisons.
//
//===----------------------------------------------------------------------===//

// Swedish: å (U+00E5/U+00C5), ä (U+00E4/U+00C4), ö (U+00F6/U+00D6) sort after z.
// We give them primary weights starting at PRI_LETTER0 + 26 (beyond 'z' which
// is PRI_LETTER0 + 25) so they collate at the end of the alphabet.
static const rt_collator_locale_patch_t g_patches_sv[] = {
    {0x00E5u, PRI_LETTER0 + 26u, 0, 0},
    {0x00C5u, PRI_LETTER0 + 26u, 0, 1},
    {0x00E4u, PRI_LETTER0 + 27u, 0, 0},
    {0x00C4u, PRI_LETTER0 + 27u, 0, 1},
    {0x00F6u, PRI_LETTER0 + 28u, 0, 0},
    {0x00D6u, PRI_LETTER0 + 28u, 0, 1},
};

// German phonebook-style: ä/ö/ü treated as ae/oe/ue (folded); ß as ss.
// For Phase 5 we just give them equal-to-base primary with elevated
// secondary so case-sensitive sort still distinguishes but strength-1
// treats ä and a as equivalent. The decompose_latin default already does
// this; the patch array is therefore empty to document that the default
// applies.
static const rt_collator_locale_patch_t g_patches_de[] = {
    {0, 0, 0, 0}, // Sentinel; count returns 0 below.
};

const rt_collator_locale_patch_t *rt_collator_locale_patches(const char *tag, size_t *out_count) {
    if (out_count)
        *out_count = 0;
    if (!tag)
        return NULL;

    // Swedish: "sv", "sv-SE", "sv-FI"
    if (tag[0] == 's' && tag[1] == 'v' && (tag[2] == '\0' || tag[2] == '-')) {
        if (out_count)
            *out_count = sizeof(g_patches_sv) / sizeof(g_patches_sv[0]);
        return g_patches_sv;
    }

    // German phonebook uses defaults.
    if (tag[0] == 'd' && tag[1] == 'e' && (tag[2] == '\0' || tag[2] == '-')) {
        (void)g_patches_de;
        return NULL;
    }

    return NULL;
}
