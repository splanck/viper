//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/gui/rt_gui_accessibility_macos.m
// Purpose: Cocoa accessibility-preference adapter for the Zanna GUI runtime.
//
// Key invariants:
//   - NSWorkspace is queried on demand so System Settings changes are observable.
//   - Results are normalized to C zero/one values.
//   - No Objective-C object escapes the autorelease scope.
//
// Ownership/Lifetime:
//   - The supplied ZannaGFX window is borrowed and is not dereferenced by this adapter.
//   - NSWorkspace is a process-owned singleton; this file retains no Cocoa object.
//
// Links: src/runtime/graphics/gui/rt_gui_accessibility_platform.h,
//        src/runtime/graphics/gui/rt_gui_accessibility.c
//
//===----------------------------------------------------------------------===//

#include "rt_gui_accessibility_platform.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include "vg_event.h"
#include "vg_ide_widgets.h"
#include "vg_widgets.h"

/// @brief Convert nullable UTF-8 into a non-null Cocoa string without throwing.
/// @param text Borrowed NUL-terminated UTF-8 text; NULL becomes an empty string.
/// @return Autoreleased NSString, empty when the source is invalid UTF-8.
static NSString *rt_gui_accessibility_macos_string(const char *text) {
    if (!text)
        return @"";
    NSString *result = [NSString stringWithUTF8String:text];
    return result ? result : @"";
}

/// @brief Return whether a widget and all ancestors are enabled and visible.
/// @param widget Borrowed widget to inspect; may be NULL.
/// @return YES only for a live, effectively enabled and visible widget.
static BOOL rt_gui_accessibility_macos_is_available(const vg_widget_t *widget) {
    if (!vg_widget_is_live(widget))
        return NO;
    for (const vg_widget_t *current = widget; current; current = current->parent) {
        if (!current->visible || !current->enabled)
            return NO;
    }
    return YES;
}

/// @brief Map a Zanna semantic role to its closest AppKit accessibility role.
/// @param role Stable cross-platform Zanna role.
/// @return Non-null AppKit role identifier.
static NSAccessibilityRole rt_gui_accessibility_macos_role(vg_accessible_role_t role) {
    switch (role) {
        case VG_ACCESSIBLE_ROLE_APPLICATION:
        case VG_ACCESSIBLE_ROLE_WINDOW:
        case VG_ACCESSIBLE_ROLE_GROUP:
            return NSAccessibilityGroupRole;
        case VG_ACCESSIBLE_ROLE_LABEL:
            return NSAccessibilityStaticTextRole;
        case VG_ACCESSIBLE_ROLE_BUTTON:
            return NSAccessibilityButtonRole;
        case VG_ACCESSIBLE_ROLE_CHECKBOX:
            return NSAccessibilityCheckBoxRole;
        case VG_ACCESSIBLE_ROLE_RADIOBUTTON:
            return NSAccessibilityRadioButtonRole;
        case VG_ACCESSIBLE_ROLE_TEXTBOX:
        case VG_ACCESSIBLE_ROLE_SEARCHBOX:
            return NSAccessibilityTextFieldRole;
        case VG_ACCESSIBLE_ROLE_COMBOBOX:
            return NSAccessibilityComboBoxRole;
        case VG_ACCESSIBLE_ROLE_LIST:
            return NSAccessibilityListRole;
        case VG_ACCESSIBLE_ROLE_LISTITEM:
        case VG_ACCESSIBLE_ROLE_ROW:
            return NSAccessibilityRowRole;
        case VG_ACCESSIBLE_ROLE_TREE:
            return NSAccessibilityOutlineRole;
        case VG_ACCESSIBLE_ROLE_TREEITEM:
            return NSAccessibilityRowRole;
        case VG_ACCESSIBLE_ROLE_TABLIST:
            return NSAccessibilityTabGroupRole;
        case VG_ACCESSIBLE_ROLE_TAB:
            return NSAccessibilityRadioButtonRole;
        case VG_ACCESSIBLE_ROLE_TABLE:
            return NSAccessibilityTableRole;
        case VG_ACCESSIBLE_ROLE_CELL:
            return NSAccessibilityCellRole;
        case VG_ACCESSIBLE_ROLE_SLIDER:
            return NSAccessibilitySliderRole;
        case VG_ACCESSIBLE_ROLE_PROGRESSBAR:
            return NSAccessibilityProgressIndicatorRole;
        case VG_ACCESSIBLE_ROLE_DIALOG:
        case VG_ACCESSIBLE_ROLE_ALERT:
            return NSAccessibilityGroupRole;
        case VG_ACCESSIBLE_ROLE_MENU:
            return NSAccessibilityMenuRole;
        case VG_ACCESSIBLE_ROLE_MENUITEM:
            return NSAccessibilityMenuItemRole;
        case VG_ACCESSIBLE_ROLE_TOOLBAR:
            return NSAccessibilityToolbarRole;
        case VG_ACCESSIBLE_ROLE_STATUSBAR:
            return NSAccessibilityGroupRole;
        case VG_ACCESSIBLE_ROLE_IMAGE:
        case VG_ACCESSIBLE_ROLE_VIDEO:
            return NSAccessibilityImageRole;
        case VG_ACCESSIBLE_ROLE_LINK:
            return NSAccessibilityLinkRole;
        case VG_ACCESSIBLE_ROLE_NONE:
        case VG_ACCESSIBLE_ROLE_COUNT:
        default:
            return NSAccessibilityGroupRole;
    }
}

