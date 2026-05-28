//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/dialogs/vg_filedialog_native.m
// Purpose: Native macOS implementation of the platform-agnostic file-dialog
//          surface declared in `vg_filedialog_native.h`. Wraps `NSOpenPanel`
//          / `NSSavePanel` (Cocoa) and the `UniformTypeIdentifiers` framework
//          (`UTType` on macOS 11+; legacy `setAllowedFileTypes:` fallback for
//          older macOS).
//
// Key invariants:
//   - Filter patterns use Win32-style `*.ext;*.ext` syntax; the bridge parses
//     and translates them to `NSArray<UTType *>` / `NSArray<NSString *>`.
//   - Returned `char *` paths are `strdup()`-allocated UTF-8 owned by the
//     caller; multi-result helpers return a `char **` array that must be
//     released with `vg_native_free_paths`.
//   - Every entry point wraps Cocoa work in an `@autoreleasepool` to avoid
//     leaking autoreleased `NSString` / `NSURL` instances on the calling
//     thread.
//
// Ownership/Lifetime:
//   - Caller frees the returned `char *` with `free()`.
//   - Caller frees the returned `char **` (and its entries) with
//     `vg_native_free_paths`.
//
// Links: lib/gui/src/dialogs/vg_filedialog_native.h (declarations),
//        lib/gui/src/widgets/vg_filedialog.c (cross-platform caller)
//
//===----------------------------------------------------------------------===//

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void setAllowedExtensions(NSSavePanel *panel, NSArray *extensions);
void vg_native_free_paths(char **paths, size_t count);

/// @brief Parse a Win32-style filter pattern and apply it to @p panel.
/// @details Splits @p filter_pattern on `;`, trims whitespace, and collects
///          extensions whose tokens start with `*.`. Empty filters and
///          filters with no recognisable `*.ext` entries are no-ops so the
///          panel still shows all files.
/// @param panel          Cocoa save/open panel to constrain.
/// @param filter_pattern Win32 filter string (e.g. `"*.zia;*.bas"`); may be `NULL`.
static void applyFilterPattern(NSSavePanel *panel, const char *filter_pattern) {
    if (!filter_pattern || strlen(filter_pattern) == 0)
        return;

    NSString *pattern = [NSString stringWithUTF8String:filter_pattern];
    NSMutableArray *extensions = [NSMutableArray array];

    NSArray *parts = [pattern componentsSeparatedByString:@ ";"];
    for (NSString *part in parts) {
        NSString *trimmed =
            [part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if ([trimmed hasPrefix:@ "*."]) {
            [extensions addObject:[trimmed substringFromIndex:2]];
        }
    }

    if ([extensions count] > 0) {
        setAllowedExtensions(panel, extensions);
    }
}

/// @brief Apply an array of file extensions as @p panel's allowed content-type filter.
/// @details On macOS 11+ converts each extension to a `UTType` and uses
///          `setAllowedContentTypes:`. On older macOS falls back to the
///          deprecated `setAllowedFileTypes:` (the `#pragma clang diagnostic`
///          suppresses the deprecation warning locally so the rest of the
///          translation unit still warns on accidental use of deprecated APIs).
/// @param panel      Cocoa save/open panel to constrain.
/// @param extensions `NSArray<NSString *>` of bare extensions ("zia", "bas", …).
static void setAllowedExtensions(NSSavePanel *panel, NSArray *extensions) {
    if (@available(macOS 11.0, *)) {
        NSMutableArray<UTType *> *types = [NSMutableArray array];
        for (NSString *ext in extensions) {
            UTType *type = [UTType typeWithFilenameExtension:ext];
            if (type) {
                [types addObject:type];
            }
        }
        if ([types count] > 0) {
            [panel setAllowedContentTypes:types];
        }
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [panel setAllowedFileTypes:extensions];
#pragma clang diagnostic pop
    }
}

/// @brief Run a modal single-file open dialog and return the selected path.
/// @details Constructs an `NSOpenPanel`, applies @p title and @p initial_path,
///          translates @p filter_pattern into an allowed-content-type list,
///          and blocks until the user picks a file or cancels.
/// @param title          Window-title text, UTF-8 (may be `NULL`).
/// @param initial_path   Starting directory, UTF-8 (may be `NULL`).
/// @param filter_name    Human-readable filter label (ignored on macOS — the
///                       OS supplies the label from the UTType).
/// @param filter_pattern Win32 filter string; restricts the panel's selection.
/// @return `strdup()`-allocated UTF-8 path on selection; `NULL` if the user
///         cancelled. Caller owns the result and must `free()` it.
char *vg_native_open_file(const char *title,
                          const char *initial_path,
                          const char *filter_name,
                          const char *filter_pattern) {
    (void)filter_name;
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];

        if (title) {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0) {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];

        applyFilterPattern(panel, filter_pattern);

        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            if (url) {
                const char *path = [[url path] UTF8String];
                return strdup(path);
            }
        }

        return NULL;
    }
}

