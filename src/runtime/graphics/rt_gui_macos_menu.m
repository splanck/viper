//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_macos_menu.m
// Purpose: Native macOS menu-bar bridge for Viper GUI menubars.
//
//===----------------------------------------------------------------------===//

#import <Cocoa/Cocoa.h>

#include "rt_gui_internal.h"
#include "../lib/graphics/src/vgfx_internal.h"

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

static NSString *rt_gui_macos_app_name(void) {
    if (s_current_app && s_current_app->title && s_current_app->title[0] != '\0') {
        return [NSString stringWithUTF8String:s_current_app->title];
    }

    NSString *bundle_name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
    if (bundle_name.length > 0) {
        return bundle_name;
    }

    NSString *process_name = [[NSProcessInfo processInfo] processName];
    return process_name.length > 0 ? process_name : @"Viper";
}

static BOOL rt_gui_macos_item_is_about(const vg_menu_item_t *item) {
    return item && item->text && strncasecmp(item->text, "About", 5) == 0;
}

static BOOL rt_gui_macos_item_is_preferences(const vg_menu_item_t *item) {
    return item && item->text && strncasecmp(item->text, "Preferences", 11) == 0;
}

static BOOL rt_gui_macos_item_is_quit(const vg_menu_item_t *item) {
    return item && item->text &&
           (strcasecmp(item->text, "Quit") == 0 || strcasecmp(item->text, "Exit") == 0 ||
            strncasecmp(item->text, "Quit ", 5) == 0);
}

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

static BOOL rt_gui_macos_should_skip_regular_item(const vg_menu_item_t *item,
                                                  const rt_gui_macos_special_items_t *specials) {
    return item && specials &&
           (item == specials->about_item || item == specials->preferences_item ||
            item == specials->quit_item);
}

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

static NSString *rt_gui_macos_display_shortcut(const char *shortcut) {
    if (!shortcut || shortcut[0] == '\0') {
        return nil;
    }

    NSString *display = [NSString stringWithUTF8String:shortcut];
    if (!display) {
        return nil;
    }

    display = [display stringByReplacingOccurrencesOfString:@"Ctrl+"
                                                 withString:@"Cmd+"
                                                    options:NSCaseInsensitiveSearch
                                                      range:NSMakeRange(0, display.length)];
    display = [display stringByReplacingOccurrencesOfString:@"Command+"
                                                 withString:@"Cmd+"
                                                    options:NSCaseInsensitiveSearch
                                                      range:NSMakeRange(0, display.length)];
    return display;
}

static NSMenuItem *rt_gui_macos_make_bound_item(vg_menu_item_t *item, NSString *title_override) {
    NSString *title = title_override;
    if (!title) {
        title = item && item->text ? [NSString stringWithUTF8String:item->text] : @"";
    }
    if (!title) {
        title = @"";
    }

    NSMenuItem *native_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    [native_item setEnabled:item ? item->enabled : YES];
    [native_item setState:item && item->checked ? NSControlStateValueOn : NSControlStateValueOff];

    if (item && !item->submenu && !item->separator) {
        [native_item setTarget:[RTGuiMacMenuDispatcher shared]];
        [native_item setAction:@selector(invokeMenuItem:)];
        [native_item setRepresentedObject:[NSValue valueWithPointer:item]];

        if (!rt_gui_macos_apply_shortcut(native_item, item->shortcut)) {
            NSString *display_shortcut = rt_gui_macos_display_shortcut(item->shortcut);
            if (display_shortcut.length > 0) {
                [native_item setTitle:[NSString stringWithFormat:@"%@\t%@", title, display_shortcut]];
            }
        }
    }

    return native_item;
}

static NSMenu *rt_gui_macos_build_regular_menu(vg_menu_t *menu,
                                               const rt_gui_macos_special_items_t *specials);

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