/// @brief Infer the same built-in accessible label used by the headless runtime snapshot.
/// @param widget Borrowed live widget.
/// @return Borrowed UTF-8 label, never NULL.
static const char *rt_gui_accessibility_macos_name(const vg_widget_t *widget) {
    if (!widget)
        return "";
    if (widget->accessibility.name)
        return widget->accessibility.name;
    switch (widget->type) {
        case VG_WIDGET_LABEL:
            return ((const vg_label_t *)widget)->text ? ((const vg_label_t *)widget)->text : "";
        case VG_WIDGET_BUTTON:
            return ((const vg_button_t *)widget)->text ? ((const vg_button_t *)widget)->text : "";
        case VG_WIDGET_TEXTINPUT: {
            const vg_textinput_t *input = (const vg_textinput_t *)widget;
            return input->placeholder ? input->placeholder : (widget->name ? widget->name : "");
        }
        case VG_WIDGET_CHECKBOX:
            return ((const vg_checkbox_t *)widget)->text ? ((const vg_checkbox_t *)widget)->text
                                                         : "";
        case VG_WIDGET_RADIO:
            return ((const vg_radiobutton_t *)widget)->text
                       ? ((const vg_radiobutton_t *)widget)->text
                       : "";
        case VG_WIDGET_DROPDOWN: {
            const vg_dropdown_t *dropdown = (const vg_dropdown_t *)widget;
            return dropdown->placeholder ? dropdown->placeholder
                                         : (widget->name ? widget->name : "");
        }
        case VG_WIDGET_GROUPBOX:
            return ((const vg_groupbox_t *)widget)->title ? ((const vg_groupbox_t *)widget)->title
                                                          : "";
        case VG_WIDGET_DIALOG:
            return ((const vg_dialog_t *)widget)->title ? ((const vg_dialog_t *)widget)->title : "";
        default:
            return widget->name ? widget->name : "";
    }
}

/// @brief Convert one Zanna widget's semantic value into an AppKit object.
/// @param widget Borrowed live widget.
/// @return NSString or NSNumber representing the current value; never nil.
static id rt_gui_accessibility_macos_value(const vg_widget_t *widget) {
    if (!widget)
        return @"";
    if (widget->accessibility.value)
        return rt_gui_accessibility_macos_string(widget->accessibility.value);
    switch (widget->type) {
        case VG_WIDGET_TEXTINPUT:
            return rt_gui_accessibility_macos_string(((const vg_textinput_t *)widget)->text);
        case VG_WIDGET_CHECKBOX:
            if (((const vg_checkbox_t *)widget)->indeterminate)
                return @2;
            return (widget->state & VG_STATE_CHECKED) ? @1 : @0;
        case VG_WIDGET_RADIO:
            return (widget->state & VG_STATE_CHECKED) ? @1 : @0;
        case VG_WIDGET_DROPDOWN: {
            const vg_dropdown_t *dropdown = (const vg_dropdown_t *)widget;
            const char *selected = dropdown->selected_index >= 0 &&
                                           dropdown->selected_index < dropdown->item_count &&
                                           dropdown->items
                                       ? dropdown->items[dropdown->selected_index]
                                       : "";
            return rt_gui_accessibility_macos_string(selected);
        }
        case VG_WIDGET_SLIDER:
            return @(((const vg_slider_t *)widget)->value);
        case VG_WIDGET_PROGRESS:
            return @(((const vg_progressbar_t *)widget)->value);
        case VG_WIDGET_SPINNER:
            return @(((const vg_spinner_t *)widget)->value);
        default:
            return @"";
    }
}

