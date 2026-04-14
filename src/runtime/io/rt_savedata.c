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
#include "rt_file_path.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "rt_trap.h"

/* JSON stream parser (from rt_json_stream.h / text module) */
extern void *rt_json_stream_new(rt_string json);
extern int64_t rt_json_stream_next(void *parser);
extern rt_string rt_json_stream_string_value(void *parser);
extern double rt_json_stream_number_value(void *parser);
extern rt_string rt_json_stream_error(void *parser);

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

typedef enum { SAVE_INT = 0, SAVE_STR = 1 } SaveEntryType;

typedef struct SaveEntry {
    rt_string key;
    int64_t key_len;
    SaveEntryType type;
    int64_t int_val;
    rt_string str_val;
    struct SaveEntry *next;
} SaveEntry;

typedef struct {
    char *game_name;
    char *file_path;
    SaveEntry *entries;
} rt_savedata_impl;

//=========================================================================
// Internal Helpers
//=========================================================================

static SaveEntry *find_entry(rt_savedata_impl *sd, const char *key, size_t key_len) {
    if (!sd || !key)
        return NULL;

    SaveEntry *e = sd->entries;
    while (e) {
        if (e->key && e->key_len == (int64_t)key_len &&
            memcmp(rt_string_cstr(e->key), key, key_len) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

static void free_entry(SaveEntry *e) {
    if (e) {
        if (e->key)
            rt_string_unref(e->key);
        if (e->str_val)
            rt_string_unref(e->str_val);
        free(e);
    }
}

static void free_all_entries(rt_savedata_impl *sd) {
    SaveEntry *e = sd->entries;
    while (e) {
        SaveEntry *next = e->next;
        free_entry(e);
        e = next;
    }
    sd->entries = NULL;
}

static char *get_home_dir(void) {
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (home)
        return strdup(home);
    const char *drive = getenv("HOMEDRIVE");
    const char *path = getenv("HOMEPATH");
    if (drive && path) {
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

static char *savedata_parent_dir_dup(const char *file_path) {
    if (!file_path)
        return NULL;
    const char *last_sep = NULL;
    for (const char *p = file_path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            last_sep = p;
    }
    if (!last_sep)
        return NULL;
    size_t len = (size_t)(last_sep - file_path);
    char *dir = (char *)malloc(len + 1);
    if (!dir)
        return NULL;
    memcpy(dir, file_path, len);
    dir[len] = '\0';
    return dir;
}

#ifdef _WIN32
static FILE *savedata_fopen_utf8(const char *path, const wchar_t *mode) {
    wchar_t *wide_path = rt_file_path_utf8_to_wide(path);
    if (!wide_path)
        return NULL;
    FILE *fp = _wfopen(wide_path, mode);
    free(wide_path);
    return fp;
}

static int savedata_remove_utf8(const char *path) {
    wchar_t *wide_path = rt_file_path_utf8_to_wide(path);
    if (!wide_path)
        return -1;
    int rc = _wremove(wide_path);
    free(wide_path);
    return rc;
}

static int savedata_replace_utf8(const char *src, const char *dst) {
    wchar_t *wsrc = rt_file_path_utf8_to_wide(src);
    wchar_t *wdst = rt_file_path_utf8_to_wide(dst);
    if (!wsrc || !wdst) {
        free(wsrc);
        free(wdst);
        return 0;
    }
    BOOL ok = MoveFileExW(wsrc, wdst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(wsrc);
    free(wdst);
    return ok ? 1 : 0;
}

static int savedata_sync_file(FILE *fp) {
    return _commit(_fileno(fp)) == 0 ? 1 : 0;
}

static int savedata_sync_parent_dir(const char *path) {
    char *parent = savedata_parent_dir_dup(path);
    if (!parent)
        return 1;

    wchar_t *wide_parent = rt_file_path_utf8_to_wide(parent);
    free(parent);
    if (!wide_parent)
        return 0;

    HANDLE h = CreateFileW(wide_parent,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    free(wide_parent);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    if (FlushFileBuffers(h)) {
        CloseHandle(h);
        return 1;
    }

    DWORD err = GetLastError();
    CloseHandle(h);
    return (err == ERROR_INVALID_HANDLE || err == ERROR_ACCESS_DENIED) ? 1 : 0;
}

static int savedata_file_size(FILE *fp, uint64_t *out_size) {
    if (_fseeki64(fp, 0, SEEK_END) != 0)
        return 0;
    __int64 size = _ftelli64(fp);
    if (size < 0)
        return 0;
    if (_fseeki64(fp, 0, SEEK_SET) != 0)
        return 0;
    *out_size = (uint64_t)size;
    return 1;
}
#else
static FILE *savedata_fopen_utf8(const char *path, const char *mode) {
    return fopen(path, mode);
}

static int savedata_remove_utf8(const char *path) {
    return remove(path);
}

static int savedata_replace_utf8(const char *src, const char *dst) {
    return rename(src, dst) == 0 ? 1 : 0;
}

static int savedata_sync_file(FILE *fp) {
    return fsync(fileno(fp)) == 0 ? 1 : 0;
}

static int savedata_sync_parent_dir(const char *path) {
    char *parent = savedata_parent_dir_dup(path);
    if (!parent)
        return 0;
    int fd = open(parent, O_RDONLY);
    free(parent);
    if (fd < 0)
        return 0;
    int ok = fsync(fd) == 0 ? 1 : 0;
    close(fd);
    return ok;
}

static int savedata_file_size(FILE *fp, uint64_t *out_size) {
    if (fseeko(fp, 0, SEEK_END) != 0)
        return 0;
    off_t size = ftello(fp);
    if (size < 0)
        return 0;
    if (fseeko(fp, 0, SEEK_SET) != 0)
        return 0;
    *out_size = (uint64_t)size;
    return 1;
}
#endif

/// @brief Validate game_name contains only safe characters for use in file paths.
/// Rejects path traversal (.. / \) and non-alphanumeric characters except - and _.
static int is_safe_game_name(const char *name) {
    if (!name || !name[0] || strlen(name) > 64)
        return 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static char *compute_save_path(const char *game_name) {
    if (!is_safe_game_name(game_name)) {
        rt_trap("SaveData: invalid game name (must be alphanumeric, dash, or underscore, max 64 "
                "chars)");
        return NULL;
    }
    char *home = get_home_dir();
    if (!home)
        return NULL;
    char *path = NULL;

#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    const char *base = appdata ? appdata : home;
    const char *middle = appdata ? "\\Viper\\" : "\\AppData\\Roaming\\Viper\\";
    size_t needed = strlen(base) + strlen(middle) + strlen(game_name) + strlen("\\save.json") + 1;
    path = (char *)malloc(needed);
    if (path)
        snprintf(path, needed, "%s%s%s\\save.json", base, middle, game_name);
#elif defined(__APPLE__)
    size_t needed = strlen(home) + strlen("/Library/Application Support/Viper/") + strlen(game_name) +
                    strlen("/save.json") + 1;
    path = (char *)malloc(needed);
    if (path)
        snprintf(path, needed, "%s/Library/Application Support/Viper/%s/save.json", home, game_name);
#else
    size_t needed =
        strlen(home) + strlen("/.local/share/viper/") + strlen(game_name) + strlen("/save.json") + 1;
    path = (char *)malloc(needed);
    if (path)
        snprintf(path, needed, "%s/.local/share/viper/%s/save.json", home, game_name);
#endif

    free(home);
    return path;
}

static void ensure_parent_dir(const char *file_path) {
    char *dir = savedata_parent_dir_dup(file_path);
    if (!dir)
        return;
    rt_string dir_str = rt_string_from_bytes(dir, strlen(dir));
    rt_dir_make_all(dir_str);
    rt_string_unref(dir_str);
    free(dir);
}

static int savedata_set_int_entry(SaveEntry **head, rt_string key, int64_t value) {
    if (!head || !key)
        return 0;

    const char *key_data = rt_string_cstr(key);
    size_t key_len = (size_t)rt_str_len(key);
    SaveEntry *e = NULL;
    for (SaveEntry *it = *head; it; it = it->next) {
        if (it->key && it->key_len == (int64_t)key_len &&
            memcmp(rt_string_cstr(it->key), key_data, key_len) == 0) {
            e = it;
            break;
        }
    }

    if (e) {
        if (e->str_val) {
            rt_string_unref(e->str_val);
            e->str_val = NULL;
        }
        e->type = SAVE_INT;
        e->int_val = value;
        return 1;
    }

    e = (SaveEntry *)malloc(sizeof(SaveEntry));
    if (!e)
        return 0;

    e->key = rt_string_ref(key);
    e->key_len = (int64_t)key_len;
    e->type = SAVE_INT;
    e->int_val = value;
    e->str_val = NULL;
    e->next = *head;
    *head = e;
    return 1;
}

static int savedata_set_string_entry(SaveEntry **head, rt_string key, rt_string value) {
    if (!head || !key)
        return 0;

    const char *key_data = rt_string_cstr(key);
    size_t key_len = (size_t)rt_str_len(key);
    SaveEntry *e = NULL;
    for (SaveEntry *it = *head; it; it = it->next) {
        if (it->key && it->key_len == (int64_t)key_len &&
            memcmp(rt_string_cstr(it->key), key_data, key_len) == 0) {
            e = it;
            break;
        }
    }

    rt_string stored_value = value ? value : rt_str_empty();
    if (e) {
        if (e->str_val)
            rt_string_unref(e->str_val);
        e->type = SAVE_STR;
        e->str_val = rt_string_ref(stored_value);
        e->int_val = 0;
        return 1;
    }

    e = (SaveEntry *)malloc(sizeof(SaveEntry));
    if (!e)
        return 0;

    e->key = rt_string_ref(key);
    e->key_len = (int64_t)key_len;
    e->type = SAVE_STR;
    e->int_val = 0;
    e->str_val = rt_string_ref(stored_value);
    e->next = *head;
    *head = e;
    return 1;
}

static void savedata_free_parser(void *parser) {
    if (parser && rt_obj_release_check0(parser))
        rt_obj_free(parser);
}

static void json_escape_append(rt_string_builder *sb, const char *str, size_t len) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"':
                rt_sb_append_cstr(sb, "\\\"");
                break;
            case '\\':
                rt_sb_append_cstr(sb, "\\\\");
                break;
            case '\b':
                rt_sb_append_cstr(sb, "\\b");
                break;
            case '\f':
                rt_sb_append_cstr(sb, "\\f");
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
                if (c < 0x20) {
                    char escaped[6] = {'\\', 'u', '0', '0', hex[(c >> 4) & 0x0F], hex[c & 0x0F]};
                    rt_sb_append_bytes(sb, escaped, sizeof(escaped));
                } else {
                    char ch = (char)c;
                    rt_sb_append_bytes(sb, &ch, 1);
                }
                break;
        }
    }
}

static int savedata_write_atomic(const char *path, const char *data, size_t len) {
    size_t path_len = strlen(path);
    char *tmp_path = (char *)malloc(path_len + 48);
    if (!tmp_path)
        return 0;

#ifdef _WIN32
    unsigned long pid = (unsigned long)_getpid();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(tmp_path, path_len + 48, "%s.tmp.%lu.%p", path, pid, (const void *)data);

    FILE *fp =
#ifdef _WIN32
        savedata_fopen_utf8(tmp_path, L"wb");
#else
        savedata_fopen_utf8(tmp_path, "wb");
#endif
    if (!fp) {
        free(tmp_path);
        return 0;
    }

    size_t written = 0;
    while (written < len) {
        size_t n = fwrite(data + written, 1, len - written, fp);
        if (n == 0) {
            if (ferror(fp))
                break;
        }
        written += n;
    }

    int ok = (written == len) ? 1 : 0;
    if (ok && fflush(fp) != 0)
        ok = 0;
    if (ok && !savedata_sync_file(fp))
        ok = 0;
    if (fclose(fp) != 0)
        ok = 0;

    if (ok)
        ok = savedata_replace_utf8(tmp_path, path);
    if (ok)
        ok = savedata_sync_parent_dir(path);

    if (!ok)
        savedata_remove_utf8(tmp_path);
    free(tmp_path);
    return ok;
}

//=========================================================================
// Finalizer
//=========================================================================

static void savedata_finalizer(void *obj) {
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

/// @brief Construct a SaveData store keyed by `game_name`. The save file path is computed from
/// the platform's per-user data directory (`%APPDATA%/<game>` on Win32, `~/.local/share/<game>`
/// on Linux, `~/Library/Application Support/<game>` on macOS). Empty game-name traps. Returns a
/// GC-managed handle; load existing data via `_load`.
void *rt_savedata_new(rt_string game_name) {
    if (!game_name || rt_str_len(game_name) == 0) {
        rt_trap("SaveData.New: game name must not be empty");
        return NULL;
    }

    rt_savedata_impl *sd = (rt_savedata_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_savedata_impl));
    if (!sd)
        return NULL;

    const char *name_cstr = rt_string_cstr(game_name);
    if (!name_cstr) {
        rt_obj_free(sd);
        return NULL;
    }

    sd->game_name = strdup(name_cstr);

    sd->file_path = compute_save_path(sd->game_name);
    sd->entries = NULL;

    rt_obj_set_finalizer(sd, savedata_finalizer);
    return sd;
}

/// @brief Store an int64 under `key`. In-memory only — call `_save` to flush to disk.
/// Re-setting an existing key overwrites; type-changing (string→int) is allowed.
void rt_savedata_set_int(void *obj, rt_string key, int64_t value) {
    if (!obj || !key)
        return;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    size_t klen = (size_t)rt_str_len(key);
    if (!kcstr || klen == 0)
        return;
    (void)klen;

    savedata_set_int_entry(&sd->entries, key, value);
}

/// @brief Store a string under `key`. In-memory only — call `_save` to persist.
void rt_savedata_set_string(void *obj, rt_string key, rt_string value) {
    if (!obj || !key)
        return;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    size_t klen = (size_t)rt_str_len(key);
    if (!kcstr || klen == 0)
        return;
    (void)klen;

    savedata_set_string_entry(&sd->entries, key, value);
}

/// @brief Read an int64 by `key`, returning `default_val` if missing or stored as a different type.
int64_t rt_savedata_get_int(void *obj, rt_string key, int64_t default_val) {
    if (!obj || !key)
        return default_val;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return default_val;
    SaveEntry *e = find_entry(sd, kcstr, (size_t)rt_str_len(key));
    if (!e || e->type != SAVE_INT)
        return default_val;
    return e->int_val;
}

/// @brief Read a string by `key`, returning `default_val` (or empty) if missing/wrong type.
/// The returned string is freshly retained.
rt_string rt_savedata_get_string(void *obj, rt_string key, rt_string default_val) {
    if (!obj || !key)
        return default_val ? default_val : rt_str_empty();
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return default_val ? default_val : rt_str_empty();
    SaveEntry *e = find_entry(sd, kcstr, (size_t)rt_str_len(key));
    if (!e || e->type != SAVE_STR)
        return default_val ? default_val : rt_str_empty();
    return e->str_val ? rt_string_ref(e->str_val) : rt_str_empty();
}

/// @brief Persist all in-memory entries to disk as JSON. Ensures the parent directory exists,
/// builds the JSON object via string-builder, then atomically writes via the file-IO layer.
/// Returns 1 on success, 0 on any failure.
int8_t rt_savedata_save(void *obj) {
    if (!obj)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    if (!sd->file_path)
        return 0;

    /* Ensure parent directory exists */
    ensure_parent_dir(sd->file_path);

    /* Build JSON using string builder */
    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_append_cstr(&sb, "{\n");

    int first = 1;
    SaveEntry *e = sd->entries;
    while (e) {
        if (!first)
            rt_sb_append_cstr(&sb, ",\n");
        first = 0;

        rt_sb_append_cstr(&sb, "  \"");
        json_escape_append(&sb, rt_string_cstr(e->key), (size_t)e->key_len);
        rt_sb_append_cstr(&sb, "\": ");

        if (e->type == SAVE_INT) {
            rt_sb_append_int(&sb, e->int_val);
        } else {
            rt_sb_append_cstr(&sb, "\"");
            if (e->str_val)
                json_escape_append(&sb, rt_string_cstr(e->str_val), (size_t)rt_str_len(e->str_val));
            rt_sb_append_cstr(&sb, "\"");
        }

        e = e->next;
    }

    rt_sb_append_cstr(&sb, "\n}\n");

    int ok = savedata_write_atomic(sd->file_path, sb.data, sb.len);
    rt_sb_free(&sb);

    return ok ? 1 : 0;
}

/// @brief Load existing entries from disk into memory, replacing the current state. Uses the
/// streaming JSON parser (`rt_json_stream_*`) so large save files don't allocate everything at
/// once. Returns 1 on success or "no file yet" (treats missing file as empty); 0 on parse error.
int8_t rt_savedata_load(void *obj) {
    if (!obj)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    if (!sd->file_path)
        return 0;

    FILE *fp =
#ifdef _WIN32
        savedata_fopen_utf8(sd->file_path, L"rb");
#else
        savedata_fopen_utf8(sd->file_path, "rb");
#endif
    if (!fp)
        return 0;

    /* Read entire file */
    uint64_t file_size = 0;
    if (!savedata_file_size(fp, &file_size) || file_size == 0 || file_size > SIZE_MAX) {
        fclose(fp);
        return 0;
    }

    char *buf = (char *)malloc((size_t)file_size + 1);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    size_t read_count = fread(buf, 1, (size_t)file_size, fp);
    if (read_count != (size_t)file_size && ferror(fp)) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    buf[read_count] = '\0';

    /* Parse JSON */
    rt_string json = rt_string_from_bytes(buf, read_count);
    free(buf);

    void *parser = rt_json_stream_new(json);
    if (!parser) {
        rt_string_unref(json);
        return 0;
    }

    SaveEntry *loaded_entries = NULL;
    int success = 0;

    int64_t tok = rt_json_stream_next(parser);
    if (tok != TOK_OBJECT_START)
        goto done;

    tok = rt_json_stream_next(parser);
    if (tok == TOK_OBJECT_END) {
        success = (rt_json_stream_next(parser) == TOK_END) ? 1 : 0;
        goto done;
    }

    while (tok == TOK_KEY) {
        rt_string key_str = rt_json_stream_string_value(parser);
        tok = rt_json_stream_next(parser);
        if (tok == TOK_NUMBER) {
            double val = rt_json_stream_number_value(parser);
            if (!savedata_set_int_entry(&loaded_entries, key_str, (int64_t)val)) {
                rt_string_unref(key_str);
                goto done;
            }
        } else if (tok == TOK_STRING) {
            rt_string val_str = rt_json_stream_string_value(parser);
            if (!savedata_set_string_entry(&loaded_entries, key_str, val_str)) {
                rt_string_unref(val_str);
                rt_string_unref(key_str);
                goto done;
            }
            rt_string_unref(val_str);
        } else {
            rt_string_unref(key_str);
            goto done;
        }
        rt_string_unref(key_str);

        tok = rt_json_stream_next(parser);
        if (tok == TOK_OBJECT_END) {
            success = (rt_json_stream_next(parser) == TOK_END) ? 1 : 0;
            goto done;
        }
        if (tok != TOK_KEY)
            goto done;
    }

done:
    savedata_free_parser(parser);
    rt_string_unref(json);
    if (!success) {
        while (loaded_entries) {
            SaveEntry *next = loaded_entries->next;
            free_entry(loaded_entries);
            loaded_entries = next;
        }
        return 0;
    }

    free_all_entries(sd);
    sd->entries = loaded_entries;
    return 1;
}

/// @brief Returns 1 if `key` exists in the current entries, regardless of stored type.
int8_t rt_savedata_has_key(void *obj, rt_string key) {
    if (!obj || !key)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return 0;
    return find_entry(sd, kcstr, (size_t)rt_str_len(key)) != NULL;
}

/// @brief Remove an entry by key. Returns 1 if removed, 0 if absent. In-memory only.
int8_t rt_savedata_remove(void *obj, rt_string key) {
    if (!obj || !key)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    const char *kcstr = rt_string_cstr(key);
    if (!kcstr)
        return 0;
    size_t klen = (size_t)rt_str_len(key);

    SaveEntry **pp = &sd->entries;
    while (*pp) {
        SaveEntry *e = *pp;
        if (e->key && e->key_len == (int64_t)klen &&
            memcmp(rt_string_cstr(e->key), kcstr, klen) == 0) {
            *pp = e->next;
            free_entry(e);
            return 1;
        }
        pp = &e->next;
    }
    return 0;
}

/// @brief Drop every in-memory entry (call `_save` afterwards to clear the on-disk file too).
void rt_savedata_clear(void *obj) {
    if (!obj)
        return;
    free_all_entries((rt_savedata_impl *)obj);
}

/// @brief Number of entries currently in the store.
int64_t rt_savedata_count(void *obj) {
    if (!obj)
        return 0;
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    int64_t count = 0;
    SaveEntry *e = sd->entries;
    while (e) {
        count++;
        e = e->next;
    }
    return count;
}

/// @brief Read the absolute path where this SaveData persists. Useful for showing the user
/// where their save lives or for backup/restore tooling.
rt_string rt_savedata_get_path(void *obj) {
    if (!obj)
        return rt_str_empty();
    rt_savedata_impl *sd = (rt_savedata_impl *)obj;
    if (!sd->file_path)
        return rt_str_empty();
    return rt_string_from_bytes(sd->file_path, strlen(sd->file_path));
}
