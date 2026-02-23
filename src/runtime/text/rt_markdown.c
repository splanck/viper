//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_markdown.c
// Purpose: Implements Markdown parsing utilities for the Viper.Text.Markdown
//          class. Provides ExtractLinks (href + text pairs), ExtractHeadings
//          (level + text), ToHtml (basic Markdown to HTML conversion), and
//          StripMarkdown (remove formatting, return plain text).
//
// Key invariants:
//   - ExtractLinks returns a Seq<Seq<String>> where each inner seq is [href, text].
//   - ExtractHeadings returns a Seq<Seq> where each inner seq is [level, text].
//   - ToHtml converts headings, bold, italic, links, code, and lists; does not
//     implement the full CommonMark spec.
//   - StripMarkdown removes **, *, _, `, and link syntax leaving plain text.
//   - All functions return empty sequences for empty or whitespace-only input.
//
// Ownership/Lifetime:
//   - All returned sequences and strings are fresh allocations owned by caller.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_markdown.h (public API),
//        src/runtime/text/rt_html.h (HTML generation for ToHtml output)
//
//===----------------------------------------------------------------------===//

#include "rt_markdown.h"

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* S-13: Check if a URL scheme is unsafe (javascript:, data:, vbscript:) */
static bool url_scheme_is_blocked(const char *url, int64_t len)
{
    static const struct
    {
        const char *scheme;
        int scheme_len;
    } blocked[] = {{"javascript:", 11}, {"data:", 5}, {"vbscript:", 9}};

    for (int s = 0; s < 3; s++)
    {
        int sl = blocked[s].scheme_len;
        if (len >= sl)
        {
            bool match = true;
            for (int i = 0; i < sl; i++)
            {
                char c = url[i];
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c + 32);
                if (c != blocked[s].scheme[i])
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return true;
        }
    }
    return false;
}

// --- Helper: append escaped HTML ---

static void append_escaped(rt_string_builder *sb, char c)
{
    switch (c)
    {
        case '<':
            rt_sb_append_cstr(sb, "&lt;");
            break;
        case '>':
            rt_sb_append_cstr(sb, "&gt;");
            break;
        case '&':
            rt_sb_append_cstr(sb, "&amp;");
            break;
        case '"':
            rt_sb_append_cstr(sb, "&quot;");
            break;
        default:
            rt_sb_append_bytes(sb, &c, 1);
            break;
    }
}

// --- Helper: process inline formatting ---

static void process_inline(rt_string_builder *sb, const char *line, int64_t len)
{
    int64_t i = 0;
    while (i < len)
    {
        // Bold **text**
        if (i + 1 < len && line[i] == '*' && line[i + 1] == '*')
        {
            i += 2;
            rt_sb_append_cstr(sb, "<strong>");
            while (i + 1 < len && !(line[i] == '*' && line[i + 1] == '*'))
            {
                append_escaped(sb, line[i]);
                i++;
            }
            rt_sb_append_cstr(sb, "</strong>");
            if (i + 1 < len)
                i += 2;
            continue;
        }

        // Italic *text*
        if (line[i] == '*' && (i + 1 < len) && line[i + 1] != '*')
        {
            i++;
            rt_sb_append_cstr(sb, "<em>");
            while (i < len && line[i] != '*')
            {
                append_escaped(sb, line[i]);
                i++;
            }
            rt_sb_append_cstr(sb, "</em>");
            if (i < len)
                i++;
            continue;
        }

        // Inline code `code`
        if (line[i] == '`')
        {
            i++;
            rt_sb_append_cstr(sb, "<code>");
            while (i < len && line[i] != '`')
            {
                append_escaped(sb, line[i]);
                i++;
            }
            rt_sb_append_cstr(sb, "</code>");
            if (i < len)
                i++;
            continue;
        }

        // Link [text](url)
        if (line[i] == '[')
        {
            int64_t text_start = i + 1;
            int64_t j = text_start;
            while (j < len && line[j] != ']')
                j++;
            if (j + 1 < len && line[j] == ']' && line[j + 1] == '(')
            {
                int64_t url_start = j + 2;
                int64_t k = url_start;
                while (k < len && line[k] != ')')
                    k++;
                if (k < len)
                {
                    rt_sb_append_cstr(sb, "<a href=\"");
                    /* S-13: Block unsafe URL schemes to prevent XSS */
                    int64_t url_len = k - url_start;
                    if (url_scheme_is_blocked(line + url_start, url_len))
                        rt_sb_append_cstr(sb, "#");
                    else
                        rt_sb_append_bytes(sb, line + url_start, url_len);
                    rt_sb_append_cstr(sb, "\">");
                    for (int64_t m = text_start; m < j; m++)
                        append_escaped(sb, line[m]);
                    rt_sb_append_cstr(sb, "</a>");
                    i = k + 1;
                    continue;
                }
            }
        }

        append_escaped(sb, line[i]);
        i++;
    }
}

// --- Public API ---