@interface RTGuiAccessibilityElement : NSAccessibilityElement
@property(nonatomic, assign) vg_widget_t *widget;
@property(nonatomic, assign) uint64_t widgetID;
@property(nonatomic, weak) NSView *hostView;
@property(nonatomic, weak) id semanticParent;
@end

/// @brief Create a dynamic native element for one live semantic widget.
/// @param widget Borrowed live widget represented by the element.
/// @param hostView Borrowed native framebuffer view used for screen conversion.
/// @param parent Native semantic parent (view or element), retained weakly.
/// @return New ARC-managed native element, or nil for invalid arguments.
static RTGuiAccessibilityElement *rt_gui_accessibility_macos_element(vg_widget_t *widget,
                                                                     NSView *hostView,
                                                                     id parent) {
    if (!vg_widget_is_live(widget) || !hostView)
        return nil;
    RTGuiAccessibilityElement *element = [[RTGuiAccessibilityElement alloc] init];
    element.widget = widget;
    element.widgetID = widget->id;
    element.hostView = hostView;
    element.semanticParent = parent;
    return element;
}

@implementation RTGuiAccessibilityElement

/// @brief Resolve the borrowed widget only while its immutable ID still matches.
/// @return Live widget pointer, or NULL after destruction/address reuse.
- (vg_widget_t *)resolvedWidget {
    vg_widget_t *widget = self.widget;
    return vg_widget_is_live(widget) && widget->id == self.widgetID ? widget : NULL;
}

/// @brief Report that each projection object is a concrete accessibility element.
/// @return YES while AppKit retains the object.
- (BOOL)isAccessibilityElement {
    return YES;
}

/// @brief Return the current AppKit role derived from the live semantic record.
/// @return AppKit accessibility role; stale nodes degrade to group.
- (NSAccessibilityRole)accessibilityRole {
    vg_widget_t *widget = [self resolvedWidget];
    return rt_gui_accessibility_macos_role(widget ? widget->accessibility.role
                                                  : VG_ACCESSIBLE_ROLE_NONE);
}

/// @brief Return the explicit or inferred accessible name.
/// @return Non-null Cocoa label.
- (NSString *)accessibilityLabel {
    return rt_gui_accessibility_macos_string(
        rt_gui_accessibility_macos_name([self resolvedWidget]));
}

/// @brief Return the supplemental semantic description.
/// @return Non-null Cocoa help text.
- (NSString *)accessibilityHelp {
    vg_widget_t *widget = [self resolvedWidget];
    return rt_gui_accessibility_macos_string(widget ? widget->accessibility.description : NULL);
}

/// @brief Return the live semantic value as text or number.
/// @return Non-null AppKit-compatible accessibility value.
- (id)accessibilityValue {
    return rt_gui_accessibility_macos_value([self resolvedWidget]);
}

/// @brief Return whether the live widget and its ancestor chain are interactive.
/// @return YES only for an effectively visible/enabled live node.
- (BOOL)isAccessibilityEnabled {
    return rt_gui_accessibility_macos_is_available([self resolvedWidget]);
}

/// @brief Return the semantic selected/check state.
/// @return YES for selected or checked nodes.
- (BOOL)isAccessibilitySelected {
    vg_widget_t *widget = [self resolvedWidget];
    return widget && (widget->state & (VG_STATE_SELECTED | VG_STATE_CHECKED)) != 0;
}

/// @brief Return whether this semantic node owns toolkit keyboard focus.
/// @return YES only when the widget's focused state flag is set.
- (BOOL)isAccessibilityFocused {
    vg_widget_t *widget = [self resolvedWidget];
    return widget && (widget->state & VG_STATE_FOCUSED) != 0;
}

/// @brief Move toolkit focus in response to a VoiceOver focus request.
/// @param focused YES to focus this node; NO clears focus only when it currently owns focus.
- (void)setAccessibilityFocused:(BOOL)focused {
    vg_widget_t *widget = [self resolvedWidget];
    if (!widget)
        return;
    if (focused)
        vg_widget_set_focus(widget);
    else if (widget->state & VG_STATE_FOCUSED)
        vg_widget_set_focus(NULL);
}