static NSMenu *rt_gui_macos_build_regular_menu(vg_menu_t *menu,
                                               const rt_gui_macos_special_items_t *specials) {
    NSString *title = menu && menu->title ? [NSString stringWithUTF8String:menu->title] : @"";
    if (!title) {
        title = @"";
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

static NSMenuItem *rt_gui_macos_build_preferences_item(const rt_gui_macos_special_items_t *specials) {
    if (!specials->preferences_item) {
        return nil;
    }

    NSMenuItem *item = rt_gui_macos_make_bound_item(specials->preferences_item, nil);
    if ([[item keyEquivalent] length] == 0) {
        [item setKeyEquivalent:@","];
        [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    }
    return item;
}

static NSMenuItem *rt_gui_macos_build_quit_item(const rt_gui_macos_special_items_t *specials,
                                                NSString *app_name) {
    if (specials->quit_item) {
        NSMenuItem *item = rt_gui_macos_make_bound_item(
            specials->quit_item, [NSString stringWithFormat:@"Quit %@", app_name]);
        [item setKeyEquivalent:@"q"];
        [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        return item;
    }

    NSMenuItem *item =
        [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Quit %@", app_name]
                                   action:@selector(quitApplication:)
                            keyEquivalent:@"q"];
    [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [item setTarget:[RTGuiMacMenuDispatcher shared]];
    return item;
}

static NSMenu *rt_gui_macos_build_app_menu(const rt_gui_macos_special_items_t *specials) {
    NSString *app_name = rt_gui_macos_app_name();
    NSMenu *app_menu = [[NSMenu alloc] initWithTitle:app_name];
    [app_menu setAutoenablesItems:NO];

    if (specials->about_item) {
        [app_menu addItem:rt_gui_macos_make_bound_item(
                              specials->about_item, [NSString stringWithFormat:@"About %@", app_name])];
    } else {
        NSMenuItem *about_item =
            [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"About %@", app_name]
                                       action:@selector(orderFrontStandardAboutPanel:)
                                keyEquivalent:@""];
        [about_item setTarget:NSApp];
        [app_menu addItem:about_item];
    }

    NSMenuItem *preferences_item = rt_gui_macos_build_preferences_item(specials);
    if (preferences_item) {
        [app_menu addItem:preferences_item];
    }

    [app_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *services_root =
        [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
    NSMenu *services_menu = [[NSMenu alloc] initWithTitle:@"Services"];
    [services_root setSubmenu:services_menu];
    [NSApp setServicesMenu:services_menu];
    [app_menu addItem:services_root];

    [app_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *hide_item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Hide %@", app_name]
                                                       action:@selector(hide:)
                                                keyEquivalent:@"h"];
    [hide_item setTarget:NSApp];
    [hide_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [app_menu addItem:hide_item];

    NSMenuItem *hide_others_item =
        [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                   action:@selector(hideOtherApplications:)
                            keyEquivalent:@"h"];
    [hide_others_item setTarget:NSApp];
    [hide_others_item
        setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [app_menu addItem:hide_others_item];

    NSMenuItem *show_all_item =
        [[NSMenuItem alloc] initWithTitle:@"Show All" action:@selector(unhideAllApplications:)
                            keyEquivalent:@""];
    [show_all_item setTarget:NSApp];
    [app_menu addItem:show_all_item];

    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItem:rt_gui_macos_build_quit_item(specials, app_name)];

    return app_menu;
}

static NSMenu *rt_gui_macos_build_main_menu(void) {
    rt_gui_macos_special_items_t specials = {0};
    if (g_main_menubar && g_main_menubar->native_main_menu && g_main_menubar->base.visible) {
        for (vg_menu_t *menu = g_main_menubar->first_menu; menu; menu = menu->next) {
            rt_gui_macos_find_special_items(menu, &specials);
        }
    }

    NSMenu *main_menu = [[NSMenu alloc] initWithTitle:@""];
    [main_menu setAutoenablesItems:NO];

    NSMenuItem *app_root = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [app_root setSubmenu:rt_gui_macos_build_app_menu(&specials)];
    [main_menu addItem:app_root];

    if (!(g_main_menubar && g_main_menubar->native_main_menu && g_main_menubar->base.visible)) {
        return main_menu;
    }

    for (vg_menu_t *menu = g_main_menubar->first_menu; menu; menu = menu->next) {
        NSString *title = menu->title ? [NSString stringWithUTF8String:menu->title] : @"";
        if (!title) {
            title = @"";
        }

        NSMenu *submenu = rt_gui_macos_build_regular_menu(menu, &specials);
        if ([submenu numberOfItems] == 0) {
            continue;
        }

        NSMenuItem *root_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        [root_item setSubmenu:submenu];
        [root_item setEnabled:menu->enabled];
        [main_menu addItem:root_item];

        if ([title caseInsensitiveCompare:@"Help"] == NSOrderedSame) {
            [NSApp setHelpMenu:submenu];
        } else if ([title caseInsensitiveCompare:@"Window"] == NSOrderedSame) {
            [NSApp setWindowsMenu:submenu];
        }
    }

    return main_menu;
}

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
    if (!item || !item->enabled) {
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

void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar) {
    if (!menubar || g_main_menubar != menubar) {
        return;
    }

    g_main_menubar = NULL;
    rt_gui_macos_rebuild_main_menu();
}

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
