//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/dialogs/vg_filedialog_native_win32.c
// Purpose: Native Windows file dialogs (plan 09) — raw COM
//          IFileOpenDialog/IFileSaveDialog implementations of the shared
//          vg_native_* dialog surface, mirroring the macOS panel semantics.
// Key invariants:
//   - COM is initialized lazily once (apartment-threaded; RPC_E_CHANGED_MODE
//     tolerated); every interface acquired is released on all paths.
//   - Returned paths are heap UTF-8 strings owned by the caller (free()).
//   - Any hard COM failure surfaces through vg_native_dialogs_available()
//     so callers can route to the drawn fallback dialog.
// Ownership/Lifetime:
//   - No global state beyond the one-shot COM/availability latch.
// Links: lib/gui/src/dialogs/vg_filedialog_native.h,
//        lib/gui/src/dialogs/vg_filedialog_native.m (macOS counterpart)
//
//===----------------------------------------------------------------------===//

#include "vg_filedialog_native.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <initguid.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <stdlib.h>
#include <string.h>

//=============================================================================
// UTF conversion helpers
//=============================================================================

/// @brief Convert UTF-8 to a caller-owned wide string (NULL-tolerant).
static wchar_t *vgfd_wide(const char *utf8) {
    if (!utf8 || !*utf8)
        return NULL;
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (count <= 0)
        return NULL;
    wchar_t *wide = (wchar_t *)malloc((size_t)count * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, count);
    return wide;
}

/// @brief Convert a wide string to caller-owned UTF-8.
static char *vgfd_utf8(const wchar_t *wide) {
    if (!wide)
        return NULL;
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (count <= 0)
        return NULL;
    char *utf8 = (char *)malloc((size_t)count);
    if (!utf8)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, count, NULL, NULL);
    return utf8;
}

//=============================================================================
// COM lifecycle
//=============================================================================

/// @brief Initialize COM once and probe dialog availability.
/// @return 1 when IFileOpenDialog can be created in this session.
int vg_native_dialogs_available(void) {
    static int s_state = -1;
    if (s_state >= 0)
        return s_state;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        s_state = 0;
        return 0;
    }
    IFileOpenDialog *probe = NULL;
    hr = CoCreateInstance(&CLSID_FileOpenDialog,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          &IID_IFileOpenDialog,
                          (void **)&probe);
    if (SUCCEEDED(hr) && probe) {
        probe->lpVtbl->Release(probe);
        s_state = 1;
    } else {
        s_state = 0;
    }
    return s_state;
}

/// @brief Apply title/initial-folder/filter options to a common dialog.
static void vgfd_apply_common(IFileDialog *dialog,
                              const char *title,
                              const char *initial_path,
                              const char *filter_name,
                              const char *filter_pattern,
                              wchar_t **filter_name_w,
                              wchar_t **filter_spec_w) {
    wchar_t *title_w = vgfd_wide(title);
    if (title_w) {
        dialog->lpVtbl->SetTitle(dialog, title_w);
        free(title_w);
    }
    if (filter_name && filter_pattern && *filter_pattern) {
        *filter_name_w = vgfd_wide(filter_name);
        *filter_spec_w = vgfd_wide(filter_pattern);
        if (*filter_name_w && *filter_spec_w) {
            COMDLG_FILTERSPEC specs[2];
            specs[0].pszName = *filter_name_w;
            specs[0].pszSpec = *filter_spec_w;
            specs[1].pszName = L"All Files";
            specs[1].pszSpec = L"*.*";
            dialog->lpVtbl->SetFileTypes(dialog, 2, specs);
        }
    }
    wchar_t *initial_w = vgfd_wide(initial_path);
    if (initial_w) {
        IShellItem *folder = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial_w, NULL, &IID_IShellItem,
                                                  (void **)&folder)) &&
            folder) {
            dialog->lpVtbl->SetDefaultFolder(dialog, folder);
            folder->lpVtbl->Release(folder);
        }
        free(initial_w);
    }
}

/// @brief Extract a caller-owned UTF-8 filesystem path from a shell item.
static char *vgfd_item_path(IShellItem *item) {
    PWSTR wide = NULL;
    if (FAILED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &wide)) || !wide)
        return NULL;
    char *path = vgfd_utf8(wide);
    CoTaskMemFree(wide);
    return path;
}

//=============================================================================
// Public dialog surface
//=============================================================================