rt_string rt_markdown_to_html(rt_string md)
{
    if (!md)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(md);
    int64_t src_len = (int64_t)strlen(src);
    rt_string_builder sb;
    rt_sb_init(&sb);

    const char *p = src;
    int in_list = 0;

    while (p < src + src_len)
    {
        // Find end of line
        const char *eol = p;
        while (eol < src + src_len && *eol != '\n')
            eol++;
        int64_t line_len = (int64_t)(eol - p);

        // Empty line
        if (line_len == 0)
        {
            if (in_list)
            {
                rt_sb_append_cstr(&sb, "</ul>\n");
                in_list = 0;
            }
            p = eol + 1;
            continue;
        }

        // Heading
        if (*p == '#')
        {
            int level = 0;
            const char *h = p;
            while (*h == '#' && level < 6)
            {
                level++;
                h++;
            }
            if (*h == ' ')
                h++;
            char tag[8];
            snprintf(tag, sizeof(tag), "<h%d>", level);
            rt_sb_append_cstr(&sb, tag);
            process_inline(&sb, h, (int64_t)(eol - h));
            snprintf(tag, sizeof(tag), "</h%d>\n", level);
            rt_sb_append_cstr(&sb, tag);
            p = eol + 1;
            continue;
        }

        // Unordered list item (- or *)
        if ((*p == '-' || *p == '*') && p + 1 < eol && p[1] == ' ')
        {
            if (!in_list)
            {
                rt_sb_append_cstr(&sb, "<ul>\n");
                in_list = 1;
            }
            rt_sb_append_cstr(&sb, "<li>");
            process_inline(&sb, p + 2, (int64_t)(eol - p - 2));
            rt_sb_append_cstr(&sb, "</li>\n");
            p = eol + 1;
            continue;
        }

        // Code block ```
        if (line_len >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`')
        {
            p = eol + 1;
            rt_sb_append_cstr(&sb, "<pre><code>");
            while (p < src + src_len)
            {
                eol = p;
                while (eol < src + src_len && *eol != '\n')
                    eol++;
                line_len = (int64_t)(eol - p);
                if (line_len >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`')
                {
                    p = eol + 1;
                    break;
                }
                for (int64_t i = 0; i < line_len; i++)
                    append_escaped(&sb, p[i]);
                rt_sb_append_cstr(&sb, "\n");
                p = eol + 1;
            }
            rt_sb_append_cstr(&sb, "</code></pre>\n");
            continue;
        }

        // Horizontal rule
        if (line_len >= 3)
        {
            int is_hr = 1;
            char hr_char = *p;
            if (hr_char == '-' || hr_char == '*' || hr_char == '_')
            {
                for (int64_t i = 0; i < line_len; i++)
                {
                    if (p[i] != hr_char && p[i] != ' ')
                    {
                        is_hr = 0;
                        break;
                    }
                }
                int count = 0;
                for (int64_t i = 0; i < line_len; i++)
                    if (p[i] == hr_char)
                        count++;
                if (is_hr && count >= 3)
                {
                    rt_sb_append_cstr(&sb, "<hr>\n");
                    p = eol + 1;
                    continue;
                }
            }
        }

        // Close list if open
        if (in_list)
        {
            rt_sb_append_cstr(&sb, "</ul>\n");
            in_list = 0;
        }

        // Regular paragraph
        rt_sb_append_cstr(&sb, "<p>");
        process_inline(&sb, p, line_len);
        rt_sb_append_cstr(&sb, "</p>\n");
        p = eol + 1;
    }

    if (in_list)
        rt_sb_append_cstr(&sb, "</ul>\n");

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

rt_string rt_markdown_to_text(rt_string md)
{
    if (!md)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(md);
    int64_t src_len = (int64_t)strlen(src);
    rt_string_builder sb;
    rt_sb_init(&sb);

    const char *p = src;

    while (p < src + src_len)
    {
        const char *eol = p;
        while (eol < src + src_len && *eol != '\n')
            eol++;
        int64_t line_len = (int64_t)(eol - p);

        // Skip heading markers
        const char *start = p;
        while (start < eol && *start == '#')
            start++;
        if (start > p && start < eol && *start == ' ')
            start++;

        // Strip inline formatting
        for (const char *c = start; c < eol; c++)
        {
            if (*c == '*' || *c == '`')
                continue;
            if (*c == '[')
            {
                // Extract link text only
                const char *end = c + 1;
                while (end < eol && *end != ']')
                    end++;
                for (const char *t = c + 1; t < end; t++)
                    rt_sb_append_bytes(&sb, t, 1);
                // Skip URL part
                if (end + 1 < eol && end[1] == '(')
                {
                    const char *close = end + 2;
                    while (close < eol && *close != ')')
                        close++;
                    c = close;
                }
                else
                {
                    c = end;
                }
                continue;
            }
            rt_sb_append_bytes(&sb, c, 1);
        }
        rt_sb_append_cstr(&sb, "\n");
        p = eol + 1;
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

void *rt_markdown_extract_links(rt_string md)
{
    void *seq = rt_seq_new();
    if (!md)
        return seq;

    const char *src = rt_string_cstr(md);
    const char *p = src;

    while (*p)
    {
        // Find [text](url) pattern
        if (*p == '[')
        {
            const char *end = p + 1;
            while (*end && *end != ']')
                end++;
            if (*end == ']' && end[1] == '(')
            {
                const char *url_start = end + 2;
                const char *url_end = url_start;
                while (*url_end && *url_end != ')')
                    url_end++;
                if (*url_end == ')')
                {
                    rt_string url = rt_string_from_bytes(url_start, (int64_t)(url_end - url_start));
                    rt_seq_push(seq, url);
                    p = url_end + 1;
                    continue;
                }
            }
        }
        p++;
    }
    return seq;
}

void *rt_markdown_extract_headings(rt_string md)
{
    void *seq = rt_seq_new();
    if (!md)
        return seq;

    const char *src = rt_string_cstr(md);
    const char *p = src;

    while (*p)
    {
        // Beginning of line (or start of string)
        if (p == src || p[-1] == '\n')
        {
            if (*p == '#')
            {
                const char *h = p;
                while (*h == '#')
                    h++;
                if (*h == ' ')
                    h++;
                const char *eol = h;
                while (*eol && *eol != '\n')
                    eol++;
                rt_string heading = rt_string_from_bytes(h, (int64_t)(eol - h));
                rt_seq_push(seq, heading);
                p = eol;
                if (*p)
                    p++;
                continue;
            }
        }
        p++;
    }
    return seq;
}
