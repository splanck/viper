//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGuiMacMenuTests.mm
// Purpose: Regression coverage for authored macOS application-menu titles.
// Key invariants:
//   - The native executable leaf and Cocoa process identity are `Zanna Studio`.
//   - Installing or removing a Zanna MenuBar preserves that authored title.
//   - Later window-title changes never replace the application identity.
// Ownership/Lifetime:
//   - The test owns one runtime application and its child MenuBar.
//   - Cocoa menu objects are borrowed from NSApp and never retained here.
// Links: runtime/graphics/gui/rt_gui_macos_menu.m,
//        lib/graphics/src/vgfx_platform_macos.m
// Cross-platform touchpoints: This test is compiled only by the Apple CMake branch.
//
//===----------------------------------------------------------------------===//

#if defined(NDEBUG)
#undef NDEBUG
#endif

#import <Cocoa/Cocoa.h>

#include "rt_gui.h"
#include "rt_string.h"

#include <assert.h>
#include <stdio.h>

namespace {

constexpr const char *kAuthoredAppName = "Zanna Studio";

void assertApplicationMenuTitle() {
    NSRunningApplication *runningApplication = [NSRunningApplication currentApplication];
    assert(
        [[[runningApplication executableURL] lastPathComponent] isEqualToString:@"Zanna Studio"]);
    assert([[runningApplication localizedName] isEqualToString:@"Zanna Studio"]);
    assert([[[NSProcessInfo processInfo] processName] isEqualToString:@"Zanna Studio"]);

    NSMenu *mainMenu = [NSApp mainMenu];
    assert(mainMenu);
    assert([mainMenu numberOfItems] > 0);

    NSMenuItem *applicationItem = [mainMenu itemAtIndex:0];
    assert(applicationItem);
    NSMenu *applicationMenu = [applicationItem submenu];
    assert(applicationMenu);
    assert([applicationMenu itemWithTitle:@"About Zanna Studio"]);
    assert([applicationMenu itemWithTitle:@"Hide Zanna Studio"]);
    assert([applicationMenu itemWithTitle:@"Quit Zanna Studio"]);
}

} // namespace

int main() {
    @autoreleasepool {
        void *app = rt_gui_app_new(rt_const_cstr(kAuthoredAppName), 160, 100);
        assert(app);
        assertApplicationMenuTitle();

        void *root = rt_gui_app_get_root(app);
        assert(root);
        void *menuBar = rt_menubar_new(root);
        assert(menuBar);
        assertApplicationMenuTitle();

        rt_app_set_title(app, rt_const_cstr("main.zia — Zanna Studio"));
        assertApplicationMenuTitle();

        rt_menubar_destroy(menuBar);
        assertApplicationMenuTitle();

        rt_gui_app_destroy(app);
    }

    printf("test_rt_gui_macos_menu: PASSED\n");
    return 0;
}
