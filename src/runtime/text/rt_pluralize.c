//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_pluralize.c
// Purpose: Implements English noun pluralization and singularization for the
//          Viper.Text.Pluralize class. Handles regular inflection rules
//          (e.g. -s, -es, -ies), common irregular forms (child/children,
//          mouse/mice), and uncountable nouns (sheep, fish).
//
// Key invariants:
//   - Irregular forms are checked before applying regular suffix rules.
//   - Uncountable nouns (mass nouns) return the input unchanged.
//   - Pluralize(1, "cat") returns "1 cat"; Pluralize(2, "cat") returns "2 cats".
//   - Rules are English-specific; other languages are not supported.
//   - Case of the first letter is preserved in the output.
//   - All lookups are case-insensitive for the irregular/uncountable tables.
//
// Ownership/Lifetime:
//   - Returned strings are fresh rt_string allocations owned by the caller.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_pluralize.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_pluralize.h"

#include "rt_internal.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

// Irregular plural forms (singular -> plural)
typedef struct {
    const char *singular;
    const char *plural;
} irregular_t;

static const irregular_t irregulars[] = {{"child", "children"},
                                         {"foot", "feet"},
                                         {"goose", "geese"},
                                         {"man", "men"},
                                         {"mouse", "mice"},
                                         {"ox", "oxen"},
                                         {"person", "people"},
                                         {"tooth", "teeth"},
                                         {"woman", "women"},
                                         {"cactus", "cacti"},
                                         {"focus", "foci"},
                                         {"fungus", "fungi"},
                                         {"nucleus", "nuclei"},
                                         {"radius", "radii"},
                                         {"stimulus", "stimuli"},
                                         {"analysis", "analyses"},
                                         {"basis", "bases"},
                                         {"crisis", "crises"},
                                         {"diagnosis", "diagnoses"},
                                         {"thesis", "theses"},
                                         {"phenomenon", "phenomena"},
                                         {"criterion", "criteria"},
                                         {"datum", "data"},
                                         {"medium", "media"},
                                         {"appendix", "appendices"},
                                         {"index", "indices"},
                                         {"matrix", "matrices"},
                                         {"vertex", "vertices"},
                                         {"die", "dice"},
                                         {"leaf", "leaves"},
                                         {"life", "lives"},
                                         {"knife", "knives"},
                                         {"wife", "wives"},
                                         {"half", "halves"},
                                         {"wolf", "wolves"},
                                         {"shelf", "shelves"},
                                         {"self", "selves"},
                                         {NULL, NULL}};

// Uncountable nouns
static const char *uncountables[] = {
    "sheep",   "fish",        "deer",      "series",   "species",  "money",
    "rice",    "information", "equipment", "news",     "advice",   "furniture",
    "luggage", "traffic",     "music",     "software", "hardware", "knowledge",
    "weather", "research",    "evidence",  "homework", NULL};

static int str_ends_with_ci(const char *str, size_t len, const char *suffix) {
    size_t slen = strlen(suffix);
    if (slen > len)
        return 0;
    const char *tail = str + len - slen;
    for (size_t i = 0; i < slen; i++) {
        if (tolower((unsigned char)tail[i]) != tolower((unsigned char)suffix[i]))
            return 0;
    }
    return 1;
}

/// @brief Case-insensitive ASCII string comparison (`a == b` with `toLower` on both sides).
/// @details Used to match user input against the irregular and
///          uncountable word tables, which are stored lowercase.
///          ASCII-only — locale-aware folds (e.g. Turkish `İ`) are
///          out of scope.
static int str_eq_nocase_len(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    if (a_len != b_len)
        return 0;
    for (size_t i = 0; i < a_len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static int is_ascii_all_caps_word(const char *src, size_t len) {
    int saw_alpha = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalpha(c)) {
            saw_alpha = 1;
            if (islower(c))
                return 0;
        }
    }
    return saw_alpha;
}

static rt_string inflection_with_input_case(const char *src,
                                            size_t src_len,
                                            const char *replacement) {
    size_t len = strlen(replacement);
    char *buf = (char *)malloc(len + 1);
    if (!buf)
        rt_trap("Pluralize: memory allocation failed");
    memcpy(buf, replacement, len + 1);

    if (is_ascii_all_caps_word(src, src_len)) {
        for (size_t i = 0; i < len; i++)
            buf[i] = (char)toupper((unsigned char)buf[i]);
    } else if (src_len > 0 && isupper((unsigned char)src[0]) && len > 0) {
        buf[0] = (char)toupper((unsigned char)buf[0]);
    }

    rt_string result = rt_string_from_bytes(buf, len);
    free(buf);
    return result;
}

