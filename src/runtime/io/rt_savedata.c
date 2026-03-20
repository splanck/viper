//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_savedata.c
// Purpose: Cross-platform game save/load persistence. Key-value pairs stored
//   as JSON in platform-appropriate directories:
//     macOS:   ~/Library/Application Support/Viper/<game>/save.json
//     Linux:   ~/.local/share/viper/<game>/save.json
//     Windows: %APPDATA%\Viper\<game>\save.json
//
// Key invariants:
//   - Keys are unique; last-write wins.
//   - Save writes atomically (open, write, close).
//   - Load replaces all in-memory data with file contents.
//   - All internal strings are heap-allocated via strdup/malloc.
//
// Ownership/Lifetime:
//   - SaveData is GC-managed; finalizer frees all entries and path strings.
//   - Keys and string values are strdup'd copies; freed in finalizer/remove.
//
// Links: rt_savedata.h (public API), rt_dir.h (directory creation),
//        rt_string_builder.h (JSON output), rt_json_stream.h (JSON input)
//
//===----------------------------------------------------------------------===//

#include "rt_savedata.h"
#include "rt_dir.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern void rt_trap(const char *msg);

/* JSON stream parser (from rt_json_stream.h / text module) */
extern void *rt_json_stream_new(rt_string json);
extern int64_t rt_json_stream_next(void *parser);
extern rt_string rt_json_stream_string_value(void *parser);
extern double rt_json_stream_number_value(void *parser);

/* Token types */
#define TOK_OBJECT_START 1
#define TOK_OBJECT_END 2
#define TOK_KEY 5
#define TOK_STRING 6
#define TOK_NUMBER 7
#define TOK_END 11

//=========================================================================
// Internal Data Structures
//=========================================================================

typedef enum
{
    SAVE_INT = 0,
    SAVE_STR = 1
} SaveEntryType;

typedef struct SaveEntry
{
    char *key;
    SaveEntryType type;
    int64_t int_val;
    char *str_val;
    struct SaveEntry *next;
} SaveEntry;

typedef struct
{
    char *game_name;
    char *file_path;
    SaveEntry *entries;
} rt_savedata_impl;

//=========================================================================
// Internal Helpers
//=========================================================================

