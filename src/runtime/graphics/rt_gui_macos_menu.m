//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_macos_menu.m
// Purpose: Native macOS menu-bar bridge. Mirrors a `vg_menubar_t` into a
//          real `NSMenu` tree so Viper applications get a proper system
//          menu bar instead of an in-canvas-drawn menu strip.
//
// Key invariants:
//   - At most one Viper menubar is "active" on the native menu at a time,
//     tracked by `g_main_menubar`. Registering a second menubar replaces
//     the first.
//   - Special items (About, Preferences, Quit) are diverted to Apple-managed
//     positions in the application menu and removed from their original
//     menu so they don't appear twice.
//   - Cocoa-level resources (NSMenu*, NSMenuItem*) are autoreleased; the
//     bridge does not retain them itself.
//   - Each `NSMenuItem` carries a `representedObject` that is a wrapped
//     `vg_menu_item_t *`; the dispatcher uses that to invoke the
//     widget-level callback when the user picks the item.
//
// Ownership/Lifetime:
//   - The Viper menubar is the source of truth; the native menu is rebuilt
//     from scratch on every change via `rt_gui_macos_rebuild_main_menu`.
//   - The Viper menubar must outlive its registration window; unregister
//     before destroying it.
//
// Links: src/runtime/graphics/rt_gui_internal.h (vg_menubar_t / vg_menu_t),
//        src/lib/gui/src/widgets/vg_menubar.c (Viper menubar widget).
//
//===----------------------------------------------------------------------===//

#import <Cocoa/Cocoa.h>

#include "../lib/graphics/src/vgfx_internal.h"
#include "rt_gui_internal.h"

#ifdef VIPER_ENABLE_GRAPHICS

@interface RTGuiMacMenuDispatcher : NSObject
+ (instancetype)shared;
- (void)invokeMenuItem:(NSMenuItem *)sender;
- (void)quitApplication:(id)sender;
@end

typedef struct {
    vg_menu_item_t *about_item;
    vg_menu_item_t *preferences_item;
    vg_menu_item_t *quit_item;
} rt_gui_macos_special_items_t;

static vg_menubar_t *g_main_menubar = NULL;

static BOOL rt_gui_macos_menu_contains_item(vg_menu_t *menu, const vg_menu_item_t *target) {
    if (!menu || !target) {
        return NO;
    }
    for (vg_menu_item_t *item = menu->first_item; item; item = item->next) {
        if (item == target) {
            return YES;
        }
        if (item->submenu && rt_gui_macos_menu_contains_item(item->submenu, target)) {
            return YES;
        }
    }
    return NO;
}

static BOOL rt_gui_macos_item_is_live(vg_menu_item_t *item) {
    if (!g_main_menubar || !item) {
        return NO;
    }
    for (vg_menu_t *menu = g_main_menubar->first_menu; menu; menu = menu->next) {
        if (rt_gui_macos_menu_contains_item(menu, item)) {
            return YES;
        }
    }
    return NO;
}

/// @brief Return the user-visible app name for the application menu title.
/// @details Consults three sources in order of preference: the current
///          Viper `s_current_app` title (set by `App.New(title, ...)`),
///          the bundle's `CFBundleName` (when running from an `.app`
///          bundle), and the process name. Falls back to `"Viper"` so the
///          menu always has a non-empty title.
/// @return Autoreleased `NSString` (non-null).
static NSString *rt_gui_macos_app_name(void) {
    if (s_current_app && s_current_app->title && s_current_app->title[0] != '\0') {
        return [NSString stringWithUTF8String:s_current_app->title];
    }

    NSString *bundle_name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@ "CFBundleName"];
    if (bundle_name.length > 0) {
        return bundle_name;
    }

    NSString *process_name = [[NSProcessInfo processInfo] processName];
    return process_name.length > 0 ? process_name : @ "Viper";
}

/// @brief Test whether @p item is the canonical "About" menu entry.
/// @details Matches case-insensitive on the `"About"` prefix (`"About"`,
///          `"About Viper"`, `"about my app"`). Used to divert the item
///          into Apple's application-menu About slot.
static BOOL rt_gui_macos_item_is_about(const vg_menu_item_t *item) {
    return item && item->text && strncasecmp(item->text, "About", 5) == 0;
}

