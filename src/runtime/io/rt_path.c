//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_path.c
// Purpose: Cross-platform path manipulation utilities backing the Viper.IO.Path
//          class. Provides Join, Dir, Name, Stem, Ext, Norm, IsAbs, WithExt,
//          and related operations that work correctly on Unix and Windows,
//          including drive-letter paths and UNC paths.
//
// Key invariants:
//   - Both '/' and '\' are accepted as separators on all platforms.
//   - Norm removes redundant '.' and '..' components without filesystem access.
//   - Join always produces a path using the native platform separator.
//   - Ext returns the final '.' suffix including the dot, or "" if absent.
//   - All returned strings are newly allocated runtime strings; none borrow.
//   - All functions are thread-safe and reentrant (no global mutable state).
//
// Ownership/Lifetime:
//   - Every returned rt_string is a fresh allocation owned by the caller.
//   - Viper's garbage collector manages returned string lifetimes.
//
// Links: src/runtime/io/rt_path.h (public API),
//        src/runtime/io/rt_dir.c (directory create/list/remove operations),
//        src/runtime/io/rt_file_ext.c (file-level read/write/copy helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_path.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#elif defined(__viperdos__)
// ViperDOS: POSIX path utilities available via libc.
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#else
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

/// @brief Check if a character is a path separator (/ or \).
///
/// Both forward slash and backslash are considered separators on all platforms
/// for maximum compatibility when handling paths from different sources.
///
/// @param c Character to check.
///
/// @return Non-zero if c is '/' or '\', zero otherwise.
static inline int is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

/// @brief Get the length of a runtime string safely (null-safe).
///
/// Returns the byte length of a runtime string, handling NULL pointers
/// gracefully by returning 0.
///
/// @param s Runtime string handle (may be NULL).
///
/// @return Length in bytes, or 0 if s is NULL or contains NULL data.
static inline size_t rt_string_safe_len(rt_string s)
{
    if (!s || !s->data)
        return 0;
    return (s->heap && s->heap != RT_SSO_SENTINEL) ? rt_heap_len(s->data) : s->literal_len;
}

/// @brief Get the data pointer of a runtime string safely (null-safe).
///
/// Returns a pointer to the string's character data, handling NULL pointers
/// gracefully by returning an empty string literal.
///
/// @param s Runtime string handle (may be NULL).
///
/// @return Pointer to string data, or "" if s is NULL or contains NULL data.
static inline const char *rt_string_safe_data(rt_string s)
{
    if (!s || !s->data)
        return "";
    return s->data;
}