static SaveEntry *find_entry(rt_savedata_impl *sd, const char *key, size_t key_len)
{
    SaveEntry *e = sd->entries;
    while (e)
    {
        if (strlen(e->key) == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

static void free_entry(SaveEntry *e)
{
    if (e)
    {
        free(e->key);
        free(e->str_val);
        free(e);
    }
}

static void free_all_entries(rt_savedata_impl *sd)
{
    SaveEntry *e = sd->entries;
    while (e)
    {
        SaveEntry *next = e->next;
        free_entry(e);
        e = next;
    }
    sd->entries = NULL;
}

static char *get_home_dir(void)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (home)
        return strdup(home);
    const char *drive = getenv("HOMEDRIVE");
    const char *path = getenv("HOMEPATH");
    if (drive && path)
    {
        size_t len = strlen(drive) + strlen(path) + 1;
        char *buf = (char *)malloc(len);
        if (buf)
            snprintf(buf, len, "%s%s", drive, path);
        return buf;
    }
    return strdup(".");
#else
    const char *home = getenv("HOME");
    if (home)
        return strdup(home);
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
        return strdup(pw->pw_dir);
    return strdup(".");
#endif
}

/// @brief Validate game_name contains only safe characters for use in file paths.
/// Rejects path traversal (.. / \) and non-alphanumeric characters except - and _.
static int is_safe_game_name(const char *name)
{
    if (!name || !name[0] || strlen(name) > 64)
        return 0;
    for (const char *p = name; *p; p++)
    {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static char *compute_save_path(const char *game_name)
{
    if (!is_safe_game_name(game_name))
    {
        rt_trap("SaveData: invalid game name (must be alphanumeric, dash, or underscore, max 64 "
                "chars)");
        return NULL;
    }
    char *home = get_home_dir();
    if (!home)
        return NULL;

    char path[1024];

#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata)
        snprintf(path, sizeof(path), "%s\\Viper\\%s\\save.json", appdata, game_name);
    else
        snprintf(path, sizeof(path), "%s\\AppData\\Roaming\\Viper\\%s\\save.json", home, game_name);
#elif defined(__APPLE__)
    snprintf(
        path, sizeof(path), "%s/Library/Application Support/Viper/%s/save.json", home, game_name);
#else
    snprintf(path, sizeof(path), "%s/.local/share/viper/%s/save.json", home, game_name);
#endif

    free(home);
    return strdup(path);
}

static void ensure_parent_dir(const char *file_path)
{
    /* Extract directory portion */
    char *dir = strdup(file_path);
    if (!dir)
        return;

    /* Find last separator */
    char *last_sep = NULL;
    for (char *p = dir; *p; p++)
    {
        if (*p == '/' || *p == '\\')
            last_sep = p;
    }
    if (last_sep)
    {
        *last_sep = '\0';
        rt_string dir_str = rt_string_from_bytes(dir, strlen(dir));
        rt_dir_make_all(dir_str);
    }
    free(dir);
}

/* Escape a string for JSON output (handles \, ", \n, \t, \r) */
static void json_escape_append(rt_string_builder *sb, const char *str, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        switch (c)
        {
            case '"':
                rt_sb_append_cstr(sb, "\\\"");
                break;
            case '\\':
                rt_sb_append_cstr(sb, "\\\\");
                break;
            case '\n':
                rt_sb_append_cstr(sb, "\\n");
                break;
            case '\r':
                rt_sb_append_cstr(sb, "\\r");
                break;
            case '\t':
                rt_sb_append_cstr(sb, "\\t");
                break;
            default:
                rt_sb_append_bytes(sb, &c, 1);
                break;
        }
    }
}

//=========================================================================
// Finalizer
//=========================================================================

static void savedata_finalizer(void *obj)
{
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    free_all_entries(sd);
    free(sd->game_name);
    free(sd->file_path);
    sd->game_name = NULL;
    sd->file_path = NULL;
}

//=========================================================================
// Public API
//=========================================================================

void *rt_savedata_new(rt_string game_name)
{
    if (!game_name || rt_str_len(game_name) == 0)
    {
        rt_trap("SaveData.New: game name must not be empty");
        return NULL;
    }

    rt_savedata_impl *sd = (rt_savedata_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_savedata_impl));
    if (!sd)
        return NULL;

    const char *name_cstr = rt_string_cstr(game_name);
    if (!name_cstr)
    {
        rt_obj_free(sd);
        return NULL;
    }

    sd->game_name = strdup(name_cstr);

    sd->file_path = compute_save_path(sd->game_name);
    sd->entries = NULL;

    rt_obj_set_finalizer(sd, savedata_finalizer);
    return sd;
}

void rt_savedata_set_int(void *obj, rt_string key, int64_t value)
{
    if (!obj || !key)
        return;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr || kcstr[0] == '\0')
        return;
    size_t klen = strlen(kcstr);

    SaveEntry *e = find_entry(sd, kcstr, klen);
    if (e)
    {
        free(e->str_val);
        e->str_val = NULL;
        e->type = SAVE_INT;
        e->int_val = value;
        return;
    }

    e = (SaveEntry *)malloc(sizeof(SaveEntry));
    if (!e)
        return;
    e->key = strdup(kcstr);
    if (!e->key)
    {
        free(e);
        return;
    }
    e->type = SAVE_INT;
    e->int_val = value;
    e->str_val = NULL;
    e->next = sd->entries;
    sd->entries = e;
}

void rt_savedata_set_string(void *obj, rt_string key, rt_string value)
{
    if (!obj || !key)
        return;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr || kcstr[0] == '\0')
        return;
    size_t klen = strlen(kcstr);

    const char *vcstr = value ? rt_string_cstr(value) : "";
    char *val_copy = strdup(vcstr ? vcstr : "");
    if (!val_copy)
        return;

    SaveEntry *e = find_entry(sd, kcstr, klen);
    if (e)
    {
        free(e->str_val);
        e->type = SAVE_STR;
        e->str_val = val_copy;
        e->int_val = 0;
        return;
    }

    e = (SaveEntry *)malloc(sizeof(SaveEntry));
    if (!e)
    {
        free(val_copy);
        return;
    }
    e->key = strdup(kcstr);
    if (!e->key)
    {
        free(val_copy);
        free(e);
        return;
    }
    e->type = SAVE_STR;
    e->str_val = val_copy;
    e->int_val = 0;
    e->next = sd->entries;
    sd->entries = e;
}

int64_t rt_savedata_get_int(void *obj, rt_string key, int64_t default_val)
{
    if (!obj || !key)
        return default_val;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return default_val;
    SaveEntry *e = find_entry(sd, kcstr, strlen(kcstr));
    if (!e || e->type != SAVE_INT)
        return default_val;
    return e->int_val;
}