/// @brief Convert physical top-left widget bounds to AppKit global point coordinates.
/// @return Global screen frame, or NSZeroRect for stale/detached elements.
- (NSRect)accessibilityFrame {
    vg_widget_t *widget = [self resolvedWidget];
    NSView *view = self.hostView;
    if (!widget || !view || !view.window)
        return NSZeroRect;
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    vg_widget_get_screen_bounds(widget, &x, &y, &width, &height);
    CGFloat scale = view.window.backingScaleFactor;
    if (!isfinite(scale) || scale <= 0.0)
        scale = 1.0;
    NSRect local = NSMakeRect((CGFloat)x / scale,
                              view.bounds.size.height - (CGFloat)(y + height) / scale,
                              (CGFloat)width / scale,
                              (CGFloat)height / scale);
    NSRect windowRect = [view convertRect:local toView:nil];
    return [view.window convertRectToScreen:windowRect];
}

/// @brief Return the native semantic parent without retaining a cycle.
/// @return Parent element or host view.
- (id)accessibilityParent {
    return self.semanticParent;
}

/// @brief Materialize current visible semantic children on demand.
/// @details Children borrow live widget pointers guarded by immutable IDs; no stale pointer is
///          dereferenced after toolkit destruction.
/// @return Fresh ordered array mirroring visible widget ownership.
- (NSArray *)accessibilityChildren {
    vg_widget_t *widget = [self resolvedWidget];
    NSView *view = self.hostView;
    if (!widget || !view)
        return @[];
    NSMutableArray *children = [NSMutableArray arrayWithCapacity:(NSUInteger)widget->child_count];
    for (vg_widget_t *child = widget->first_child; child; child = child->next_sibling) {
        if (!child->visible)
            continue;
        RTGuiAccessibilityElement *element = rt_gui_accessibility_macos_element(child, view, self);
        if (element)
            [children addObject:element];
    }
    return children;
}

/// @brief Return a stable diagnostic identifier for automation and accessibility inspection.
/// @return Widget name when set, otherwise a decimal immutable widget ID.
- (NSString *)accessibilityIdentifier {
    vg_widget_t *widget = [self resolvedWidget];
    if (!widget)
        return @"";
    if (widget->name)
        return rt_gui_accessibility_macos_string(widget->name);
    return [NSString stringWithFormat:@"zanna-widget-%llu", (unsigned long long)widget->id];
}

/// @brief Activate the widget through its keyboard event path.
/// @details Using the existing event handler preserves control-specific state transitions and
///          callback ordering for buttons, checkboxes, radios, and other keyboard-activatable
///          widgets.
/// @return YES when the widget handled Space activation, otherwise NO.
- (BOOL)accessibilityPerformPress {
    vg_widget_t *widget = [self resolvedWidget];
    if (!rt_gui_accessibility_macos_is_available(widget))
        return NO;
    vg_widget_set_focus(widget);
    vg_event_t event = vg_event_key(VG_EVENT_KEY_DOWN, VG_KEY_SPACE, 0, VG_MOD_NONE);
    return vg_event_send(widget, &event) ? YES : NO;
}

@end

@interface RTGuiAccessibilityBridge : NSObject
@property(nonatomic, strong) RTGuiAccessibilityElement *rootElement;
@end

@implementation RTGuiAccessibilityBridge
@end

static unsigned char g_rt_gui_accessibility_bridge_key;

/// @brief Query macOS's Increase Contrast accessibility display preference.
/// @param window Borrowed window handle; unused because NSWorkspace exposes a process-wide value.
/// @return One when Increase Contrast is enabled, otherwise zero.
int32_t rt_gui_accessibility_platform_high_contrast(vgfx_window_t window) {
    (void)window;
    @autoreleasepool {
        NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
        return workspace.accessibilityDisplayShouldIncreaseContrast ? 1 : 0;
    }
}

/// @brief Query macOS's Reduce Motion accessibility display preference.
/// @param window Borrowed window handle; unused because NSWorkspace exposes a process-wide value.
/// @return One when Reduce Motion is enabled, otherwise zero.
int32_t rt_gui_accessibility_platform_reduced_motion(vgfx_window_t window) {
    (void)window;
    @autoreleasepool {
        NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
        return workspace.accessibilityDisplayShouldReduceMotion ? 1 : 0;
    }
}

