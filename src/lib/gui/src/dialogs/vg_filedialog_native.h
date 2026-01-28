// vg_filedialog_native.h - Native file dialog declarations
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    // Native open file dialog - returns allocated string or NULL
    char *vg_native_open_file(const char *title,
                              const char *initial_path,
                              const char *filter_name,
                              const char *filter_pattern);

    // Native save file dialog - returns allocated string or NULL
    char *vg_native_save_file(const char *title,
                              const char *initial_path,
                              const char *default_name,
                              const char *filter_name,
                              const char *filter_pattern);

    // Native folder selection dialog - returns allocated string or NULL
    char *vg_native_select_folder(const char *title, const char *initial_path);

#ifdef __cplusplus
}
#endif
