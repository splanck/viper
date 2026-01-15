//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: menu_test.c
// Purpose: Test the Phase 1-8 runtime GUI features (menus, clipboard, shortcuts,
//          statusbar, toolbar, codeeditor enhancements, dialogs, findbar, command palette,
//          tooltips, toasts, breadcrumb, minimap, drag and drop)
//
//===----------------------------------------------------------------------===//

#include "../../../src/runtime/rt_gui.h"
#include "../../../src/runtime/rt_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper to create rt_string from C string
static rt_string str(const char* s) {
    return rt_string_from_bytes(s, strlen(s));
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("=== ViperIDE Runtime Phase 1-8 Test ===\n\n");

    //=========================================================================
    // Phase 1: Clipboard API Test
    //=========================================================================
    printf("--- Phase 1: Clipboard API ---\n");

    // Set text to clipboard
    rt_clipboard_set_text(str("Hello from ViperIDE!"));
    printf("Set clipboard: 'Hello from ViperIDE!'\n");

    // Check if clipboard has text
    int64_t has_text = rt_clipboard_has_text();
    printf("Has text: %s\n", has_text ? "yes" : "no");

    // Get text from clipboard
    rt_string clip_text = rt_clipboard_get_text();
    const char* clip_cstr = rt_string_cstr(clip_text);
    if (clip_cstr && clip_cstr[0] != '\0') {
        printf("Got clipboard: '%s'\n", clip_cstr);
    }
    printf("\n");

    //=========================================================================
    // Phase 1: Keyboard Shortcuts Test
    //=========================================================================
    printf("--- Phase 1: Keyboard Shortcuts ---\n");

    // Register shortcuts
    rt_shortcuts_register(str("save"), str("Ctrl+S"), str("Save file"));
    rt_shortcuts_register(str("open"), str("Ctrl+O"), str("Open file"));
    rt_shortcuts_register(str("quit"), str("Ctrl+Q"), str("Quit application"));
    printf("Registered shortcuts: Ctrl+S, Ctrl+O, Ctrl+Q\n");

    // Check if shortcuts are enabled
    printf("'save' enabled: %s\n", rt_shortcuts_is_enabled(str("save")) ? "yes" : "no");

    // Disable a shortcut
    rt_shortcuts_set_enabled(str("quit"), 0);
    printf("'quit' enabled after disable: %s\n", rt_shortcuts_is_enabled(str("quit")) ? "yes" : "no");

    // Check global enabled state
    printf("Global shortcuts enabled: %s\n", rt_shortcuts_get_global_enabled() ? "yes" : "no");
    printf("\n");

    //=========================================================================
    // Phase 1: Window Management Test
    //=========================================================================
    printf("--- Phase 1: Window Management ---\n");

    // These functions need a valid app handle - we test the stubs
    printf("Window management functions available (require running GUI app)\n");
    printf("  - rt_app_set_title()\n");
    printf("  - rt_app_get_width()/get_height()\n");
    printf("  - rt_app_minimize()/maximize()/restore()\n");
    printf("  - rt_app_set_fullscreen()\n");
    printf("  - rt_app_was_close_requested()\n");
    printf("\n");

    //=========================================================================
    // Phase 1: Cursor Styles Test
    //=========================================================================
    printf("--- Phase 1: Cursor Styles ---\n");

    rt_cursor_set(RT_CURSOR_IBEAM);
    printf("Set cursor to IBEAM\n");

    rt_cursor_reset();
    printf("Reset cursor to ARROW\n");

    rt_cursor_set_visible(1);
    printf("Cursor visibility set to visible\n");
    printf("\n");

    //=========================================================================
    // Phase 2: Menu System Test (requires GUI context)
    //=========================================================================
    printf("--- Phase 2: Menu System ---\n");
    printf("Menu system functions available (require running GUI app)\n");
    printf("  - MenuBar: rt_menubar_new(), rt_menubar_add_menu()\n");
    printf("  - Menu: rt_menu_add_item(), rt_menu_add_separator()\n");
    printf("  - MenuItem: rt_menuitem_set_text(), rt_menuitem_is_checked()\n");
    printf("  - ContextMenu: rt_contextmenu_new(), rt_contextmenu_show()\n");
    printf("\n");

    //=========================================================================
    // Phase 2: Context Menu Test (standalone)
    //=========================================================================
    printf("--- Phase 2: ContextMenu (standalone test) ---\n");

    void* ctx = rt_contextmenu_new();
    if (ctx) {
        printf("Created context menu\n");

        void* item1 = rt_contextmenu_add_item(ctx, str("Cut"));
        void* item2 = rt_contextmenu_add_item_with_shortcut(ctx, str("Copy"), str("Ctrl+C"));
        void* item3 = rt_contextmenu_add_item_with_shortcut(ctx, str("Paste"), str("Ctrl+V"));
        rt_contextmenu_add_separator(ctx);
        void* item4 = rt_contextmenu_add_item(ctx, str("Select All"));

        printf("Added items: Cut, Copy, Paste, (separator), Select All\n");

        // Check visibility
        printf("Context menu visible: %s\n", rt_contextmenu_is_visible(ctx) ? "yes" : "no");

        // Enable/disable items
        if (item1) {
            rt_menuitem_set_enabled(item1, 1);
            printf("'Cut' enabled: %s\n", rt_menuitem_is_enabled(item1) ? "yes" : "no");
        }

        // Clean up
        rt_contextmenu_destroy(ctx);
        printf("Destroyed context menu\n");
    } else {
        printf("Failed to create context menu\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 3: StatusBar Test
    //=========================================================================
    printf("--- Phase 3: StatusBar ---\n");

    void* statusbar = rt_statusbar_new(NULL);
    if (statusbar) {
        printf("Created status bar\n");

        rt_statusbar_set_left_text(statusbar, str("Ready"));
        rt_statusbar_set_center_text(statusbar, str("Line 1, Col 1"));
        rt_statusbar_set_right_text(statusbar, str("UTF-8"));
        printf("Set zone texts: Left='Ready', Center='Line 1, Col 1', Right='UTF-8'\n");

        printf("StatusBar visible: %s\n", rt_statusbar_is_visible(statusbar) ? "yes" : "no");

        // Add items
        void* item = rt_statusbar_add_text(statusbar, str("Status Item"), RT_STATUSBAR_ZONE_LEFT);
        if (item) {
            rt_statusbaritem_set_tooltip(item, str("This is a status item"));
            printf("Added status bar item with tooltip\n");
        }

        rt_statusbar_destroy(statusbar);
        printf("Destroyed status bar\n");
    } else {
        printf("Failed to create status bar\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 3: Toolbar Test
    //=========================================================================
    printf("--- Phase 3: Toolbar ---\n");

    void* toolbar = rt_toolbar_new(NULL);
    if (toolbar) {
        printf("Created toolbar\n");

        void* btn1 = rt_toolbar_add_button(toolbar, str("new.png"), str("New File"));
        void* btn2 = rt_toolbar_add_button(toolbar, str("open.png"), str("Open File"));
        rt_toolbar_add_separator(toolbar);
        void* btn3 = rt_toolbar_add_button(toolbar, str("save.png"), str("Save File"));
        printf("Added buttons: New, Open, (separator), Save\n");

        rt_toolbar_set_icon_size(toolbar, RT_TOOLBAR_ICON_LARGE);
        printf("Set icon size to LARGE (32x32)\n");

        rt_toolbar_set_style(toolbar, RT_TOOLBAR_STYLE_ICON_TEXT);
        printf("Set style to ICON_TEXT\n");

        printf("Toolbar item count: %lld\n", (long long)rt_toolbar_get_item_count(toolbar));

        if (btn1) {
            rt_toolbaritem_set_tooltip(btn1, str("Create a new file (Ctrl+N)"));
            printf("Set tooltip on New button\n");
        }

        rt_toolbar_destroy(toolbar);
        printf("Destroyed toolbar\n");
    } else {
        printf("Failed to create toolbar\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 4: CodeEditor Enhancements Test
    //=========================================================================
    printf("--- Phase 4: CodeEditor Enhancements ---\n");
    printf("CodeEditor enhancement functions available (require running GUI app)\n");
    printf("  Syntax Highlighting:\n");
    printf("    - rt_codeeditor_set_language()\n");
    printf("    - rt_codeeditor_set_token_color()\n");
    printf("    - rt_codeeditor_add_highlight()\n");
    printf("  Gutter & Line Numbers:\n");
    printf("    - rt_codeeditor_set_show_line_numbers()\n");
    printf("    - rt_codeeditor_set_gutter_icon()\n");
    printf("    - rt_codeeditor_was_gutter_clicked()\n");
    printf("  Code Folding:\n");
    printf("    - rt_codeeditor_add_fold_region()\n");
    printf("    - rt_codeeditor_fold()/unfold()\n");
    printf("    - rt_codeeditor_is_folded()\n");
    printf("  Multiple Cursors:\n");
    printf("    - rt_codeeditor_get_cursor_count()\n");
    printf("    - rt_codeeditor_add_cursor()\n");
    printf("    - rt_codeeditor_cursor_has_selection()\n");
    printf("\n");

    // Test token type constants
    printf("Token type constants defined:\n");
    printf("  RT_TOKEN_KEYWORD = %d\n", RT_TOKEN_KEYWORD);
    printf("  RT_TOKEN_STRING = %d\n", RT_TOKEN_STRING);
    printf("  RT_TOKEN_COMMENT = %d\n", RT_TOKEN_COMMENT);
    printf("  RT_TOKEN_FUNCTION = %d\n", RT_TOKEN_FUNCTION);
    printf("\n");

    //=========================================================================
    // Phase 5: MessageBox Test
    //=========================================================================
    printf("--- Phase 5: MessageBox ---\n");
    printf("MessageBox dialog functions available:\n");
    printf("  Quick dialogs (require GUI context):\n");
    printf("    - rt_messagebox_info()\n");
    printf("    - rt_messagebox_warning()\n");
    printf("    - rt_messagebox_error()\n");
    printf("    - rt_messagebox_question()\n");
    printf("    - rt_messagebox_confirm()\n");
    printf("  Custom dialogs:\n");
    printf("    - rt_messagebox_new()\n");
    printf("    - rt_messagebox_add_button()\n");
    printf("    - rt_messagebox_show()\n");
    printf("\n");

    printf("MessageBox type constants defined:\n");
    printf("  RT_MESSAGEBOX_INFO = %d\n", RT_MESSAGEBOX_INFO);
    printf("  RT_MESSAGEBOX_WARNING = %d\n", RT_MESSAGEBOX_WARNING);
    printf("  RT_MESSAGEBOX_ERROR = %d\n", RT_MESSAGEBOX_ERROR);
    printf("  RT_MESSAGEBOX_QUESTION = %d\n", RT_MESSAGEBOX_QUESTION);
    printf("\n");

    //=========================================================================
    // Phase 5: FileDialog Test
    //=========================================================================
    printf("--- Phase 5: FileDialog ---\n");
    printf("FileDialog functions available:\n");
    printf("  Quick dialogs:\n");
    printf("    - rt_filedialog_open()\n");
    printf("    - rt_filedialog_open_multiple()\n");
    printf("    - rt_filedialog_save()\n");
    printf("    - rt_filedialog_select_folder()\n");
    printf("  Custom dialogs:\n");
    printf("    - rt_filedialog_new()\n");
    printf("    - rt_filedialog_set_title()\n");
    printf("    - rt_filedialog_set_path()\n");
    printf("    - rt_filedialog_add_filter()\n");
    printf("    - rt_filedialog_show()\n");
    printf("\n");

    printf("FileDialog type constants defined:\n");
    printf("  RT_FILEDIALOG_OPEN = %d\n", RT_FILEDIALOG_OPEN);
    printf("  RT_FILEDIALOG_SAVE = %d\n", RT_FILEDIALOG_SAVE);
    printf("  RT_FILEDIALOG_FOLDER = %d\n", RT_FILEDIALOG_FOLDER);
    printf("\n");

    //=========================================================================
    // Phase 6: FindBar Test
    //=========================================================================
    printf("--- Phase 6: FindBar ---\n");

    void* findbar = rt_findbar_new(NULL);
    if (findbar) {
        printf("Created FindBar\n");

        rt_findbar_set_find_text(findbar, str("search term"));
        printf("Set find text: 'search term'\n");

        rt_findbar_set_case_sensitive(findbar, 1);
        printf("Case sensitive: %s\n", rt_findbar_is_case_sensitive(findbar) ? "yes" : "no");

        rt_findbar_set_whole_word(findbar, 1);
        printf("Whole word: %s\n", rt_findbar_is_whole_word(findbar) ? "yes" : "no");

        rt_findbar_set_regex(findbar, 0);
        printf("Regex: %s\n", rt_findbar_is_regex(findbar) ? "yes" : "no");

        rt_findbar_set_replace_mode(findbar, 1);
        printf("Replace mode: %s\n", rt_findbar_is_replace_mode(findbar) ? "yes" : "no");

        rt_findbar_destroy(findbar);
        printf("Destroyed FindBar\n");
    } else {
        printf("Failed to create FindBar\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 6: CommandPalette Test
    //=========================================================================
    printf("--- Phase 6: CommandPalette ---\n");

    void* palette = rt_commandpalette_new(NULL);
    if (palette) {
        printf("Created CommandPalette\n");

        rt_commandpalette_add_command(palette, str("file.new"), str("New File"), str("File"));
        rt_commandpalette_add_command_with_shortcut(palette, str("file.open"), str("Open File"), str("File"), str("Ctrl+O"));
        rt_commandpalette_add_command_with_shortcut(palette, str("file.save"), str("Save File"), str("File"), str("Ctrl+S"));
        printf("Added commands: New File, Open File (Ctrl+O), Save File (Ctrl+S)\n");

        printf("CommandPalette visible: %s\n", rt_commandpalette_is_visible(palette) ? "yes" : "no");

        rt_commandpalette_destroy(palette);
        printf("Destroyed CommandPalette\n");
    } else {
        printf("Failed to create CommandPalette\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 7: Tooltip Test
    //=========================================================================
    printf("--- Phase 7: Tooltip ---\n");

    rt_tooltip_set_delay(500);
    printf("Set tooltip delay to 500ms\n");

    rt_tooltip_show(str("Hello Tooltip!"), 100, 100);
    printf("Showed tooltip at (100, 100)\n");

    rt_tooltip_show_rich(str("Title"), str("Body text with more details"), 200, 200);
    printf("Showed rich tooltip at (200, 200)\n");

    rt_tooltip_hide();
    printf("Hid tooltip\n");
    printf("\n");

    //=========================================================================
    // Phase 7: Toast/Notifications Test
    //=========================================================================
    printf("--- Phase 7: Toast/Notifications ---\n");

    printf("Toast type constants defined:\n");
    printf("  RT_TOAST_INFO = %d\n", RT_TOAST_INFO);
    printf("  RT_TOAST_SUCCESS = %d\n", RT_TOAST_SUCCESS);
    printf("  RT_TOAST_WARNING = %d\n", RT_TOAST_WARNING);
    printf("  RT_TOAST_ERROR = %d\n", RT_TOAST_ERROR);

    printf("Toast position constants defined:\n");
    printf("  RT_TOAST_POSITION_TOP_RIGHT = %d\n", RT_TOAST_POSITION_TOP_RIGHT);
    printf("  RT_TOAST_POSITION_BOTTOM_LEFT = %d\n", RT_TOAST_POSITION_BOTTOM_LEFT);

    rt_toast_set_position(RT_TOAST_POSITION_TOP_RIGHT);
    printf("Set toast position to TOP_RIGHT\n");

    rt_toast_set_max_visible(3);
    printf("Set max visible toasts to 3\n");

    // Custom toast
    void* custom_toast = rt_toast_new(str("Custom notification"), RT_TOAST_INFO, 5000);
    if (custom_toast) {
        printf("Created custom toast with 5000ms duration\n");
        printf("Was dismissed: %s\n", rt_toast_was_dismissed(custom_toast) ? "yes" : "no");
        rt_toast_dismiss(custom_toast);
        printf("Dismissed custom toast\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 8: Breadcrumb Test
    //=========================================================================
    printf("--- Phase 8: Breadcrumb ---\n");

    void* breadcrumb = rt_breadcrumb_new(NULL);
    if (breadcrumb) {
        printf("Created breadcrumb\n");

        rt_breadcrumb_set_path(breadcrumb, str("src/lib/gui/widgets"), str("/"));
        printf("Set path: src/lib/gui/widgets\n");

        rt_breadcrumb_clear(breadcrumb);
        printf("Cleared breadcrumb\n");

        rt_breadcrumb_add_item(breadcrumb, str("Root"), str("root_data"));
        rt_breadcrumb_add_item(breadcrumb, str("Child"), str("child_data"));
        printf("Added items: Root, Child\n");

        rt_breadcrumb_set_separator(breadcrumb, str(" > "));
        printf("Set separator to ' > '\n");

        printf("Was item clicked: %s\n", rt_breadcrumb_was_item_clicked(breadcrumb) ? "yes" : "no");

        rt_breadcrumb_destroy(breadcrumb);
        printf("Destroyed breadcrumb\n");
    } else {
        printf("Failed to create breadcrumb\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 8: Minimap Test
    //=========================================================================
    printf("--- Phase 8: Minimap ---\n");

    void* minimap = rt_minimap_new(NULL);
    if (minimap) {
        printf("Created minimap\n");

        rt_minimap_set_width(minimap, 100);
        printf("Set width: %lld\n", (long long)rt_minimap_get_width(minimap));

        rt_minimap_set_scale(minimap, 0.1);
        printf("Set scale to 0.1\n");

        rt_minimap_set_show_slider(minimap, 1);
        printf("Enabled viewport slider\n");

        printf("Minimap marker constants defined:\n");
        printf("  RT_MINIMAP_MARKER_ERROR = %d\n", RT_MINIMAP_MARKER_ERROR);
        printf("  RT_MINIMAP_MARKER_WARNING = %d\n", RT_MINIMAP_MARKER_WARNING);

        // Markers (stubs)
        rt_minimap_add_marker(minimap, 10, 0xFF0000FF, RT_MINIMAP_MARKER_ERROR);
        printf("Added error marker at line 10\n");

        rt_minimap_clear_markers(minimap);
        printf("Cleared all markers\n");

        rt_minimap_destroy(minimap);
        printf("Destroyed minimap\n");
    } else {
        printf("Failed to create minimap\n");
    }
    printf("\n");

    //=========================================================================
    // Phase 8: Drag and Drop Test
    //=========================================================================
    printf("--- Phase 8: Drag and Drop ---\n");
    printf("Drag and Drop functions available (stubs - require widget extension):\n");
    printf("  Widget Drag:\n");
    printf("    - rt_widget_set_draggable()\n");
    printf("    - rt_widget_set_drag_data()\n");
    printf("    - rt_widget_is_being_dragged()\n");
    printf("  Widget Drop:\n");
    printf("    - rt_widget_set_drop_target()\n");
    printf("    - rt_widget_set_accepted_drop_types()\n");
    printf("    - rt_widget_is_drag_over()\n");
    printf("    - rt_widget_was_dropped()\n");
    printf("    - rt_widget_get_drop_type()\n");
    printf("    - rt_widget_get_drop_data()\n");
    printf("  File Drop:\n");
    printf("    - rt_app_was_file_dropped()\n");
    printf("    - rt_app_get_dropped_file_count()\n");
    printf("    - rt_app_get_dropped_file()\n");
    printf("\n");

    //=========================================================================
    // Cleanup
    //=========================================================================
    printf("--- Cleanup ---\n");
    rt_shortcuts_clear();
    printf("Cleared all shortcuts\n");

    rt_clipboard_clear();
    printf("Cleared clipboard\n");

    rt_toast_dismiss_all();
    printf("Dismissed all toasts\n");

    printf("\n=== All Phase 1-8 Tests Complete ===\n");
    return 0;
}