char *vg_native_open_file(const char *title,
                          const char *initial_path,
                          const char *filter_name,
                          const char *filter_pattern) {
    if (!vg_native_dialogs_available())
        return NULL;
    IFileOpenDialog *dialog = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dialog)) ||
        !dialog)
        return NULL;
    wchar_t *fname = NULL, *fspec = NULL;
    vgfd_apply_common((IFileDialog *)dialog, title, initial_path, filter_name, filter_pattern,
                      &fname, &fspec);
    char *result = NULL;
    if (SUCCEEDED(dialog->lpVtbl->Show(dialog, NULL))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(dialog->lpVtbl->GetResult(dialog, &item)) && item) {
            result = vgfd_item_path(item);
            item->lpVtbl->Release(item);
        }
    }
    dialog->lpVtbl->Release(dialog);
    free(fname);
    free(fspec);
    return result;
}

char **vg_native_open_files(const char *title,
                            const char *initial_path,
                            const char *filter_name,
                            const char *filter_pattern,
                            size_t *out_count) {
    if (out_count)
        *out_count = 0;
    if (!vg_native_dialogs_available() || !out_count)
        return NULL;
    IFileOpenDialog *dialog = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dialog)) ||
        !dialog)
        return NULL;
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->lpVtbl->GetOptions(dialog, &options)))
        dialog->lpVtbl->SetOptions(dialog, options | FOS_ALLOWMULTISELECT);
    wchar_t *fname = NULL, *fspec = NULL;
    vgfd_apply_common((IFileDialog *)dialog, title, initial_path, filter_name, filter_pattern,
                      &fname, &fspec);
    char **paths = NULL;
    if (SUCCEEDED(dialog->lpVtbl->Show(dialog, NULL))) {
        IShellItemArray *items = NULL;
        if (SUCCEEDED(dialog->lpVtbl->GetResults(dialog, &items)) && items) {
            DWORD count = 0;
            if (SUCCEEDED(items->lpVtbl->GetCount(items, &count)) && count > 0) {
                paths = (char **)calloc(count, sizeof(char *));
                if (paths) {
                    size_t written = 0;
                    for (DWORD i = 0; i < count; ++i) {
                        IShellItem *item = NULL;
                        if (SUCCEEDED(items->lpVtbl->GetItemAt(items, i, &item)) && item) {
                            char *path = vgfd_item_path(item);
                            if (path)
                                paths[written++] = path;
                            item->lpVtbl->Release(item);
                        }
                    }
                    *out_count = written;
                    if (written == 0) {
                        free(paths);
                        paths = NULL;
                    }
                }
            }
            items->lpVtbl->Release(items);
        }
    }
    dialog->lpVtbl->Release(dialog);
    free(fname);
    free(fspec);
    return paths;
}

void vg_native_free_paths(char **paths, size_t count) {
    if (!paths)
        return;
    for (size_t i = 0; i < count; ++i)
        free(paths[i]);
    free(paths);
}

char *vg_native_save_file(const char *title,
                          const char *initial_path,
                          const char *default_name,
                          const char *filter_name,
                          const char *filter_pattern) {
    if (!vg_native_dialogs_available())
        return NULL;
    IFileSaveDialog *dialog = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileSaveDialog, (void **)&dialog)) ||
        !dialog)
        return NULL;
    wchar_t *fname = NULL, *fspec = NULL;
    vgfd_apply_common((IFileDialog *)dialog, title, initial_path, filter_name, filter_pattern,
                      &fname, &fspec);
    wchar_t *name_w = vgfd_wide(default_name);
    if (name_w) {
        dialog->lpVtbl->SetFileName(dialog, name_w);
        free(name_w);
    }
    char *result = NULL;
    if (SUCCEEDED(dialog->lpVtbl->Show(dialog, NULL))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(dialog->lpVtbl->GetResult(dialog, &item)) && item) {
            result = vgfd_item_path(item);
            item->lpVtbl->Release(item);
        }
    }
    dialog->lpVtbl->Release(dialog);
    free(fname);
    free(fspec);
    return result;
}

char *vg_native_select_folder(const char *title, const char *initial_path) {
    if (!vg_native_dialogs_available())
        return NULL;
    IFileOpenDialog *dialog = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dialog)) ||
        !dialog)
        return NULL;
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->lpVtbl->GetOptions(dialog, &options)))
        dialog->lpVtbl->SetOptions(dialog, options | FOS_PICKFOLDERS);
    wchar_t *fname = NULL, *fspec = NULL;
    vgfd_apply_common((IFileDialog *)dialog, title, initial_path, NULL, NULL, &fname, &fspec);
    char *result = NULL;
    if (SUCCEEDED(dialog->lpVtbl->Show(dialog, NULL))) {
        IShellItem *item = NULL;
        if (SUCCEEDED(dialog->lpVtbl->GetResult(dialog, &item)) && item) {
            result = vgfd_item_path(item);
            item->lpVtbl->Release(item);
        }
    }
    dialog->lpVtbl->Release(dialog);
    free(fname);
    free(fspec);
    return result;
}
