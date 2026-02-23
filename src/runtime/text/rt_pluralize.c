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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

// Irregular plural forms (singular -> plural)
typedef struct
{
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

static int str_ends_with(const char *str, size_t len, const char *suffix)
{
    size_t slen = strlen(suffix);
    if (slen > len)
        return 0;
    return memcmp(str + len - slen, suffix, slen) == 0;
}

static int str_eq_nocase(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int is_uncountable(const char *word)
{
    for (int i = 0; uncountables[i]; ++i)
    {
        if (str_eq_nocase(word, uncountables[i]))
            return 1;
    }
    return 0;
}

rt_string rt_pluralize(rt_string word)
{
    if (!word)
        return rt_string_from_bytes("", 0);
    const char *src = rt_string_cstr(word);
    if (!src)
        return rt_string_from_bytes("", 0);
    size_t len = strlen(src);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Check uncountable
    if (is_uncountable(src))
        return rt_string_from_bytes(src, len);

    // Check irregulars
    for (int i = 0; irregulars[i].singular; ++i)
    {
        if (str_eq_nocase(src, irregulars[i].singular))
            return rt_string_from_bytes(irregulars[i].plural, strlen(irregulars[i].plural));
    }

    // Rules applied in order:
    // -s, -x, -z, -ch, -sh -> +es
    if (str_ends_with(src, len, "s") || str_ends_with(src, len, "x") ||
        str_ends_with(src, len, "z") || str_ends_with(src, len, "ch") ||
        str_ends_with(src, len, "sh"))
    {
        size_t blen = len + 2; // + "es"
        char *buf = (char *)malloc(blen + 1);
        if (!buf)
            rt_trap("Pluralize: memory allocation failed");
        snprintf(buf, blen + 1, "%ses", src);
        rt_string result = rt_string_from_bytes(buf, blen);
        free(buf);
        return result;
    }

    // consonant + y -> ies
    if (len >= 2 && src[len - 1] == 'y')
    {
        char prev = (char)tolower((unsigned char)src[len - 2]);
        if (prev != 'a' && prev != 'e' && prev != 'i' && prev != 'o' && prev != 'u')
        {
            size_t blen = len + 2; // (len-1) + "ies"
            char *buf = (char *)malloc(blen + 1);
            if (!buf)
                rt_trap("Pluralize: memory allocation failed");
            memcpy(buf, src, len - 1);
            memcpy(buf + len - 1, "ies", 3);
            buf[blen] = '\0';
            rt_string result = rt_string_from_bytes(buf, blen);
            free(buf);
            return result;
        }
    }

    // -f -> -ves (but not already covered by irregulars)
    if (len >= 2 && src[len - 1] == 'f' && src[len - 2] != 'f')
    {
        size_t blen = len + 2; // (len-1) + "ves"
        char *buf = (char *)malloc(blen + 1);
        if (!buf)
            rt_trap("Pluralize: memory allocation failed");
        memcpy(buf, src, len - 1);
        memcpy(buf + len - 1, "ves", 3);
        buf[blen] = '\0';
        rt_string result = rt_string_from_bytes(buf, blen);
        free(buf);
        return result;
    }

    // -fe -> -ves
    if (str_ends_with(src, len, "fe"))
    {
        size_t blen = len + 1; // (len-2) + "ves"
        char *buf = (char *)malloc(blen + 1);
        if (!buf)
            rt_trap("Pluralize: memory allocation failed");
        memcpy(buf, src, len - 2);
        memcpy(buf + len - 2, "ves", 3);
        buf[blen] = '\0';
        rt_string result = rt_string_from_bytes(buf, blen);
        free(buf);
        return result;
    }

    // -o -> -oes for certain words (simplified: consonant + o -> oes)
    if (len >= 2 && src[len - 1] == 'o')
    {
        char prev = (char)tolower((unsigned char)src[len - 2]);
        if (prev != 'a' && prev != 'e' && prev != 'i' && prev != 'o' && prev != 'u')
        {
            size_t blen = len + 2; // + "es"
            char *buf = (char *)malloc(blen + 1);
            if (!buf)
                rt_trap("Pluralize: memory allocation failed");
            snprintf(buf, blen + 1, "%ses", src);
            rt_string result = rt_string_from_bytes(buf, blen);
            free(buf);
            return result;
        }
    }

    // Default: add -s
    {
        size_t blen = len + 1; // + "s"
        char *buf = (char *)malloc(blen + 1);
        if (!buf)
            rt_trap("Pluralize: memory allocation failed");
        snprintf(buf, blen + 1, "%ss", src);
        rt_string result = rt_string_from_bytes(buf, blen);
        free(buf);
        return result;
    }
}

rt_string rt_singularize(rt_string word)
{
    if (!word)
        return rt_string_from_bytes("", 0);
    const char *src = rt_string_cstr(word);
    if (!src)
        return rt_string_from_bytes("", 0);
    size_t len = strlen(src);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    // Check uncountable
    if (is_uncountable(src))
        return rt_string_from_bytes(src, len);

    // Check irregulars (reverse lookup)
    for (int i = 0; irregulars[i].singular; ++i)
    {
        if (str_eq_nocase(src, irregulars[i].plural))
            return rt_string_from_bytes(irregulars[i].singular, strlen(irregulars[i].singular));
    }

    // -ves -> -f or -fe
    if (str_ends_with(src, len, "ves") && len > 3)
    {
        // Try -f first (e.g., "wolves" -> "wolf")
        size_t blen = len - 2; // (len-3) chars + 'f' + null
        char *buf = (char *)malloc(blen + 1);
        if (!buf)
            rt_trap("Singularize: memory allocation failed");
        memcpy(buf, src, len - 3);
        buf[len - 3] = 'f';
        buf[blen] = '\0';
        rt_string result = rt_string_from_bytes(buf, blen);
        free(buf);
        return result;
    }

    // -ies -> -y
    if (str_ends_with(src, len, "ies") && len > 3)
    {
        size_t blen = len - 2; // (len-3) chars + 'y'
        char *buf = (char *)malloc(blen + 1);
        if (!buf)
            rt_trap("Singularize: memory allocation failed");
        memcpy(buf, src, len - 3);
        buf[len - 3] = 'y';
        buf[blen] = '\0';
        rt_string result = rt_string_from_bytes(buf, blen);
        free(buf);
        return result;
    }

    // -ses, -xes, -zes, -ches, -shes -> remove -es
    if (str_ends_with(src, len, "shes") || str_ends_with(src, len, "ches"))
    {
        return rt_string_from_bytes(src, len - 2);
    }
    if (str_ends_with(src, len, "ses") || str_ends_with(src, len, "xes") ||
        str_ends_with(src, len, "zes"))
    {
        return rt_string_from_bytes(src, len - 2);
    }

    // -oes -> -o
    if (str_ends_with(src, len, "oes") && len > 3)
    {
        return rt_string_from_bytes(src, len - 2);
    }

    // -s (but not -ss) -> remove -s
    if (len > 1 && src[len - 1] == 's' && src[len - 2] != 's')
    {
        return rt_string_from_bytes(src, len - 1);
    }

    // Already singular
    return rt_string_from_bytes(src, len);
}

rt_string rt_pluralize_count(int64_t count, rt_string word)
{
    if (!word)
        return rt_string_from_bytes("", 0);

    rt_string noun = (count == 1 || count == -1) ? rt_string_ref(word) : rt_pluralize(word);
    const char *nstr = rt_string_cstr(noun);
    if (!nstr)
        nstr = "";

    // 21 = max chars for int64 (sign + 19 digits) + 1 space + word + null
    size_t blen = 21 + strlen(nstr);
    char *buf = (char *)malloc(blen + 1);
    if (!buf)
    {
        rt_string_unref(noun);
        rt_trap("Pluralize: memory allocation failed");
    }
    int written = snprintf(buf, blen + 1, "%lld %s", (long long)count, nstr);
    rt_string_unref(noun);
    rt_string result = rt_string_from_bytes(buf, (size_t)(written > 0 ? written : 0));
    free(buf);
    return result;
}
