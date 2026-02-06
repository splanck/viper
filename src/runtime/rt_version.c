//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_version.c
// Purpose: Semantic version parsing, comparison, and constraint checking.
//          Implements SemVer 2.0.0 spec (https://semver.org/).
//
//===----------------------------------------------------------------------===//

#include "rt_version.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct rt_version_impl
{
    void **vptr;
    int64_t major;
    int64_t minor;
    int64_t patch;
    char *prerelease; // NULL if none
    char *build;      // NULL if none
} rt_version_impl;

static void version_finalizer(void *obj)
{
    rt_version_impl *v = (rt_version_impl *)obj;
    if (!v)
        return;
    free(v->prerelease);
    free(v->build);
}

static char *dup_str(const char *s, size_t len)
{
    char *r = (char *)malloc(len + 1);
    if (!r)
        return NULL;
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

// Parse a non-negative integer from str[*pos], advancing *pos.
// Returns -1 on failure.
static int64_t parse_num(const char *str, size_t len, size_t *pos)
{
    if (*pos >= len || !isdigit((unsigned char)str[*pos]))
        return -1;

    // No leading zeros (except "0" itself)
    if (str[*pos] == '0' && *pos + 1 < len && isdigit((unsigned char)str[*pos + 1]))
        return -1;

    int64_t val = 0;
    while (*pos < len && isdigit((unsigned char)str[*pos]))
    {
        val = val * 10 + (str[*pos] - '0');
        (*pos)++;
    }
    return val;
}

void *rt_version_parse(rt_string str)
{
    if (!str)
        return NULL;
    const char *src = rt_string_cstr(str);
    if (!src)
        return NULL;
    size_t len = strlen(src);
    if (len == 0)
        return NULL;

    // Skip optional leading 'v' or 'V'
    size_t pos = 0;
    if (src[0] == 'v' || src[0] == 'V')
        pos = 1;

    // Parse MAJOR
    int64_t major = parse_num(src, len, &pos);
    if (major < 0)
        return NULL;

    // Expect '.'
    if (pos >= len || src[pos] != '.')
        return NULL;
    pos++;

    // Parse MINOR
    int64_t minor = parse_num(src, len, &pos);
    if (minor < 0)
        return NULL;

    // PATCH is optional - default to 0
    int64_t patch = 0;
    if (pos < len && src[pos] == '.')
    {
        pos++;
        patch = parse_num(src, len, &pos);
        if (patch < 0)
            return NULL;
    }

    // Pre-release: -alpha.1.beta
    char *prerelease = NULL;
    if (pos < len && src[pos] == '-')
    {
        pos++;
        size_t start = pos;
        while (pos < len && src[pos] != '+')
            pos++;
        if (pos > start)
            prerelease = dup_str(src + start, pos - start);
    }

    // Build metadata: +build.42
    char *build = NULL;
    if (pos < len && src[pos] == '+')
    {
        pos++;
        size_t start = pos;
        while (pos < len)
            pos++;
        if (pos > start)
            build = dup_str(src + start, pos - start);
    }

    // Should have consumed everything
    if (pos != len)
    {
        free(prerelease);
        free(build);
        return NULL;
    }

    rt_version_impl *v = (rt_version_impl *)rt_obj_new_i64(0, sizeof(rt_version_impl));
    if (!v)
    {
        free(prerelease);
        free(build);
        return NULL;
    }
    v->major = major;
    v->minor = minor;
    v->patch = patch;
    v->prerelease = prerelease;
    v->build = build;

    rt_obj_set_finalizer(v, version_finalizer);
    return v;
}

int8_t rt_version_is_valid(rt_string str)
{
    void *v = rt_version_parse(str);
    if (!v)
        return 0;
    rt_obj_release_check0(v);
    rt_obj_free(v);
    return 1;
}

int64_t rt_version_major(void *ver)
{
    if (!ver)
        return 0;
    return ((rt_version_impl *)ver)->major;
}

int64_t rt_version_minor(void *ver)
{
    if (!ver)
        return 0;
    return ((rt_version_impl *)ver)->minor;
}

int64_t rt_version_patch(void *ver)
{
    if (!ver)
        return 0;
    return ((rt_version_impl *)ver)->patch;
}

rt_string rt_version_prerelease(void *ver)
{
    if (!ver || !((rt_version_impl *)ver)->prerelease)
        return rt_string_from_bytes("", 0);
    const char *pr = ((rt_version_impl *)ver)->prerelease;
    return rt_string_from_bytes(pr, strlen(pr));
}

rt_string rt_version_build(void *ver)
{
    if (!ver || !((rt_version_impl *)ver)->build)
        return rt_string_from_bytes("", 0);
    const char *b = ((rt_version_impl *)ver)->build;
    return rt_string_from_bytes(b, strlen(b));
}

rt_string rt_version_to_string(void *ver)
{
    if (!ver)
        return rt_string_from_bytes("", 0);
    rt_version_impl *v = (rt_version_impl *)ver;

    char buf[256];
    int written = snprintf(buf,
                           sizeof(buf),
                           "%lld.%lld.%lld",
                           (long long)v->major,
                           (long long)v->minor,
                           (long long)v->patch);
    if (v->prerelease)
        written += snprintf(buf + written, sizeof(buf) - (size_t)written, "-%s", v->prerelease);
    if (v->build)
        written += snprintf(buf + written, sizeof(buf) - (size_t)written, "+%s", v->build);

    return rt_string_from_bytes(buf, (size_t)written);
}

// Compare pre-release identifiers per SemVer spec.
// No pre-release > with pre-release.
// Numeric identifiers compared numerically, alphanumeric lexically.
static int cmp_prerelease(const char *a, const char *b)
{
    // Both NULL -> equal
    if (!a && !b)
        return 0;
    // No pre-release has higher precedence
    if (!a)
        return 1;
    if (!b)
        return -1;

    // Split by '.' and compare each identifier
    const char *pa = a, *pb = b;
    while (*pa || *pb)
    {
        if (!*pa)
            return -1; // Fewer identifiers -> lower precedence
        if (!*pb)
            return 1;

        // Extract next identifier
        const char *ea = strchr(pa, '.');
        const char *eb = strchr(pb, '.');
        size_t la = ea ? (size_t)(ea - pa) : strlen(pa);
        size_t lb = eb ? (size_t)(eb - pb) : strlen(pb);

        // Check if numeric
        int a_num = 1, b_num = 1;
        for (size_t i = 0; i < la; ++i)
            if (!isdigit((unsigned char)pa[i]))
                a_num = 0;
        for (size_t i = 0; i < lb; ++i)
            if (!isdigit((unsigned char)pb[i]))
                b_num = 0;

        if (a_num && b_num)
        {
            // Both numeric: compare as integers
            long long va = 0, vb = 0;
            for (size_t i = 0; i < la; ++i)
                va = va * 10 + (pa[i] - '0');
            for (size_t i = 0; i < lb; ++i)
                vb = vb * 10 + (pb[i] - '0');
            if (va < vb)
                return -1;
            if (va > vb)
                return 1;
        }
        else if (a_num != b_num)
        {
            // Numeric < alphanumeric
            return a_num ? -1 : 1;
        }
        else
        {
            // Both alphanumeric: lexicographic
            size_t minl = la < lb ? la : lb;
            int cmp = memcmp(pa, pb, minl);
            if (cmp != 0)
                return cmp < 0 ? -1 : 1;
            if (la < lb)
                return -1;
            if (la > lb)
                return 1;
        }

        pa += la + (ea ? 1 : 0);
        pb += lb + (eb ? 1 : 0);
    }
    return 0;
}

int64_t rt_version_cmp(void *a, void *b)
{
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    rt_version_impl *va = (rt_version_impl *)a;
    rt_version_impl *vb = (rt_version_impl *)b;

    if (va->major != vb->major)
        return va->major < vb->major ? -1 : 1;
    if (va->minor != vb->minor)
        return va->minor < vb->minor ? -1 : 1;
    if (va->patch != vb->patch)
        return va->patch < vb->patch ? -1 : 1;

    return cmp_prerelease(va->prerelease, vb->prerelease);
}

int8_t rt_version_satisfies(void *ver, rt_string constraint)
{
    if (!ver || !constraint)
        return 0;

    const char *cstr = rt_string_cstr(constraint);
    if (!cstr)
        return 0;
    size_t len = strlen(cstr);
    if (len == 0)
        return 1; // Empty constraint matches all

    rt_version_impl *v = (rt_version_impl *)ver;

    // Parse operator and version
    const char *p = cstr;
    char op[3] = {0};

    if (p[0] == '^')
    {
        // Caret: compatible with (same major, if major > 0)
        p++;
        rt_string vs = rt_string_from_bytes(p, len - 1);
        void *cv = rt_version_parse(vs);
        rt_string_unref(vs);
        if (!cv)
            return 0;

        rt_version_impl *c = (rt_version_impl *)cv;
        int8_t result = 0;
        if (c->major > 0)
        {
            result = (v->major == c->major && rt_version_cmp(ver, cv) >= 0) ? 1 : 0;
        }
        else if (c->minor > 0)
        {
            result =
                (v->major == 0 && v->minor == c->minor && rt_version_cmp(ver, cv) >= 0) ? 1 : 0;
        }
        else
        {
            result = (rt_version_cmp(ver, cv) == 0) ? 1 : 0;
        }
        rt_obj_release_check0(cv);
        rt_obj_free(cv);
        return result;
    }
    else if (p[0] == '~')
    {
        // Tilde: same major.minor
        p++;
        rt_string vs = rt_string_from_bytes(p, strlen(p));
        void *cv = rt_version_parse(vs);
        rt_string_unref(vs);
        if (!cv)
            return 0;

        rt_version_impl *c = (rt_version_impl *)cv;
        int8_t result =
            (v->major == c->major && v->minor == c->minor && rt_version_cmp(ver, cv) >= 0) ? 1 : 0;
        rt_obj_release_check0(cv);
        rt_obj_free(cv);
        return result;
    }

    // Comparison operators: >=, <=, !=, >, <, =
    if (p[0] == '>' && p[1] == '=')
    {
        op[0] = '>';
        op[1] = '=';
        p += 2;
    }
    else if (p[0] == '<' && p[1] == '=')
    {
        op[0] = '<';
        op[1] = '=';
        p += 2;
    }
    else if (p[0] == '!' && p[1] == '=')
    {
        op[0] = '!';
        op[1] = '=';
        p += 2;
    }
    else if (p[0] == '>')
    {
        op[0] = '>';
        p++;
    }
    else if (p[0] == '<')
    {
        op[0] = '<';
        p++;
    }
    else if (p[0] == '=')
    {
        op[0] = '=';
        p++;
    }
    else
    {
        // No operator -> exact match
        op[0] = '=';
    }

    // Skip whitespace after operator
    while (*p == ' ')
        p++;

    rt_string vs = rt_string_from_bytes(p, strlen(p));
    void *cv = rt_version_parse(vs);
    rt_string_unref(vs);
    if (!cv)
        return 0;

    int64_t cmp = rt_version_cmp(ver, cv);
    rt_obj_release_check0(cv);
    rt_obj_free(cv);

    if (op[0] == '>' && op[1] == '=')
        return cmp >= 0 ? 1 : 0;
    if (op[0] == '<' && op[1] == '=')
        return cmp <= 0 ? 1 : 0;
    if (op[0] == '!' && op[1] == '=')
        return cmp != 0 ? 1 : 0;
    if (op[0] == '>')
        return cmp > 0 ? 1 : 0;
    if (op[0] == '<')
        return cmp < 0 ? 1 : 0;
    // '='
    return cmp == 0 ? 1 : 0;
}

rt_string rt_version_bump_major(void *ver)
{
    if (!ver)
        return rt_string_from_bytes("", 0);
    rt_version_impl *v = (rt_version_impl *)ver;
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld.0.0", (long long)(v->major + 1));
    return rt_string_from_bytes(buf, strlen(buf));
}

rt_string rt_version_bump_minor(void *ver)
{
    if (!ver)
        return rt_string_from_bytes("", 0);
    rt_version_impl *v = (rt_version_impl *)ver;
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld.%lld.0", (long long)v->major, (long long)(v->minor + 1));
    return rt_string_from_bytes(buf, strlen(buf));
}

rt_string rt_version_bump_patch(void *ver)
{
    if (!ver)
        return rt_string_from_bytes("", 0);
    rt_version_impl *v = (rt_version_impl *)ver;
    char buf[64];
    snprintf(buf,
             sizeof(buf),
             "%lld.%lld.%lld",
             (long long)v->major,
             (long long)v->minor,
             (long long)(v->patch + 1));
    return rt_string_from_bytes(buf, strlen(buf));
}