static void copy_suffix_with_input_case(char *dst,
                                        const char *src,
                                        size_t src_len,
                                        const char *suffix,
                                        size_t suffix_len) {
    memcpy(dst, suffix, suffix_len);
    if (is_ascii_all_caps_word(src, src_len)) {
        for (size_t i = 0; i < suffix_len; i++)
            dst[i] = (char)toupper((unsigned char)dst[i]);
    }
}

static rt_string append_suffix(const char *src, size_t len, const char *suffix) {
    size_t suffix_len = strlen(suffix);
    size_t blen = len + suffix_len;
    char *buf = (char *)malloc(blen + 1);
    if (!buf)
        rt_trap("Pluralize: memory allocation failed");
    memcpy(buf, src, len);
    copy_suffix_with_input_case(buf + len, src, len, suffix, suffix_len);
    buf[blen] = '\0';
    rt_string result = rt_string_from_bytes(buf, blen);
    free(buf);
    return result;
}

static rt_string replace_suffix(const char *src,
                                size_t len,
                                size_t remove_len,
                                const char *replacement) {
    size_t replacement_len = strlen(replacement);
    size_t stem_len = remove_len > len ? 0 : len - remove_len;
    size_t blen = stem_len + replacement_len;
    char *buf = (char *)malloc(blen + 1);
    if (!buf)
        rt_trap("Pluralize: memory allocation failed");
    memcpy(buf, src, stem_len);
    copy_suffix_with_input_case(buf + stem_len, src, len, replacement, replacement_len);
    buf[blen] = '\0';
    rt_string result = rt_string_from_bytes(buf, blen);
    free(buf);
    return result;
}

/// @brief Test whether `word` is in the uncountable-noun list (case-insensitive).
/// @details Linear scan — the table is small (~22 entries) so a hash
///          lookup wouldn't pay off. Words like "sheep", "rice",
///          "information" pluralize to themselves, so the caller
///          short-circuits with "return the word as-is" on a hit.
static int is_uncountable(const char *word, size_t len) {
    for (int i = 0; uncountables[i]; ++i) {
        if (str_eq_nocase_len(word, len, uncountables[i]))
            return 1;
    }
    return 0;
}

/// @brief Convert an English noun from singular to plural form.
/// @details Three-tier rule application:
///          1. **Uncountable** ("sheep", "rice", ...) → return as-is.
///          2. **Irregular** ("child" → "children", "person" → "people")
///             → lookup in the irregular table.
///          3. **Regular suffix rules** in priority order:
///             - `-s/-x/-z/-ch/-sh` → `+es` ("box" → "boxes")
///             - consonant + `y` → `-y +ies` ("city" → "cities")
///             - `-f/-fe` → `-f/-fe +ves` ("life" → "lives")
///             - default → `+s` ("cat" → "cats")
///          Case is preserved for the original portion of the word.
rt_string rt_pluralize(rt_string word) {
    if (!word)
        return rt_string_from_bytes("", 0);
    const char *src = rt_string_cstr(word);
    if (!src)
        return rt_string_from_bytes("", 0);
    size_t len = (size_t)rt_str_len(word);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Check uncountable
    if (is_uncountable(src, len))
        return rt_string_from_bytes(src, len);

    // Check irregulars
    for (int i = 0; irregulars[i].singular; ++i) {
        if (str_eq_nocase_len(src, len, irregulars[i].singular))
            return inflection_with_input_case(src, len, irregulars[i].plural);
    }

    // Rules applied in order:
    // -s, -x, -z, -ch, -sh -> +es
    if (str_ends_with_ci(src, len, "s") || str_ends_with_ci(src, len, "x") ||
        str_ends_with_ci(src, len, "z") || str_ends_with_ci(src, len, "ch") ||
        str_ends_with_ci(src, len, "sh")) {
        return append_suffix(src, len, "es");
    }

    // consonant + y -> ies
    if (len >= 2 && tolower((unsigned char)src[len - 1]) == 'y') {
        char prev = (char)tolower((unsigned char)src[len - 2]);
        if (prev != 'a' && prev != 'e' && prev != 'i' && prev != 'o' && prev != 'u') {
            return replace_suffix(src, len, 1, "ies");
        }
    }

    // -f -> -ves (but not already covered by irregulars)
    if (len >= 2 && tolower((unsigned char)src[len - 1]) == 'f' &&
        tolower((unsigned char)src[len - 2]) != 'f') {
        return replace_suffix(src, len, 1, "ves");
    }

    // -fe -> -ves
    if (str_ends_with_ci(src, len, "fe")) {
        return replace_suffix(src, len, 2, "ves");
    }

    // -o -> -oes for certain words (simplified: consonant + o -> oes)
    if (len >= 2 && tolower((unsigned char)src[len - 1]) == 'o') {
        char prev = (char)tolower((unsigned char)src[len - 2]);
        if (prev != 'a' && prev != 'e' && prev != 'i' && prev != 'o' && prev != 'u') {
            return append_suffix(src, len, "es");
        }
    }

    // Default: add -s
    return append_suffix(src, len, "s");
}