rt_string rt_savedata_get_string(void *obj, rt_string key, rt_string default_val)
{
    if (!obj || !key)
        return default_val ? default_val : rt_str_empty();
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return default_val ? default_val : rt_str_empty();
    SaveEntry *e = find_entry(sd, kcstr, strlen(kcstr));
    if (!e || e->type != SAVE_STR)
        return default_val ? default_val : rt_str_empty();
    return rt_string_from_bytes(e->str_val, strlen(e->str_val));
}

int8_t rt_savedata_save(void *obj)
{
    if (!obj)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    if (!sd->file_path)
        return 0;

    /* Ensure parent directory exists */
    ensure_parent_dir(sd->file_path);

    FILE *fp = fopen(sd->file_path, "w");
    if (!fp)
        return 0;

    /* Build JSON using string builder */
    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_append_cstr(&sb, "{\n");

    int first = 1;
    SaveEntry *e = sd->entries;
    while (e)
    {
        if (!first)
            rt_sb_append_cstr(&sb, ",\n");
        first = 0;

        rt_sb_append_cstr(&sb, "  \"");
        json_escape_append(&sb, e->key, strlen(e->key));
        rt_sb_append_cstr(&sb, "\": ");

        if (e->type == SAVE_INT)
        {
            rt_sb_append_int(&sb, e->int_val);
        }
        else
        {
            rt_sb_append_cstr(&sb, "\"");
            if (e->str_val)
                json_escape_append(&sb, e->str_val, strlen(e->str_val));
            rt_sb_append_cstr(&sb, "\"");
        }

        e = e->next;
    }

    rt_sb_append_cstr(&sb, "\n}\n");

    size_t total = sb.len;
    size_t written = fwrite(sb.data, 1, total, fp);
    fclose(fp);
    rt_sb_free(&sb);

    return (written == total) ? 1 : 0;
}

int8_t rt_savedata_load(void *obj)
{
    if (!obj)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    if (!sd->file_path)
        return 0;

    FILE *fp = fopen(sd->file_path, "r");
    if (!fp)
        return 0;

    /* Read entire file */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0)
    {
        fclose(fp);
        return 0;
    }

    char *buf = (char *)malloc((size_t)file_size + 1);
    if (!buf)
    {
        fclose(fp);
        return 0;
    }
    size_t read_count = fread(buf, 1, (size_t)file_size, fp);
    fclose(fp);
    buf[read_count] = '\0';

    /* Parse JSON */
    rt_string json = rt_string_from_bytes(buf, read_count);
    free(buf);

    void *parser = rt_json_stream_new(json);
    if (!parser)
        return 0;

    /* Clear existing entries */
    free_all_entries(sd);

    int64_t tok = rt_json_stream_next(parser);
    if (tok != TOK_OBJECT_START)
        return 0;

    tok = rt_json_stream_next(parser);
    while (tok == TOK_KEY)
    {
        rt_string key_str = rt_json_stream_string_value(parser);

        tok = rt_json_stream_next(parser);
        if (tok == TOK_NUMBER)
        {
            double val = rt_json_stream_number_value(parser);
            rt_savedata_set_int(obj, key_str, (int64_t)val);
        }
        else if (tok == TOK_STRING)
        {
            rt_string val_str = rt_json_stream_string_value(parser);
            rt_savedata_set_string(obj, key_str, val_str);
        }

        tok = rt_json_stream_next(parser);
    }

    return 1;
}

int8_t rt_savedata_has_key(void *obj, rt_string key)
{
    if (!obj || !key)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return 0;
    return find_entry(sd, kcstr, strlen(kcstr)) != NULL;
}

int8_t rt_savedata_remove(void *obj, rt_string key)
{
    if (!obj || !key)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return 0;
    size_t klen = strlen(kcstr);

    SaveEntry **pp = &sd->entries;
    while (*pp)
    {
        SaveEntry *e = *pp;
        if (strlen(e->key) == klen && memcmp(e->key, kcstr, klen) == 0)
        {
            *pp = e->next;
            free_entry(e);
            return 1;
        }
        pp = &e->next;
    }
    return 0;
}

void rt_savedata_clear(void *obj)
{
    if (!obj)
        return;
    free_all_entries((rt_savedata_impl *)obj);
}

int64_t rt_savedata_count(void *obj)
{
    if (!obj)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    int64_t count = 0;
    SaveEntry *e = sd->entries;
    while (e)
    {
        count++;
        e = e->next;
    }
    return count;
}

rt_string rt_savedata_get_path(void *obj)
{
    if (!obj)
        return rt_str_empty();
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    if (!sd->file_path)
        return rt_str_empty();
    return rt_string_from_bytes(sd->file_path, strlen(sd->file_path));
}