/// @brief Query the effective Aqua appearance selected for the supplied app window.
/// @details A native view's effective appearance takes precedence so per-window appearance
///          overrides are respected. When no view exists, NSApp's process appearance is used.
///          Missing application state deterministically reports light mode.
/// @param window Borrowed ZannaGFX window; may be NULL.
/// @return One for Dark Aqua and zero for Aqua or unavailable state.
int32_t rt_gui_accessibility_platform_prefers_dark(vgfx_window_t window) {
    @autoreleasepool {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(window);
        NSAppearance *appearance = view ? view.effectiveAppearance : NSApp.effectiveAppearance;
        if (!appearance)
            return 0;
        NSAppearanceName match = [appearance
            bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        return [match isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0;
    }
}

/// @brief Attach a dynamic NSAccessibilityElement hierarchy to the framebuffer NSView.
/// @param window Borrowed ZannaGFX window whose native view becomes the accessibility container.
/// @param root Borrowed semantic widget root; NULL delegates to detach behavior.
void rt_gui_accessibility_platform_attach(vgfx_window_t window, vg_widget_t *root) {
    @autoreleasepool {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(window);
        if (!view)
            return;
        if (!vg_widget_is_live(root)) {
            rt_gui_accessibility_platform_detach(window);
            return;
        }
        RTGuiAccessibilityBridge *bridge = [[RTGuiAccessibilityBridge alloc] init];
        bridge.rootElement = rt_gui_accessibility_macos_element(root, view, view);
        if (!bridge.rootElement)
            return;
        objc_setAssociatedObject(
            view, &g_rt_gui_accessibility_bridge_key, bridge, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [view setAccessibilityElement:NO];
        view.accessibilityChildren = @[ bridge.rootElement ];
        NSAccessibilityPostNotification(view, NSAccessibilityLayoutChangedNotification);
    }
}

/// @brief Remove native elements before their borrowed widget tree is destroyed.
/// @param window Borrowed ZannaGFX window; NULL or already-detached windows are harmless.
void rt_gui_accessibility_platform_detach(vgfx_window_t window) {
    @autoreleasepool {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(window);
        if (!view)
            return;
        view.accessibilityChildren = @[];
        objc_setAssociatedObject(
            view, &g_rt_gui_accessibility_bridge_key, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        NSAccessibilityPostNotification(view, NSAccessibilityLayoutChangedNotification);
    }
}

/// @brief Notify AppKit that one dynamic semantic element should be queried again.
/// @param window Borrowed owning ZannaGFX window.
/// @param widget Borrowed changed widget; stale pointers are ignored.
void rt_gui_accessibility_platform_notify(vgfx_window_t window, vg_widget_t *widget) {
    @autoreleasepool {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(window);
        RTGuiAccessibilityBridge *bridge =
            view ? objc_getAssociatedObject(view, &g_rt_gui_accessibility_bridge_key) : nil;
        if (!bridge || !vg_widget_is_live(widget))
            return;
        RTGuiAccessibilityElement *element =
            rt_gui_accessibility_macos_element(widget, view, bridge.rootElement);
        NSAccessibilityPostNotification(element, NSAccessibilityValueChangedNotification);
    }
}

/// @brief Post a VoiceOver announcement while retaining the headless record as source of truth.
/// @param window Borrowed owning ZannaGFX window.
/// @param widget Borrowed live announcement source.
/// @param text Borrowed UTF-8 announcement text.
/// @param mode Polite or assertive announcement urgency.
void rt_gui_accessibility_platform_announce(vgfx_window_t window,
                                            vg_widget_t *widget,
                                            const char *text,
                                            vg_live_region_mode_t mode) {
    @autoreleasepool {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(window);
        RTGuiAccessibilityBridge *bridge =
            view ? objc_getAssociatedObject(view, &g_rt_gui_accessibility_bridge_key) : nil;
        if (!bridge || !vg_widget_is_live(widget))
            return;
        RTGuiAccessibilityElement *element =
            rt_gui_accessibility_macos_element(widget, view, bridge.rootElement);
        NSDictionary *userInfo = @{
            NSAccessibilityAnnouncementKey : rt_gui_accessibility_macos_string(text),
            NSAccessibilityPriorityKey : mode == VG_LIVE_REGION_ASSERTIVE
                ? @(NSAccessibilityPriorityHigh)
                : @(NSAccessibilityPriorityMedium),
        };
        NSAccessibilityPostNotificationWithUserInfo(
            element, NSAccessibilityAnnouncementRequestedNotification, userInfo);
    }
}