/// @brief Convert an English plural noun back to singular form.
/// @details Rule order mirrors `rt_pluralize` in reverse:
///          1. Uncountable → return as-is.
///          2. Irregular table → reverse lookup ("children" → "child").
///          3. Suffix peeling:
///             - `-ves` → `-f` ("wolves" → "wolf").
///             - `-ies` → `-y` ("cities" → "city").
///             - `-shes/-ches/-ses/-xes/-zes/-oes` → strip `-es`.
///             - bare `-s` (but not `-ss`) → strip.
///          For ambiguous forms ("foxes" could come from "fox" or
///          "foxe"), the rules pick the more common reverse — never
///          perfect but matches typical English.
rt_string rt_singularize(rt_string word) {
    if (!word)
        return rt_string_from_bytes("", 0);
    const char *src = rt_string_cstr(word);
    if (!src)
        return rt_string_from_bytes("", 0);
    size_t len = (size_t)rt_str_len(word);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Check uncountable
    if (is_uncountable(src, len))
        return rt_string_from_bytes(src, len);

    // Check irregulars (reverse lookup)
    for (int i = 0; irregulars[i].singular; ++i) {
        if (str_eq_nocase_len(src, len, irregulars[i].plural))
            return inflection_with_input_case(src, len, irregulars[i].singular);
    }

    // -ves -> -f or -fe
    if (str_ends_with_ci(src, len, "ves") && len > 3) {
        return replace_suffix(src, len, 3, "f");
    }

    // -ies -> -y
    if (str_ends_with_ci(src, len, "ies") && len > 3) {
        return replace_suffix(src, len, 3, "y");
    }

    // -ses, -xes, -zes, -ches, -shes -> remove -es
    if (str_ends_with_ci(src, len, "shes") || str_ends_with_ci(src, len, "ches")) {
        return rt_string_from_bytes(src, len - 2);
    }
    if (str_ends_with_ci(src, len, "ses") || str_ends_with_ci(src, len, "xes") ||
        str_ends_with_ci(src, len, "zes")) {
        return rt_string_from_bytes(src, len - 2);
    }

    // -oes -> -o
    if (str_ends_with_ci(src, len, "oes") && len > 3) {
        return rt_string_from_bytes(src, len - 2);
    }

    // -s (but not -ss) -> remove -s
    if (len > 1 && tolower((unsigned char)src[len - 1]) == 's' &&
        tolower((unsigned char)src[len - 2]) != 's') {
        return rt_string_from_bytes(src, len - 1);
    }

    // Already singular
    return rt_string_from_bytes(src, len);
}

/// @brief Format a count + noun pair, pluralizing the noun based on the count.
/// @details Returns "1 item" for ±1, "5 items" / "0 items" otherwise.
///          Matches the convention of natural English where "0 items"
///          and "5 items" both use the plural form (unlike Russian or
///          Polish which have separate "few" / "many" forms — out of
///          scope here).
rt_string rt_pluralize_count(int64_t count, rt_string word) {
    if (!word)
        return rt_string_from_bytes("", 0);

    rt_string noun = (count == 1 || count == -1) ? rt_string_ref(word) : rt_pluralize(word);
    const char *nstr = rt_string_cstr(noun);
    if (!nstr)
        nstr = "";

    size_t noun_len = (size_t)rt_str_len(noun);
    char count_buf[32];
    int count_len = snprintf(count_buf, sizeof(count_buf), "%lld", (long long)count);
    if (count_len < 0) {
        rt_string_unref(noun);
        rt_trap("Pluralize: count formatting failed");
    }
    if ((size_t)count_len > SIZE_MAX - noun_len - 1) {
        rt_string_unref(noun);
        rt_trap("Pluralize: output length overflow");
    }

    size_t blen = (size_t)count_len + 1 + noun_len;
    char *buf = (char *)malloc(blen + 1);
    if (!buf) {
        rt_string_unref(noun);
        rt_trap("Pluralize: memory allocation failed");
    }
    memcpy(buf, count_buf, (size_t)count_len);
    buf[count_len] = ' ';
    memcpy(buf + count_len + 1, nstr, noun_len);
    buf[blen] = '\0';
    rt_string_unref(noun);
    rt_string result = rt_string_from_bytes(buf, blen);
    free(buf);
    return result;
}