/// @brief Test whether @p item is the canonical "Preferences" menu entry.
/// @details Matches case-insensitive on the `"Preferences"` prefix so it
///          can be relocated to Apple's standard `⌘,` slot in the
///          application menu.
static BOOL rt_gui_macos_item_is_preferences(const vg_menu_item_t *item) {
    return item && item->text && strncasecmp(item->text, "Preferences", 11) == 0;
}

/// @brief Test whether @p item is a quit-style menu entry.
/// @details Accepts `"Quit"`, `"Exit"`, or any `"Quit "`-prefixed string
///          (case-insensitive) so the bridge can route the item to the
///          standard Cocoa `terminate:` action in the application menu.
static BOOL rt_gui_macos_item_is_quit(const vg_menu_item_t *item) {
    return item && item->text &&
           (strcasecmp(item->text, "Quit") == 0 || strcasecmp(item->text, "Exit") == 0 ||
            strncasecmp(item->text, "Quit ", 5) == 0);
}

/// @brief Walk a Viper menu tree and identify About / Preferences / Quit items.
/// @details Used to relocate those entries to Apple-managed positions in the
///          application menu so they appear in their expected positions
///          regardless of where the Viper-side menubar declares them.
///          Recurses into submenus so a "Quit" buried under "File" / "Exit"
///          still gets caught.
/// @param menu     Viper menu tree to scan (may be NULL).
/// @param specials Output struct; fields are filled in only when the matching
///                 item is found and not already set.
static void rt_gui_macos_find_special_items(vg_menu_t *menu,
                                            rt_gui_macos_special_items_t *specials) {
    if (!menu || !specials) {
        return;
    }

    for (vg_menu_item_t *item = menu->first_item; item; item = item->next) {
        if (!item->separator && !item->submenu) {
            if (!specials->about_item && rt_gui_macos_item_is_about(item)) {
                specials->about_item = item;
            } else if (!specials->preferences_item && rt_gui_macos_item_is_preferences(item)) {
                specials->preferences_item = item;
            } else if (!specials->quit_item && rt_gui_macos_item_is_quit(item)) {
                specials->quit_item = item;
            }
        }

        if (item->submenu) {
            rt_gui_macos_find_special_items(item->submenu, specials);
        }
    }
}

/// @brief Test whether @p item should be omitted from the regular menu build pass.
/// @details Special items (About/Preferences/Quit) are emitted into the
///          application menu instead; they must not appear twice in the
///          regular menu structure.
/// @param item     Item under consideration.
/// @param specials Previously-collected special-item pointers.
/// @return `YES` when @p item is one of the special items; `NO` otherwise.
static BOOL rt_gui_macos_should_skip_regular_item(const vg_menu_item_t *item,
                                                  const rt_gui_macos_special_items_t *specials) {
    return item && specials &&
           (item == specials->about_item || item == specials->preferences_item ||
            item == specials->quit_item);
}