/// @brief Run a modal multi-file open dialog and return the selected paths.
/// @details Same as @ref vg_native_open_file but with `allowsMultipleSelection`
///          enabled. Writes the selected count through @p out_count.
/// @param title          Window-title text, UTF-8 (may be `NULL`).
/// @param initial_path   Starting directory, UTF-8 (may be `NULL`).
/// @param filter_name    Human-readable filter label (ignored on macOS).
/// @param filter_pattern Win32 filter string.
/// @param out_count      Receives the number of selected paths (0 if cancelled).
/// @return Newly-allocated array of `strdup()`-allocated UTF-8 paths; `NULL`
///         if cancelled. Free with @ref vg_native_free_paths.
char **vg_native_open_files(const char *title,
                            const char *initial_path,
                            const char *filter_name,
                            const char *filter_pattern,
                            size_t *out_count) {
    (void)filter_name;
    if (out_count)
        *out_count = 0;

    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];

        if (title) {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0) {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:YES];
        applyFilterPattern(panel, filter_pattern);

        if ([panel runModal] != NSModalResponseOK)
            return NULL;

        NSArray<NSURL *> *urls = [panel URLs];
        NSUInteger ns_count = [urls count];
        if (ns_count == 0 || ns_count > SIZE_MAX / sizeof(char *))
            return NULL;

        char **paths = calloc((size_t)ns_count, sizeof(char *));
        if (!paths)
            return NULL;

        size_t count = 0;
        for (NSURL *url in urls) {
            const char *path = [[url path] UTF8String];
            paths[count] = path ? strdup(path) : NULL;
            if (!paths[count]) {
                vg_native_free_paths(paths, count);
                return NULL;
            }
            count++;
        }

        if (out_count)
            *out_count = count;
        return paths;
    }
}

/// @brief Free the array returned by @ref vg_native_open_files.
/// @details Releases every entry with `free()` then frees the outer array.
///          Safe on `NULL` arrays.
/// @param paths Array of `strdup()`-allocated UTF-8 strings.
/// @param count Number of entries in @p paths.
void vg_native_free_paths(char **paths, size_t count) {
    if (!paths)
        return;
    for (size_t i = 0; i < count; i++)
        free(paths[i]);
    free(paths);
}

/// @brief Run a modal save-file dialog and return the chosen destination.
/// @details Configures the panel with @p title, @p initial_path, and
///          @p default_name (the pre-filled filename), applies the filter,
///          and blocks until the user confirms or cancels.
/// @param title          Window-title text, UTF-8 (may be `NULL`).
/// @param initial_path   Starting directory, UTF-8 (may be `NULL`).
/// @param default_name   Pre-filled filename, UTF-8 (may be `NULL`).
/// @param filter_name    Human-readable filter label (ignored on macOS).
/// @param filter_pattern Win32 filter string.
/// @return `strdup()`-allocated UTF-8 destination path, or `NULL` if cancelled.
char *vg_native_save_file(const char *title,
                          const char *initial_path,
                          const char *default_name,
                          const char *filter_name,
                          const char *filter_pattern) {
    (void)filter_name;
    @autoreleasepool {
        NSSavePanel *panel = [NSSavePanel savePanel];

        if (title) {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0) {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        if (default_name && strlen(default_name) > 0) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:default_name]];
        }

        applyFilterPattern(panel, filter_pattern);

        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [panel URL];
            if (url) {
                const char *path = [[url path] UTF8String];
                return strdup(path);
            }
        }

        return NULL;
    }
}

/// @brief Run a modal folder-selection dialog and return the chosen directory.
/// @details Builds an `NSOpenPanel` configured with
///          `canChooseFiles=NO, canChooseDirectories=YES`. No file-extension
///          filter is applied — the user picks a directory.
/// @param title        Window-title text, UTF-8 (may be `NULL`).
/// @param initial_path Starting directory, UTF-8 (may be `NULL`).
/// @return `strdup()`-allocated UTF-8 directory path, or `NULL` if cancelled.
char *vg_native_select_folder(const char *title, const char *initial_path) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];

        if (title) {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0) {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];

        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            if (url) {
                const char *path = [[url path] UTF8String];
                return strdup(path);
            }
        }

        return NULL;
    }
}