/// @brief Join two path components with the platform separator.
///
/// Combines two path components into a single path, inserting the appropriate
/// platform separator between them. Handles edge cases like empty paths,
/// trailing separators, and absolute second paths.
///
/// **Usage example:**
/// ```
/// ' Basic joining
/// Dim path = Path.Join("src", "main.bas")
/// ' Unix:    "src/main.bas"
/// ' Windows: "src\main.bas"
///
/// ' Multiple levels
/// Dim deep = Path.Join(Path.Join("a", "b"), "c")  ' "a/b/c"
///
/// ' Handles trailing separators
/// Path.Join("dir/", "file")   ' "dir/file" (not "dir//file")
/// Path.Join("dir", "/file")   ' "dir/file" (not "dir//file")
///
/// ' Absolute second path replaces first
/// Path.Join("prefix", "/absolute")  ' "/absolute"
/// Path.Join("prefix", "C:\abs")     ' "C:\abs" (Windows)
/// ```
///
/// **Special cases:**
/// | First    | Second    | Result    |
/// |----------|-----------|-----------|
/// | empty    | "b"       | "b"       |
/// | "a"      | empty     | "a"       |
/// | "a"      | "/b"      | "/b"      |
/// | "a/"     | "b"       | "a/b"     |
/// | "a"      | "b"       | "a/b"     |
///
/// @param a First path component (may be empty).
/// @param b Second path component (may be empty or absolute).
///
/// @return Newly allocated joined path string.
///
/// @note O(n) time complexity where n is total length of both paths.
/// @note If b is absolute, returns b (ignores a).
/// @note Uses platform-native separator (/ on Unix, \ on Windows).
///
/// @see rt_path_dir For extracting the directory portion
/// @see rt_path_name For extracting the filename portion
rt_string rt_path_join(rt_string a, rt_string b)
{
    const char *a_data = rt_string_safe_data(a);
    size_t a_len = rt_string_safe_len(a);
    const char *b_data = rt_string_safe_data(b);
    size_t b_len = rt_string_safe_len(b);

    // If a is empty, return b
    if (a_len == 0)
        return rt_string_from_bytes(b_data, b_len);

    // If b is empty, return a
    if (b_len == 0)
        return rt_string_from_bytes(a_data, a_len);

    // If b is absolute, return b
    // Check for Unix absolute path
    if (is_path_sep(b_data[0]))
        return rt_string_from_bytes(b_data, b_len);

#ifdef _WIN32
    // Check for Windows drive letter (C:) or UNC path
    if ((b_len >= 2 && isalpha((unsigned char)b_data[0]) && b_data[1] == ':') ||
        (b_len >= 2 && is_path_sep(b_data[0]) && is_path_sep(b_data[1])))
    {
        return rt_string_from_bytes(b_data, b_len);
    }
#endif

    // Check if a ends with separator
    int a_has_sep = (a_len > 0 && is_path_sep(a_data[a_len - 1]));
    // Check if b starts with separator
    int b_has_sep = (b_len > 0 && is_path_sep(b_data[0]));

    rt_string_builder sb;
    rt_sb_init(&sb);

    rt_sb_append_bytes(&sb, a_data, a_len);

    if (!a_has_sep && !b_has_sep)
        rt_sb_append_bytes(&sb, PATH_SEP_STR, 1);
    else if (a_has_sep && b_has_sep)
        b_data++, b_len--; // Skip redundant separator in b

    rt_sb_append_bytes(&sb, b_data, b_len);

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

/// @brief Get the directory portion of a path (parent directory).
///
/// Returns the directory containing the file or directory referenced by the
/// path. This is equivalent to removing the last component of the path.
///
/// **Usage example:**
/// ```
/// ' Extract parent directory
/// Dim dir = Path.Dir("/home/user/file.txt")  ' "/home/user"
/// Dim dir2 = Path.Dir("src/main.bas")        ' "src"
///
/// ' Navigate up the tree
/// Dim path = "/a/b/c/d"
/// Print Path.Dir(path)                        ' "/a/b/c"
/// Print Path.Dir(Path.Dir(path))              ' "/a/b"
///
/// ' Root handling
/// Path.Dir("/file.txt")  ' "/"
/// Path.Dir("file.txt")   ' "."
/// ```
///
/// **Examples by path type:**
/// | Input                    | Output              |
/// |--------------------------|---------------------|
/// | `/home/user/file.txt`    | `/home/user`        |
/// | `src/main.bas`           | `src`               |
/// | `file.txt`               | `.`                 |
/// | `/file.txt`              | `/`                 |
/// | `C:\Users\file.txt`      | `C:\Users`          |
/// | `C:\file.txt`            | `C:\`               |
/// | `\\server\share\file`    | `\\server\share`    |
///
/// @param path Path to extract directory from.
///
/// @return Newly allocated directory path string.
///
/// @note O(n) time complexity where n is path length.
/// @note Returns "." for simple filenames without directory.
/// @note Preserves drive letters and UNC prefixes on Windows.
///
/// @see rt_path_name For extracting the filename portion
/// @see rt_path_join For combining directory and filename
rt_string rt_path_dir(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return rt_str_empty();

    // Find the last path separator
    size_t i = len;
    while (i > 0 && !is_path_sep(data[i - 1]))
        i--;

    if (i == 0)
    {
        // No separator found - return "." for relative path
        return rt_string_from_bytes(".", 1);
    }

#ifdef _WIN32
    // Handle Windows drive root (e.g., "C:\")
    if (i == 3 && isalpha((unsigned char)data[0]) && data[1] == ':' && is_path_sep(data[2]))
        return rt_string_from_bytes(data, 3);

    // Handle UNC paths (e.g., "\\server\share\")
    if (i >= 2 && is_path_sep(data[0]) && is_path_sep(data[1]))
    {
        // Find the end of server\share part
        size_t slashes = 0;
        size_t j = 2;
        while (j < len && slashes < 2)
        {
            if (is_path_sep(data[j]))
                slashes++;
            j++;
        }
        if (i <= j)
            return rt_string_from_bytes(data, j);
    }
#endif

    // Unix root
    if (i == 1 && is_path_sep(data[0]))
        return rt_string_from_bytes("/", 1);

    // Return directory without trailing separator
    return rt_string_from_bytes(data, i - 1);
}

/// @brief Get the filename portion of a path (last component).
///
/// Returns the final component of the path - the filename including its
/// extension. This is the inverse of rt_path_dir.
///
/// **Usage example:**
/// ```
/// ' Extract filename
/// Dim name = Path.Name("/home/user/file.txt")  ' "file.txt"
/// Dim name2 = Path.Name("src/main.bas")        ' "main.bas"
///
/// ' Works with directories too
/// Path.Name("/home/user/")     ' "user"
/// Path.Name("project/src")     ' "src"
///
/// ' Get just the filename from user input
/// Dim fullPath = SelectFile()
/// Print "You selected: " & Path.Name(fullPath)
/// ```
///
/// **Examples:**
/// | Input                    | Output        |
/// |--------------------------|---------------|
/// | `/home/user/file.txt`    | `file.txt`    |
/// | `src/main.bas`           | `main.bas`    |
/// | `file.txt`               | `file.txt`    |
/// | `/home/user/`            | `user`        |
/// | `C:\Users\file.txt`      | `file.txt`    |
/// | `/`                      | (empty)       |
///
/// @param path Path to extract filename from.
///
/// @return Newly allocated filename string (may be empty).
///
/// @note O(n) time complexity where n is path length.
/// @note Strips trailing separators before extracting name.
/// @note Returns empty string for root paths.
///
/// @see rt_path_dir For extracting the directory portion
/// @see rt_path_stem For filename without extension
/// @see rt_path_ext For just the extension
rt_string rt_path_name(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return rt_str_empty();

    // Strip trailing separators
    while (len > 0 && is_path_sep(data[len - 1]))
        len--;

    if (len == 0)
        return rt_str_empty();

    // Find the last path separator
    size_t i = len;
    while (i > 0 && !is_path_sep(data[i - 1]))
        i--;

    return rt_string_from_bytes(data + i, len - i);
}

/// @brief Get the filename without its extension (stem).
///
/// Returns the filename portion of the path with the extension removed.
/// The stem is the base name used for generating related files or displaying
/// to users without the file type suffix.
///
/// **Usage example:**
/// ```
/// ' Extract stem (base name)
/// Dim stem = Path.Stem("/home/user/report.txt")  ' "report"
/// Dim stem2 = Path.Stem("document.pdf")          ' "document"
///
/// ' Generate related filenames
/// Dim source = "program.bas"
/// Dim output = Path.Stem(source) & ".exe"  ' "program.exe"
/// Dim backup = Path.Stem(source) & ".bak"  ' "program.bak"
///
/// ' Display clean names to users
/// Dim files = Dir.Files("docs")
/// For Each f In files
///     Print Path.Stem(f)  ' No extension clutter
/// Next
/// ```
///
/// **Hidden files (Unix convention):**
/// Files starting with a dot are treated as having no extension:
/// ```
/// Path.Stem(".bashrc")   ' ".bashrc" (entire name is stem)
/// Path.Stem(".gitignore") ' ".gitignore"
/// ```
///
/// **Examples:**
/// | Input                | Output        |
/// |----------------------|---------------|
/// | `report.txt`         | `report`      |
/// | `archive.tar.gz`     | `archive.tar` |
/// | `.bashrc`            | `.bashrc`     |
/// | `file`               | `file`        |
/// | `file.`              | `file`        |
///
/// @param path Path to extract stem from.
///
/// @return Newly allocated stem string.
///
/// @note O(n) time complexity where n is filename length.
/// @note For files with multiple extensions, only the last is removed.
/// @note Hidden files (starting with .) keep their full name.
///
/// @see rt_path_name For filename with extension
/// @see rt_path_ext For just the extension
/// @see rt_path_with_ext For changing the extension
rt_string rt_path_stem(rt_string path)
{
    // First get the filename
    rt_string name = rt_path_name(path);
    const char *data = rt_string_safe_data(name);
    size_t len = rt_string_safe_len(name);

    if (len == 0)
    {
        rt_string_unref(name);
        return rt_str_empty();
    }

    // Find the last dot
    size_t dot_pos = len;
    for (size_t i = len; i > 0; i--)
    {
        if (data[i - 1] == '.')
        {
            dot_pos = i - 1;
            break;
        }
    }

    // Handle special cases: ".file" and "file."
    // ".file" -> stem is ".file" (hidden file)
    // "file." -> stem is "file"
    if (dot_pos == 0)
    {
        // Starts with dot - no extension
        return name;
    }

    rt_string result = rt_string_from_bytes(data, dot_pos);
    rt_string_unref(name);
    return result;
}

/// @brief Get the file extension including the leading dot.
///
/// Returns the file extension (the portion after the last dot in the filename).
/// The returned string includes the dot prefix for consistency with standard
/// extension formats.
///
/// **Usage example:**
/// ```
/// ' Extract extension
/// Dim ext = Path.Ext("report.txt")   ' ".txt"
/// Dim ext2 = Path.Ext("image.PNG")   ' ".PNG"
///
/// ' Check file type
/// Dim file = "document.pdf"
/// Select Case Path.Ext(file).ToLower()
///     Case ".pdf": OpenPDFViewer(file)
///     Case ".txt": OpenTextEditor(file)
///     Case ".bas": OpenCompiler(file)
/// End Select
///
/// ' Filter files by extension
/// For Each f In Dir.Files("docs")
///     If Path.Ext(f) = ".md" Then
///         ProcessMarkdown(f)
///     End If
/// Next
/// ```
///
/// **Special cases:**
/// | Input                | Output    | Notes                    |
/// |----------------------|-----------|--------------------------|
/// | `file.txt`           | `.txt`    | Normal case              |
/// | `archive.tar.gz`     | `.gz`     | Only last extension      |
/// | `.bashrc`            | (empty)   | Hidden file, no ext      |
/// | `README`             | (empty)   | No extension             |
/// | `file.`              | `.`       | Empty extension          |
///
/// @param path Path to extract extension from.
///
/// @return Newly allocated extension string (includes dot) or empty string.
///
/// @note O(n) time complexity where n is filename length.
/// @note Returns empty string if no extension found.
/// @note Hidden files (starting with .) are not considered to have extensions.
/// @note Extension case is preserved (not normalized).
///
/// @see rt_path_stem For filename without extension
/// @see rt_path_with_ext For changing the extension
/// @see rt_path_name For full filename
rt_string rt_path_ext(rt_string path)
{
    // First get the filename
    rt_string name = rt_path_name(path);
    const char *data = rt_string_safe_data(name);
    size_t len = rt_string_safe_len(name);

    if (len == 0)
    {
        rt_string_unref(name);
        return rt_str_empty();
    }

    // Find the last dot
    size_t dot_pos = len;
    for (size_t i = len; i > 0; i--)
    {
        if (data[i - 1] == '.')
        {
            dot_pos = i - 1;
            break;
        }
    }

    // No dot found, or only at start (hidden file)
    if (dot_pos == len || dot_pos == 0)
    {
        rt_string_unref(name);
        return rt_str_empty();
    }

    rt_string result = rt_string_from_bytes(data + dot_pos, len - dot_pos);
    rt_string_unref(name);
    return result;
}

/// @brief Replace or add an extension to a path.
///
/// Returns a new path with the extension replaced (or added if none existed).
/// The new extension can be specified with or without the leading dot.
///
/// **Usage example:**
/// ```
/// ' Change file extension
/// Dim source = "program.bas"
/// Dim compiled = Path.WithExt(source, ".exe")  ' "program.exe"
/// Dim listing = Path.WithExt(source, "lst")    ' "program.lst" (dot added)
///
/// ' Generate output filenames
/// Dim input = "/data/report.csv"
/// Dim output = Path.WithExt(input, ".json")    ' "/data/report.json"
///
/// ' Remove extension (empty string)
/// Dim noExt = Path.WithExt("file.txt", "")     ' "file"
///
/// ' Add extension to files without one
/// Dim readme = Path.WithExt("README", ".md")   ' "README.md"
/// ```
///
/// **Behavior:**
/// | Original         | New Ext    | Result           |
/// |------------------|------------|------------------|
/// | `file.txt`       | `.md`      | `file.md`        |
/// | `file.txt`       | `md`       | `file.md`        |
/// | `file.tar.gz`    | `.zip`     | `file.tar.zip`   |
/// | `file`           | `.txt`     | `file.txt`       |
/// | `.bashrc`        | `.bak`     | `.bashrc.bak`    |
/// | `file.txt`       | (empty)    | `file`           |
///
/// @param path Original path.
/// @param new_ext New extension (with or without leading dot).
///
/// @return Newly allocated path with new extension.
///
/// @note O(n) time complexity where n is path length.
/// @note Automatically adds leading dot if not provided.
/// @note Preserves directory portion of the path.
/// @note Empty extension removes the extension.
///
/// @see rt_path_ext For extracting the current extension
/// @see rt_path_stem For filename without extension
rt_string rt_path_with_ext(rt_string path, rt_string new_ext)
{
    const char *path_data = rt_string_safe_data(path);
    size_t path_len = rt_string_safe_len(path);
    const char *ext_data = rt_string_safe_data(new_ext);
    size_t ext_len = rt_string_safe_len(new_ext);

    if (path_len == 0)
        return rt_string_from_bytes(ext_data, ext_len);

    // Find the filename portion
    size_t name_start = path_len;
    while (name_start > 0 && !is_path_sep(path_data[name_start - 1]))
        name_start--;

    // Find the last dot in the filename portion
    size_t dot_pos = path_len;
    for (size_t i = path_len; i > name_start; i--)
    {
        if (path_data[i - 1] == '.')
        {
            // Don't treat leading dot as extension separator
            if (i - 1 > name_start)
            {
                dot_pos = i - 1;
                break;
            }
        }
    }

    rt_string_builder sb;
    rt_sb_init(&sb);

    // Add path up to the extension
    rt_sb_append_bytes(&sb, path_data, dot_pos);

    // Add new extension with dot if needed
    if (ext_len > 0)
    {
        if (ext_data[0] != '.')
            rt_sb_append_bytes(&sb, ".", 1);
        rt_sb_append_bytes(&sb, ext_data, ext_len);
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

/// @brief Check if a path is absolute (starts from root).
///
/// Determines whether a path is absolute (fully qualified) or relative.
/// Absolute paths start from the filesystem root and can be used directly
/// without reference to the current directory.
///
/// **Usage example:**
/// ```
/// ' Check path type
/// If Path.IsAbs(userInput) Then
///     ProcessPath(userInput)
/// Else
///     ProcessPath(Path.Join(Dir.Current(), userInput))
/// End If
///
/// ' Validate configuration paths
/// Dim configPath = GetConfig("output_dir")
/// If Not Path.IsAbs(configPath) Then
///     Print "Warning: relative path in config"
/// End If
/// ```
///
/// **Platform-specific absolute paths:**
/// | Platform | Absolute Path Examples                |
/// |----------|---------------------------------------|
/// | Unix     | `/home/user`, `/etc/config`           |
/// | Windows  | `C:\Users`, `\\server\share`, `\path` |
///
/// **Absolute path detection:**
/// - Unix: Starts with `/`
/// - Windows: Starts with `X:\` (drive letter) or `\\` (UNC path)
///
/// **Examples:**
/// | Input                    | Result | Notes                    |
/// |--------------------------|--------|--------------------------|
/// | `/home/user/file.txt`    | true   | Unix absolute            |
/// | `C:\Users\file.txt`      | true   | Windows drive letter     |
/// | `\\server\share`         | true   | Windows UNC path         |
/// | `src/file.txt`           | false  | Relative                 |
/// | `./file.txt`             | false  | Explicit relative        |
/// | `../file.txt`            | false  | Parent-relative          |
///
/// @param path Path to check.
///
/// @return 1 if absolute, 0 if relative or empty.
///
/// @note O(1) time complexity (checks first few characters).
/// @note Empty paths are considered relative.
///
/// @see rt_path_abs For converting relative to absolute
/// @see rt_path_join For combining path components
int64_t rt_path_is_abs(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return 0;

    // Unix absolute path
    if (data[0] == '/')
        return 1;

#ifdef _WIN32
    // Windows drive letter (C:\)
    if (len >= 3 && isalpha((unsigned char)data[0]) && data[1] == ':' && is_path_sep(data[2]))
        return 1;

    // UNC path (\\server\share)
    if (len >= 2 && is_path_sep(data[0]) && is_path_sep(data[1]))
        return 1;
#endif

    return 0;
}

/// @brief Convert a relative path to an absolute path.
///
/// Converts a relative path to an absolute path by prepending the current
/// working directory. If the path is already absolute, it is normalized
/// and returned. The result is always normalized (no `.` or `..` components).
///
/// **Usage example:**
/// ```
/// ' Convert relative to absolute
/// Dim absPath = Path.Abs("src/main.bas")
/// ' Result: "/home/user/project/src/main.bas"
///
/// ' Already absolute paths are normalized
/// Dim clean = Path.Abs("/home/user/../user/file.txt")
/// ' Result: "/home/user/file.txt"
///
/// ' Store absolute paths in config
/// Dim outputDir = Path.Abs(args(1))
/// SaveConfig("output", outputDir)
/// ```
///
/// **Conversion process:**
/// 1. If path is already absolute → normalize and return
/// 2. Get current working directory
/// 3. Join cwd with path
/// 4. Normalize the result (resolve `.` and `..`)
///
/// **Examples:**
/// | Input             | CWD              | Output                     |
/// |-------------------|------------------|----------------------------|
/// | `src/file.txt`    | `/home/user`     | `/home/user/src/file.txt`  |
/// | `./file.txt`      | `/home/user`     | `/home/user/file.txt`      |
/// | `../other/file`   | `/home/user`     | `/home/other/file`         |
/// | `/absolute/path`  | (any)            | `/absolute/path`           |
///
/// @param path Path to convert (may be relative or absolute).
///
/// @return Newly allocated absolute, normalized path.
///
/// @note O(n) time complexity where n is path length.
/// @note Uses current working directory at time of call.
/// @note Always returns normalized path (no redundant components).
///
/// @see rt_path_is_abs For checking if path is absolute
/// @see rt_path_norm For normalizing without making absolute
/// @see rt_dir_current For getting current working directory
rt_string rt_path_abs(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    // If already absolute, normalize and return
    if (rt_path_is_abs(path))
        return rt_path_norm(path);

    // Get current working directory
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        // Failed to get cwd, just return normalized input
        return rt_path_norm(path);
    }

    // Join cwd with path
    rt_string cwd_str = rt_string_from_bytes(cwd, strlen(cwd));
    rt_string path_str = rt_string_from_bytes(data, len);
    rt_string joined = rt_path_join(cwd_str, path_str);

    rt_string_unref(cwd_str);
    rt_string_unref(path_str);

    // Normalize the result
    rt_string result = rt_path_norm(joined);
    rt_string_unref(joined);

    return result;
}

/// @brief Normalize a path by removing redundant components.
///
/// Cleans up a path by removing redundant separators, resolving `.` (current
/// directory) and `..` (parent directory) components, and normalizing the
/// separator style for the current platform.
///
/// **Usage example:**
/// ```
/// ' Clean up user-provided paths
/// Dim clean = Path.Norm("./src/../src/./file.txt")
/// ' Result: "src/file.txt"
///
/// ' Remove double separators
/// Path.Norm("path//to///file")  ' "path/to/file"
///
/// ' Resolve parent references
/// Path.Norm("a/b/c/../../d")    ' "a/d"
///
/// ' Handle edge cases
/// Path.Norm("")                  ' "."
/// Path.Norm(".")                 ' "."
/// Path.Norm("..")                ' ".."
/// ```
///
/// **Normalization rules:**
/// 1. Remove redundant separators (`//` → `/`)
/// 2. Remove `.` components (current directory)
/// 3. Resolve `..` components (go up one level)
/// 4. Normalize separators to platform style
/// 5. Preserve root prefix (/, C:\, \\server\share)
///
/// **Examples:**
/// | Input                      | Output          |
/// |----------------------------|-----------------|
/// | `./src/./file.txt`         | `src/file.txt`  |
/// | `a/b/../c`                 | `a/c`           |
/// | `a/b/../../c`              | `c`             |
/// | `/a/b/../c`                | `/a/c`          |
/// | `path//to///file`          | `path/to/file`  |
/// | `../../outside`            | `../../outside` |
/// | (empty)                    | `.`             |
///
/// **Edge case: `..` beyond root:**
/// - Absolute paths: `..` at root is ignored (`/../a` → `/a`)
/// - Relative paths: `..` is preserved (`../../a` stays as-is)
///
/// @param path Path to normalize.
///
/// @return Newly allocated normalized path.
///
/// @note O(n) time complexity where n is path length.
/// @note Returns "." for empty input.
/// @note Preserves drive letters and UNC paths on Windows.
///
/// @see rt_path_abs For making paths absolute
/// @see rt_path_join For combining path components
rt_string rt_path_norm(rt_string path)
{
    const char *data = rt_string_safe_data(path);
    size_t len = rt_string_safe_len(path);

    if (len == 0)
        return rt_string_from_bytes(".", 1);

    // Parse path components
    // We'll store component start/end pairs
    size_t *comp_starts = (size_t *)malloc(len * sizeof(size_t));
    size_t *comp_ends = (size_t *)malloc(len * sizeof(size_t));
    if (!comp_starts || !comp_ends)
    {
        free(comp_starts);
        free(comp_ends);
        rt_trap("rt_path: memory allocation failed");
    }
    size_t comp_count = 0;

    int is_absolute = 0;
    size_t prefix_len = 0;

    // Determine prefix (root portion)
#ifdef _WIN32
    if (len >= 2 && isalpha((unsigned char)data[0]) && data[1] == ':')
    {
        prefix_len = 2;
        if (len >= 3 && is_path_sep(data[2]))
        {
            prefix_len = 3;
            is_absolute = 1;
        }
    }
    else if (len >= 2 && is_path_sep(data[0]) && is_path_sep(data[1]))
    {
        // UNC path
        is_absolute = 1;
        prefix_len = 2;
        // Find end of server\share
        int slashes = 0;
        while (prefix_len < len && slashes < 2)
        {
            if (is_path_sep(data[prefix_len]))
                slashes++;
            else if (slashes == 0 || slashes == 1)
                prefix_len++;
            if (slashes < 2)
                prefix_len++;
        }
    }
    else
#endif
        if (is_path_sep(data[0]))
    {
        prefix_len = 1;
        is_absolute = 1;
    }

    // Parse components
    size_t i = prefix_len;
    while (i < len)
    {
        // Skip separators
        while (i < len && is_path_sep(data[i]))
            i++;

        if (i >= len)
            break;

        // Find end of component
        size_t start = i;
        while (i < len && !is_path_sep(data[i]))
            i++;

        size_t comp_len = i - start;

        // Handle . and ..
        if (comp_len == 1 && data[start] == '.')
        {
            // Skip "." components
            continue;
        }
        else if (comp_len == 2 && data[start] == '.' && data[start + 1] == '.')
        {
            // Handle ".." - go up one level if possible
            if (comp_count > 0)
            {
                // Check if previous component is also ".."
                size_t prev_len = comp_ends[comp_count - 1] - comp_starts[comp_count - 1];
                const char *prev = data + comp_starts[comp_count - 1];
                if (prev_len == 2 && prev[0] == '.' && prev[1] == '.')
                {
                    // Previous is "..", keep this one too
                    comp_starts[comp_count] = start;
                    comp_ends[comp_count] = i;
                    comp_count++;
                }
                else
                {
                    // Remove previous component
                    comp_count--;
                }
            }
            else if (!is_absolute)
            {
                // Keep ".." at start of relative path
                comp_starts[comp_count] = start;
                comp_ends[comp_count] = i;
                comp_count++;
            }
            // else: at root, ignore ".."
        }
        else
        {
            // Regular component
            comp_starts[comp_count] = start;
            comp_ends[comp_count] = i;
            comp_count++;
        }
    }

    // Build result
    rt_string_builder sb;
    rt_sb_init(&sb);

    // Add prefix
    if (prefix_len > 0)
    {
        rt_sb_append_bytes(&sb, data, prefix_len);
#ifdef _WIN32
        // Normalize prefix separators on Windows
        for (size_t j = 0; j < sb.len; j++)
        {
            if (sb.data[j] == '/')
                sb.data[j] = '\\';
        }
#endif
    }

    // Add components
    for (size_t j = 0; j < comp_count; j++)
    {
        if (j > 0 || (prefix_len > 0 && !is_path_sep(data[prefix_len - 1])))
            rt_sb_append_bytes(&sb, PATH_SEP_STR, 1);

        rt_sb_append_bytes(&sb, data + comp_starts[j], comp_ends[j] - comp_starts[j]);
    }

    // Handle empty result
    if (sb.len == 0)
    {
        rt_sb_free(&sb);
        free(comp_starts);
        free(comp_ends);
        return rt_string_from_bytes(".", 1);
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(comp_starts);
    free(comp_ends);

    return result;
}

/// @brief Get the platform-specific path separator character.
///
/// Returns the native path separator for the current platform. This is useful
/// for building paths manually or displaying platform-appropriate messages.
///
/// **Usage example:**
/// ```
/// ' Build path manually (prefer Path.Join instead)
/// Dim sep = Path.Sep()
/// Dim path = "src" & sep & "utils" & sep & "helper.bas"
///
/// ' Display platform info
/// Print "Path separator on this system: " & Path.Sep()
///
/// ' Note: Path.Join is usually preferred
/// Dim better = Path.Join("src", Path.Join("utils", "helper.bas"))
/// ```
///
/// **Return value by platform:**
/// | Platform | Return Value |
/// |----------|--------------|
/// | Unix     | `"/"`        |
/// | Windows  | `"\"`        |
///
/// **When to use:**
/// - Displaying paths in UI
/// - Building regex patterns for path matching
/// - Platform-specific path formatting
///
/// **When NOT to use:**
/// - Building paths (use Path.Join instead)
/// - Parsing paths (path functions handle both separators)
///
/// @return Newly allocated string containing "/" on Unix or "\" on Windows.
///
/// @note O(1) time complexity.
/// @note Always returns a single-character string.
///
/// @see rt_path_join For the preferred way to build paths
rt_string rt_path_sep(void)
{
    return rt_string_from_bytes(PATH_SEP_STR, 1);
}