/// @brief Map a Viper key code to the Cocoa `keyEquivalent` string.
/// @details Translates `VG_KEY_*` codes into the single-character key
///          equivalent that `NSMenuItem` expects. Letters use lowercase;
///          punctuation maps to its ASCII character; navigation keys
///          (arrows, function keys) map to the architectural Unicode
///          private-use code points Apple defines for them. Unrecognised
///          keys return an empty string so the menu item is left without
///          a key equivalent.
/// @param key Viper `VG_KEY_*` enum value.
/// @return Autoreleased `NSString` (possibly empty).
static NSString *rt_gui_macos_key_equivalent_for_key(int key) {
    if (key >= VG_KEY_A && key <= VG_KEY_Z) {
        unichar ch = (unichar)('a' + (key - VG_KEY_A));
        return [NSString stringWithCharacters:&ch length:1];
    }
    if ((key >= VG_KEY_0 && key <= VG_KEY_9) || key == VG_KEY_SPACE || key == VG_KEY_COMMA ||
        key == VG_KEY_MINUS || key == VG_KEY_PERIOD || key == VG_KEY_SLASH ||
        key == VG_KEY_SEMICOLON || key == VG_KEY_EQUAL || key == VG_KEY_APOSTROPHE ||
        key == VG_KEY_LEFT_BRACKET || key == VG_KEY_BACKSLASH || key == VG_KEY_RIGHT_BRACKET ||
        key == VG_KEY_GRAVE) {
        unichar ch = (unichar)key;
        return [NSString stringWithCharacters:&ch length:1];
    }

    switch (key) {
        case VG_KEY_ENTER: {
            unichar ch = '\r';
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_TAB: {
            unichar ch = '\t';
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_ESCAPE: {
            unichar ch = 0x1B;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_BACKSPACE: {
            unichar ch = NSBackspaceCharacter;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_DELETE: {
            unichar ch = NSDeleteFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_HOME: {
            unichar ch = NSHomeFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_END: {
            unichar ch = NSEndFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_PAGE_UP: {
            unichar ch = NSPageUpFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_PAGE_DOWN: {
            unichar ch = NSPageDownFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_UP: {
            unichar ch = NSUpArrowFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_DOWN: {
            unichar ch = NSDownArrowFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_LEFT: {
            unichar ch = NSLeftArrowFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_RIGHT: {
            unichar ch = NSRightArrowFunctionKey;
            return [NSString stringWithCharacters:&ch length:1];
        }
        case VG_KEY_F1:
        case VG_KEY_F2:
        case VG_KEY_F3:
        case VG_KEY_F4:
        case VG_KEY_F5:
        case VG_KEY_F6:
        case VG_KEY_F7:
        case VG_KEY_F8:
        case VG_KEY_F9:
        case VG_KEY_F10:
        case VG_KEY_F11:
        case VG_KEY_F12: {
            unichar ch = (unichar)(NSF1FunctionKey + (key - VG_KEY_F1));
            return [NSString stringWithCharacters:&ch length:1];
        }
        default:
            return nil;
    }
}

/// @brief Parse a Viper shortcut string and apply it to a Cocoa menu item.
/// @details Accepts shortcut strings of the form `"Ctrl+S"`, `"Cmd+Shift+P"`,
///          `"Ctrl+Alt+F4"`, etc. Splits on `+`, recognises modifier tokens
///          (`Ctrl` → `NSEventModifierFlagCommand` on macOS for the natural
///          mapping, `Cmd` → command, `Alt` → option, `Shift` → shift), and
///          maps the final token to a key equivalent via
///          @ref rt_gui_macos_key_equivalent_for_key.
/// @param native_item Cocoa menu item to configure.
/// @param shortcut    Viper shortcut string (may be NULL).
/// @return `YES` if the shortcut was recognised and applied; `NO` otherwise.
static BOOL rt_gui_macos_apply_shortcut(NSMenuItem *native_item, const char *shortcut) {
    if (!native_item || !shortcut || shortcut[0] == '\0' || strchr(shortcut, ' ') != NULL) {
        return NO;
    }

    vg_accelerator_t accel;
    if (!vg_parse_accelerator(shortcut, &accel)) {
        return NO;
    }

    NSString *key_equivalent = rt_gui_macos_key_equivalent_for_key(accel.key);
    if (!key_equivalent) {
        return NO;
    }

    NSEventModifierFlags flags = 0;
    if (accel.modifiers & (VG_MOD_CTRL | VG_MOD_SUPER)) {
        flags |= NSEventModifierFlagCommand;
    }
    if (accel.modifiers & VG_MOD_SHIFT) {
        flags |= NSEventModifierFlagShift;
    }
    if (accel.modifiers & VG_MOD_ALT) {
        flags |= NSEventModifierFlagOption;
    }

    [native_item setKeyEquivalent:key_equivalent];
    [native_item setKeyEquivalentModifierMask:flags];
    return YES;
}

/// @brief Convert a Viper shortcut string into its user-visible display form.
/// @details Used for menu entries whose shortcut should appear in the menu
///          even when the Cocoa key-equivalent path doesn't apply (e.g. an
///          item that fires the shortcut via a separate handler). Returns
///          the original string normalised to the Cocoa display convention
///          (`⌃`, `⌥`, `⇧`, `⌘`) when modifiers are present.
/// @param shortcut Viper shortcut string (may be NULL).
/// @return Autoreleased display `NSString` or `nil` for unrecognised input.
static NSString *rt_gui_macos_display_shortcut(const char *shortcut) {
    if (!shortcut || shortcut[0] == '\0') {
        return nil;
    }

    NSString *display = [NSString stringWithUTF8String:shortcut];
    if (!display) {
        return nil;
    }

    display = [display stringByReplacingOccurrencesOfString:@ "Ctrl+"
                                                 withString:@ "Cmd+"
                                                    options:NSCaseInsensitiveSearch
                                                      range:NSMakeRange(0, display.length)];
    display = [display stringByReplacingOccurrencesOfString:@ "Command+"
                                                 withString:@ "Cmd+"
                                                    options:NSCaseInsensitiveSearch
                                                      range:NSMakeRange(0, display.length)];
    return display;
}

/// @brief Build an `NSMenuItem` bound to a Viper menu-item callback.
/// @details Constructs the Cocoa item with @p title_override (falling back
///          to the Viper item's `text` when nil), routes its action to the
///          shared `RTGuiMacMenuDispatcher`, stores the Viper item pointer in
///          `representedObject`, applies the shortcut via
///          @ref rt_gui_macos_apply_shortcut, and reflects the item's
///          `enabled` flag.
/// @param item           Viper menu item to mirror.
/// @param title_override Optional title to use in place of `item->text`.
/// @return Autoreleased `NSMenuItem *` bound to the dispatcher.
static NSMenuItem *rt_gui_macos_make_bound_item(vg_menu_item_t *item, NSString *title_override) {
    NSString *title = title_override;
    if (!title) {
        title = item && item->text ? [NSString stringWithUTF8String:item->text] : @ "";
    }
    if (!title) {
        title = @ "";
    }

    NSMenuItem *native_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@ ""];
    [native_item setEnabled:item ? item->enabled : YES];
    [native_item setState:item && item->checked ? NSControlStateValueOn : NSControlStateValueOff];

    if (item && !item->submenu && !item->separator) {
        [native_item setTarget:[RTGuiMacMenuDispatcher shared]];
        [native_item setAction:@selector(invokeMenuItem:)];
        [native_item setRepresentedObject:[NSValue valueWithPointer:item]];

        if (!rt_gui_macos_apply_shortcut(native_item, item->shortcut)) {
            NSString *display_shortcut = rt_gui_macos_display_shortcut(item->shortcut);
            if (display_shortcut.length > 0) {
                [native_item
                    setTitle:[NSString stringWithFormat:@ "%@\t%@", title, display_shortcut]];
            }
        }
    }

    return native_item;
}

static NSMenu *rt_gui_macos_build_regular_menu(vg_menu_t *menu,
                                               const rt_gui_macos_special_items_t *specials);

/// @brief Recursively build a single `NSMenuItem` (or submenu) for a regular Viper item.
/// @details Skips items diverted to the application menu. Separator items
///          become `[NSMenuItem separatorItem]`. Submenu items get their
///          children built first; if the resulting submenu is empty after
///          filtering, the item is dropped to avoid an empty submenu in the
///          UI. Action items are bound via @ref rt_gui_macos_make_bound_item.
/// @param item     Viper menu item to mirror.
/// @param specials Special-items registry (used to skip diverted entries).
/// @return Autoreleased `NSMenuItem *`, or nil when the item should be dropped.
static NSMenuItem *rt_gui_macos_build_regular_item(vg_menu_item_t *item,
                                                   const rt_gui_macos_special_items_t *specials) {
    if (!item || rt_gui_macos_should_skip_regular_item(item, specials)) {
        return nil;
    }

    if (item->separator) {
        return [NSMenuItem separatorItem];
    }

    NSMenuItem *native_item = rt_gui_macos_make_bound_item(item, nil);
    if (item->submenu) {
        NSMenu *submenu = rt_gui_macos_build_regular_menu(item->submenu, specials);
        if ([submenu numberOfItems] == 0) {
            return nil;
        }
        [native_item setTarget:nil];
        [native_item setAction:nil];
        [native_item setSubmenu:submenu];
    }

    return native_item;
}

/// @brief Build the `NSMenu` for a regular Viper menu, skipping special items.
/// @details Walks the Viper menu's item chain, calls
///          @ref rt_gui_macos_build_regular_item for each, and appends the
///          result. Trailing separators are stripped so menus don't end in a
///          dangling divider when their last action item is one of the
///          relocated specials.
/// @param menu     Viper menu to mirror (may be NULL — produces an empty menu).
/// @param specials Special-items registry passed down to per-item builds.
/// @return Autoreleased `NSMenu *` (always non-nil).
static NSMenu *rt_gui_macos_build_regular_menu(vg_menu_t *menu,
                                               const rt_gui_macos_special_items_t *specials) {
    NSString *title = menu && menu->title ? [NSString stringWithUTF8String:menu->title] : @ "";
    if (!title) {
        title = @ "";
    }

    NSMenu *native_menu = [[NSMenu alloc] initWithTitle:title];
    [native_menu setAutoenablesItems:NO];

    for (vg_menu_item_t *item = menu ? menu->first_item : NULL; item; item = item->next) {
        NSMenuItem *native_item = rt_gui_macos_build_regular_item(item, specials);
        if (native_item) {
            [native_menu addItem:native_item];
        }
    }

    while ([native_menu numberOfItems] > 0 &&
           [[native_menu itemAtIndex:[native_menu numberOfItems] - 1] isSeparatorItem]) {
        [native_menu removeItemAtIndex:[native_menu numberOfItems] - 1];
    }

    return native_menu;
}

/// @brief Build the Preferences item for the application menu, if Viper supplied one.
/// @details Wraps the user-supplied preferences item in a bound `NSMenuItem`.
///          If the Viper item didn't declare its own shortcut, applies the
///          Apple-standard `⌘,` key equivalent so users get the expected
///          binding for free.
/// @param specials Special-items registry.
/// @return Autoreleased `NSMenuItem *`, or nil when no Preferences item exists.
static NSMenuItem *rt_gui_macos_build_preferences_item(
    const rt_gui_macos_special_items_t *specials) {
    if (!specials->preferences_item) {
        return nil;
    }

    NSMenuItem *item = rt_gui_macos_make_bound_item(specials->preferences_item, nil);
    if ([[item keyEquivalent] length] == 0) {
        [item setKeyEquivalent:@ ","];
        [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    }
    return item;
}

/// @brief Build the Quit item for the application menu.
/// @details When the Viper menubar declared a Quit item, that callback is
///          wrapped in a bound `NSMenuItem` titled `"Quit <AppName>"` with
///          the standard `⌘Q` key equivalent. Otherwise, fabricates a
///          default Quit item that calls the dispatcher's
///          `quitApplication:` method, so applications without a custom
///          quit handler still get the expected menu entry.
/// @param specials  Special-items registry (provides the user callback if any).
/// @param app_name  App-display name used in the menu label.
/// @return Autoreleased `NSMenuItem *` (always non-nil).
static NSMenuItem *rt_gui_macos_build_quit_item(const rt_gui_macos_special_items_t *specials,
                                                NSString *app_name) {
    if (specials->quit_item) {
        NSMenuItem *item = rt_gui_macos_make_bound_item(
            specials->quit_item, [NSString stringWithFormat:@ "Quit %@", app_name]);
        [item setKeyEquivalent:@ "q"];
        [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        return item;
    }

    NSMenuItem *item =
        [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@ "Quit %@", app_name]
                                   action:@selector(quitApplication:)
                            keyEquivalent:@ "q"];
    [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [item setTarget:[RTGuiMacMenuDispatcher shared]];
    return item;
}

/// @brief Build the standard macOS application menu.
/// @details The application menu is the first menu in every Mac app's menu
///          bar (bold, named after the app) and contains the Apple-managed
///          About / Preferences / Services / Hide / Quit family. This
///          function constructs that menu from scratch on every rebuild,
///          inserting the user-supplied Viper About/Preferences/Quit
///          items where they belong and providing sensible defaults
///          (`orderFrontStandardAboutPanel:`, `quitApplication:`) when
///          they are not supplied.
/// @param specials Special-items registry built by
///                 @ref rt_gui_macos_find_special_items.
/// @return Autoreleased fully-populated `NSMenu *`.
static NSMenu *rt_gui_macos_build_app_menu(const rt_gui_macos_special_items_t *specials) {
    NSString *app_name = rt_gui_macos_app_name();
    NSMenu *app_menu = [[NSMenu alloc] initWithTitle:app_name];
    [app_menu setAutoenablesItems:NO];

    if (specials->about_item) {
        [app_menu
            addItem:rt_gui_macos_make_bound_item(
                        specials->about_item, [NSString stringWithFormat:@ "About %@", app_name])];
    } else {
        NSMenuItem *about_item =
            [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@ "About %@", app_name]
                                       action:@selector(orderFrontStandardAboutPanel:)
                                keyEquivalent:@ ""];
        [about_item setTarget:NSApp];
        [app_menu addItem:about_item];
    }

    NSMenuItem *preferences_item = rt_gui_macos_build_preferences_item(specials);
    if (preferences_item) {
        [app_menu addItem:preferences_item];
    }

    [app_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *services_root = [[NSMenuItem alloc] initWithTitle:@ "Services"
                                                           action:nil
                                                    keyEquivalent:@ ""];
    NSMenu *services_menu = [[NSMenu alloc] initWithTitle:@ "Services"];
    [services_root setSubmenu:services_menu];
    [NSApp setServicesMenu:services_menu];
    [app_menu addItem:services_root];

    [app_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *hide_item =
        [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@ "Hide %@", app_name]
                                   action:@selector(hide:)
                            keyEquivalent:@ "h"];
    [hide_item setTarget:NSApp];
    [hide_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [app_menu addItem:hide_item];

    NSMenuItem *hide_others_item =
        [[NSMenuItem alloc] initWithTitle:@ "Hide Others"
                                   action:@selector(hideOtherApplications:)
                            keyEquivalent:@ "h"];
    [hide_others_item setTarget:NSApp];
    [hide_others_item
        setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [app_menu addItem:hide_others_item];

    NSMenuItem *show_all_item = [[NSMenuItem alloc] initWithTitle:@ "Show All"
                                                           action:@selector(unhideAllApplications:)
                                                    keyEquivalent:@ ""];
    [show_all_item setTarget:NSApp];
    [app_menu addItem:show_all_item];

    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItem:rt_gui_macos_build_quit_item(specials, app_name)];

    return app_menu;
}

/// @brief Build the top-level `NSMenu` reflecting the currently-registered Viper menubar.
/// @details Pre-walks every Viper menu to collect special items, then
///          assembles the menu bar: first the application menu (always
///          present), followed by each Viper-supplied top-level menu in
///          declaration order. Menus titled `"Help"` and `"Window"` are
///          also passed to `NSApp` so Cocoa hooks them up as the OS-managed
///          Help and Windows menus. Empty submenus are dropped to avoid
///          dangling titles in the menu bar.
/// @return Autoreleased top-level `NSMenu *` (always non-nil, always has
///         at least the application menu).
static NSMenu *rt_gui_macos_build_main_menu(void) {
    rt_gui_macos_special_items_t specials = {0};
    if (g_main_menubar && g_main_menubar->native_main_menu && g_main_menubar->base.visible) {
        for (vg_menu_t *menu = g_main_menubar->first_menu; menu; menu = menu->next) {
            rt_gui_macos_find_special_items(menu, &specials);
        }
    }

    NSMenu *main_menu = [[NSMenu alloc] initWithTitle:@ ""];
    [main_menu setAutoenablesItems:NO];

    NSMenuItem *app_root = [[NSMenuItem alloc] initWithTitle:@ "" action:nil keyEquivalent:@ ""];
    [app_root setSubmenu:rt_gui_macos_build_app_menu(&specials)];
    [main_menu addItem:app_root];

    if (!(g_main_menubar && g_main_menubar->native_main_menu && g_main_menubar->base.visible)) {
        return main_menu;
    }

    for (vg_menu_t *menu = g_main_menubar->first_menu; menu; menu = menu->next) {
        NSString *title = menu->title ? [NSString stringWithUTF8String:menu->title] : @ "";
        if (!title) {
            title = @ "";
        }

        NSMenu *submenu = rt_gui_macos_build_regular_menu(menu, &specials);
        if ([submenu numberOfItems] == 0) {
            continue;
        }

        NSMenuItem *root_item = [[NSMenuItem alloc] initWithTitle:title
                                                           action:nil
                                                    keyEquivalent:@ ""];
        [root_item setSubmenu:submenu];
        [root_item setEnabled:menu->enabled];
        [main_menu addItem:root_item];

        if ([title caseInsensitiveCompare:@ "Help"] == NSOrderedSame) {
            [NSApp setHelpMenu:submenu];
        } else if ([title caseInsensitiveCompare:@ "Window"] == NSOrderedSame) {
            [NSApp setWindowsMenu:submenu];
        }
    }

    return main_menu;
}

/// @brief Replace the current `NSApp.mainMenu` with a freshly-built menu tree.
/// @details Called from every menu-mutation entry point (registration,
///          unregistration, item add/remove/enable/etc.) so the OS sees an
///          up-to-date menu structure. Calls `[NSApplication sharedApplication]`
///          first to ensure `NSApp` exists for headless or pre-init paths,
///          then sets the new main menu. The previous menu is released
///          by Cocoa's autorelease pool.
static void rt_gui_macos_rebuild_main_menu(void) {
    [NSApplication sharedApplication];
    if (!(g_main_menubar && g_main_menubar->native_main_menu && g_main_menubar->base.visible)) {
        const char *preferred_title =
            (s_current_app && s_current_app->title && s_current_app->title[0] != '\0')
                ? s_current_app->title
                : NULL;
        vgfx_platform_macos_install_default_main_menu(preferred_title);
        return;
    }

    vgfx_platform_macos_finish_launching_if_needed();
    [NSApp setMainMenu:rt_gui_macos_build_main_menu()];
}

@implementation RTGuiMacMenuDispatcher

+ (instancetype)shared {
    static RTGuiMacMenuDispatcher *shared = nil;
    if (!shared) {
        shared = [[RTGuiMacMenuDispatcher alloc] init];
    }
    return shared;
}

- (void)invokeMenuItem:(NSMenuItem *)sender {
    NSValue *boxed = [sender representedObject];
    vg_menu_item_t *item = boxed ? (vg_menu_item_t *)[boxed pointerValue] : NULL;
    if (!rt_gui_macos_item_is_live(item) || !item->enabled) {
        return;
    }

    item->was_clicked = true;
    if (item->action) {
        item->action(item->action_data);
    }
}

- (void)quitApplication:(id)sender {
    (void)sender;

    NSWindow *target_window = [NSApp keyWindow];
    if (!target_window) {
        target_window = [NSApp mainWindow];
    }

    if (target_window) {
        [target_window performClose:nil];
        return;
    }

    if (s_current_app) {
        s_current_app->should_close = 1;
        return;
    }

    [NSApp terminate:nil];
}

@end

/// @brief Register @p menubar as the active source for the native menu.
/// @details Only one Viper menubar at a time can drive the native menu bar.
///          Calling this with a different menubar while one is already
///          registered fails (returns false); callers must unregister the
///          existing menubar first. Idempotent for the already-registered
///          menubar.
/// @param menubar Viper menubar instance.
/// @return `true` if @p menubar is now the active menubar; `false` if
///         another menubar is already registered.
bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar) {
    if (!menubar) {
        return false;
    }

    if (!g_main_menubar) {
        g_main_menubar = menubar;
        return true;
    }

    return g_main_menubar == menubar;
}

/// @brief Unregister @p menubar and revert to the default Viper menu.
/// @details No-op if @p menubar is not the current active menubar
///          (defensive against double-unregister or stale pointers).
///          After clearing the global, rebuilds the native menu so the
///          OS sees the change immediately.
/// @param menubar Menubar to unregister (must match the currently-active one).
void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar) {
    if (!menubar || g_main_menubar != menubar) {
        return;
    }

    g_main_menubar = NULL;
    rt_gui_macos_rebuild_main_menu();
}

/// @brief Rebuild the native menu after a Viper menubar mutation.
/// @details Called whenever the user code adds, removes, renames, enables,
///          or disables a menu item. No-op when @p menubar is not the
///          active one — only the active menubar's structure is reflected
///          on the OS menu bar.
/// @param menubar Menubar whose state changed.
void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar) {
    if (!menubar || menubar != g_main_menubar) {
        return;
    }

    rt_gui_macos_rebuild_main_menu();
}

void rt_gui_macos_menu_sync_app(rt_gui_app_t *app) {
    if (app && app != s_current_app) {
        return;
    }

    rt_gui_macos_rebuild_main_menu();
}

void rt_gui_macos_menu_app_destroy(rt_gui_app_t *app) {
    if (app && app != s_current_app) {
        return;
    }

    g_main_menubar = NULL;
    rt_gui_macos_rebuild_main_menu();
}

#endif
