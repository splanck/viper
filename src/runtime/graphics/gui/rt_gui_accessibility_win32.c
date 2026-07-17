//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/gui/rt_gui_accessibility_win32.c
// Purpose: Win32 accessibility-preference adapter for the Viper GUI runtime.
//
// Key invariants:
//   - SystemParametersInfo is queried on demand and results are normalized.
//   - Failed or unsupported queries use the deterministic fallback value zero.
//   - No registry keys or global process settings are modified.
//
// Ownership/Lifetime:
//   - The supplied ViperGFX window is borrowed and not retained.
//   - All Win32 descriptors are stack-owned for the duration of a query.
//
// Links: src/runtime/graphics/gui/rt_gui_accessibility_platform.h,
//        src/runtime/graphics/gui/rt_gui_accessibility.c
//
//===----------------------------------------------------------------------===//

#include "rt_gui_accessibility_platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Query the Win32 HIGHCONTRAST system parameter.
/// @param window Borrowed window handle; unused because the setting is process-independent.
/// @return One when the HCF_HIGHCONTRASTON flag is active, otherwise zero.
int32_t rt_gui_accessibility_platform_high_contrast(vgfx_window_t window) {
    (void)window;
    HIGHCONTRASTW contrast = {0};
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
        return 0;
    return (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0 ? 1 : 0;
}

/// @brief Query whether Win32 client-area animation has been disabled.
/// @details SPI_GETCLIENTAREAANIMATION is the closest native equivalent to a reduced-motion
///          preference. Older SDKs fall back to the general animation setting.
/// @param window Borrowed window handle; unused because the setting is process-independent.
/// @return One when interface animation is disabled, otherwise zero.
int32_t rt_gui_accessibility_platform_reduced_motion(vgfx_window_t window) {
    (void)window;
#ifdef SPI_GETCLIENTAREAANIMATION
    BOOL animations_enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations_enabled, 0)) {
        return animations_enabled ? 0 : 1;
    }
#endif
    ANIMATIONINFO animation = {0};
    animation.cbSize = sizeof(animation);
    if (!SystemParametersInfoW(SPI_GETANIMATION, sizeof(animation), &animation, 0))
        return 0;
    return animation.iMinAnimate ? 0 : 1;
}

/// @brief Query Windows' per-user application light/dark preference.
/// @details `AppsUseLightTheme` is a DWORD maintained by Windows personalization settings. A
///          missing key, access failure, unexpected type, or malformed size uses the stable light
///          fallback. The registry handle is predefined and therefore requires no cleanup.
/// @param window Borrowed ViperGFX window; unused because the setting is per-user.
/// @return One when applications should use dark mode, otherwise zero.
int32_t rt_gui_accessibility_platform_prefers_dark(vgfx_window_t window) {
    (void)window;
    DWORD apps_use_light = 1;
    DWORD byte_count = (DWORD)sizeof(apps_use_light);
    LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD,
                     NULL,
                     &apps_use_light,
                     &byte_count);
    if (status != ERROR_SUCCESS || byte_count != (DWORD)sizeof(apps_use_light))
        return 0;
    return apps_use_light == 0 ? 1 : 0;
}

/// @brief Preserve the headless semantic tree when a Win32 native provider is unavailable.
/// @param window Borrowed native window; currently unused.
/// @param root Borrowed semantic root; currently unused.
void rt_gui_accessibility_platform_attach(vgfx_window_t window, vg_widget_t *root) {
    (void)window;
    (void)root;
}

/// @brief Detach the optional Win32 semantic projection.
/// @param window Borrowed native window; currently unused.
void rt_gui_accessibility_platform_detach(vgfx_window_t window) {
    (void)window;
}

/// @brief Notify the optional Win32 semantic projection of a changed widget.
/// @param window Borrowed native window; currently unused.
/// @param widget Borrowed changed widget; currently unused.
void rt_gui_accessibility_platform_notify(vgfx_window_t window, vg_widget_t *widget) {
    (void)window;
    (void)widget;
}

/// @brief Project a live-region announcement when a Win32 provider is installed.
/// @param window Borrowed native window; currently unused.
/// @param widget Borrowed source widget; currently unused.
/// @param text Borrowed UTF-8 announcement; currently unused.
/// @param mode Requested urgency; currently unused.
void rt_gui_accessibility_platform_announce(vgfx_window_t window,
                                            vg_widget_t *widget,
                                            const char *text,
                                            vg_live_region_mode_t mode) {
    (void)window;
    (void)widget;
    (void)text;
    (void)mode;
}
