//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX macOS Cocoa Backend
//
// Platform-specific implementation using macOS Cocoa APIs (Objective-C).
// Provides window creation, event handling, framebuffer blitting, and timing
// functions for macOS systems.
//
// Architecture:
//   - NSWindow: Native window container
//   - VGFXView (NSView): Custom view for framebuffer display via CGImage
//   - VGFXWindowDelegate: Handles window lifecycle events (close, resize, focus)
//   - Event Translation: NSEvent → vgfx_event_t mapping
//   - Coordinate Conversion: Cocoa (bottom-left origin) → ViperGFX (top-left origin)
//
// Key macOS Concepts:
//   - Autorelease pools: Required for Objective-C memory management
//   - NSApplication: Singleton application instance (NSApp)
//   - Event loop: nextEventMatchingMask with distantPast for non-blocking
//   - CGImage: Core Graphics image created from raw RGBA framebuffer
//   - mach_absolute_time: High-resolution monotonic timer
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief macOS Cocoa backend implementation for ViperGFX.
/// @details Uses NSWindow, NSView, and Core Graphics to provide window
///          management and framebuffer presentation on macOS.

#include "../include/vgfx.h"
#include "vgfx_internal.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <mach/mach_time.h>
#include <time.h>

//===----------------------------------------------------------------------===//
// Forward Declarations
//===----------------------------------------------------------------------===//

/// @brief Custom NSView subclass for displaying the ViperGFX framebuffer.
/// @details Overrides drawRect: to convert the raw RGBA framebuffer into a
///          CGImage and blit it to the screen.  Handles coordinate system
///          conversion (Cocoa uses bottom-left origin, we use top-left).
@interface VGFXView : NSView
/// @brief Pointer to the ViperGFX window structure (backlink for rendering).
@property(nonatomic, assign) struct vgfx_window *vgfxWindow;
@end

/// @brief Window delegate for handling lifecycle events.
/// @details Implements NSWindowDelegate protocol to receive notifications for
///          window close, resize, and focus change events.  Translates these
///          into vgfx_event_t and enqueues them.
@interface VGFXWindowDelegate : NSObject <NSWindowDelegate>
/// @brief Pointer to the ViperGFX window structure (backlink for events).
@property(nonatomic, assign) struct vgfx_window *vgfxWindow;
@end

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Platform-specific data for macOS windows.
/// @details Allocated and owned by the platform backend.  Stored in
///          vgfx_window->platform_data.  Contains references to the native
///          NSWindow, custom view, delegate, and close request flag.
///
/// @invariant window != nil implies view != nil && delegate != nil
typedef struct
{
    NSWindow *window;             ///< Native NSWindow instance
    VGFXView *view;               ///< Custom view for framebuffer display
    VGFXWindowDelegate *delegate; ///< Delegate for window events
    int close_requested;          ///< 1 if user clicked close button, 0 otherwise
} vgfx_macos_platform;

//===----------------------------------------------------------------------===//
// Key Code Translation
//===----------------------------------------------------------------------===//

/// @brief Translate macOS virtual key codes to vgfx_key_t.
/// @details Attempts to use the character representation first (handles A-Z,
///          0-9, SPACE), then falls back to keycode-based lookup for special
///          keys (arrows, Enter, Escape).  Unrecognized keys return
///          VGFX_KEY_UNKNOWN.
///
/// @param keycode macOS virtual key code from NSEvent
/// @param chars   Character string from NSEvent (used for alphanumeric keys)
/// @return Corresponding vgfx_key_t, or VGFX_KEY_UNKNOWN if not recognized
///
/// @details Key mapping:
///            - A-Z: Mapped to vgfx_key_t enum values (uppercase)
///            - 0-9: Mapped to vgfx_key_t enum values
///            - Space: VGFX_KEY_SPACE
///            - Arrows: VGFX_KEY_LEFT/RIGHT/UP/DOWN
///            - Enter: VGFX_KEY_ENTER (both main and numpad)
///            - Escape: VGFX_KEY_ESCAPE
static vgfx_key_t translate_keycode(unsigned short keycode, NSString *chars)
{
    /* Try to use character first (handles A-Z, 0-9, SPACE) */
    if (chars && [chars length] > 0)
    {
        unichar c = [[chars uppercaseString] characterAtIndex:0];
        if (c >= 'A' && c <= 'Z')
            return (vgfx_key_t)c;
        if (c >= '0' && c <= '9')
            return (vgfx_key_t)c;
        if (c == ' ')
            return VGFX_KEY_SPACE;
    }

    /* Special keys by keycode (macOS-specific virtual key codes) */
    switch (keycode)
    {
        case 0x24:
            return VGFX_KEY_ENTER; /* Return key */
        case 0x4C:
            return VGFX_KEY_ENTER; /* Enter key (numpad) */
        case 0x35:
            return VGFX_KEY_ESCAPE; /* Escape key */
        case 0x33:
            return VGFX_KEY_BACKSPACE; /* Backspace (delete backward) */
        case 0x75:
            return VGFX_KEY_DELETE; /* Forward delete */
        case 0x30:
            return VGFX_KEY_TAB; /* Tab key */
        case 0x73:
            return VGFX_KEY_HOME; /* Home key */
        case 0x77:
            return VGFX_KEY_END; /* End key */
        case 0x7B:
            return VGFX_KEY_LEFT; /* Left arrow */
        case 0x7C:
            return VGFX_KEY_RIGHT; /* Right arrow */
        case 0x7E:
            return VGFX_KEY_UP; /* Up arrow */
        case 0x7D:
            return VGFX_KEY_DOWN; /* Down arrow */
        default:
            return VGFX_KEY_UNKNOWN; /* Unrecognized key */
    }
}

//===----------------------------------------------------------------------===//
// Custom NSView for Framebuffer Display
//===----------------------------------------------------------------------===//

@implementation VGFXView

/// @brief Initialize the view with the given frame rectangle.
/// @details Creates an NSView subclass that will display the ViperGFX
///          framebuffer.  Sets up the backlink to NULL (assigned later
///          by vgfx_platform_init_window).
///
/// @param frameRect Initial frame rectangle for the view (in window coordinates)
/// @return Initialized VGFXView instance, or nil on failure
- (id)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if (self)
    {
        _vgfxWindow = NULL;
    }
    return self;
}

/// @brief Declare the view as opaque (optimization hint for Cocoa).
/// @details Tells the Cocoa drawing system that this view fills its entire
///          bounds with opaque content, enabling faster compositing.
///
/// @return YES (view is always opaque)
- (BOOL)isOpaque
{
    return YES;
}

/// @brief Allow the view to become the first responder.
/// @details Required to receive keyboard events.  Without this, the view
///          won't receive key presses.
///
/// @return YES (view accepts first responder status)
- (BOOL)acceptsFirstResponder
{
    return YES;
}

/// @brief Accept mouse clicks even when the window is not focused.
/// @details Without this, the first click on an unfocused window only gives
///          focus without generating a mouse event.
///
/// @param event The mouse-down event (unused)
/// @return YES (always accept first mouse click)
- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    (void)event;
    return YES;
}

/// @brief Render the framebuffer to the view.
/// @details Called by Cocoa when the view needs to be redrawn (triggered by
///          setNeedsDisplay:YES in vgfx_platform_present).  Creates a CGImage
///          from the raw RGBA framebuffer and draws it to the view's graphics
///          context, handling coordinate system conversion (flip Y axis).
///
/// @param dirtyRect The rectangle that needs redrawing (ignored - we redraw all)
///
/// @details Rendering process:
///            1. Create CGColorSpace (device RGB)
///            2. Create CGDataProvider from framebuffer pointer (zero-copy)
///            3. Create CGImage with format info (8-bit RGBA, 32 bpp)
///            4. Flip coordinate system (Cocoa bottom-left → top-left)
///            5. Draw CGImage to view's CGContext
///            6. Restore coordinate system and cleanup
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;

    if (!_vgfxWindow || !_vgfxWindow->pixels)
    {
        /* No framebuffer available - clear to black */
        [[NSColor blackColor] setFill];
        NSRectFill(self.bounds);
        return;
    }

    /* Create CGImage from framebuffer (zero-copy via CGDataProvider) */
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL,                                      /* info (release callback context) */
        _vgfxWindow->pixels,                       /* data pointer (framebuffer) */
        _vgfxWindow->stride * _vgfxWindow->height, /* data size in bytes */
        NULL);                                     /* release callback (none, we own the buffer) */

    CGImageRef image =
        CGImageCreate(_vgfxWindow->width,  /* width in pixels */
                      _vgfxWindow->height, /* height in pixels */
                      8,                   /* bits per component (R/G/B/A each 8 bits) */
                      32,                  /* bits per pixel (4 * 8 = 32) */
                      _vgfxWindow->stride, /* bytes per row */
                      colorSpace,          /* color space (RGB) */
                      kCGBitmapByteOrderDefault | kCGImageAlphaLast, /* format (RGBA, alpha last) */
                      provider,                                      /* data provider */
                      NULL,                                          /* decode array (none) */
                      false,                      /* should interpolate (no, pixel art) */
                      kCGRenderingIntentDefault); /* rendering intent */

    /* Get the view's Core Graphics context */
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];

    /* Use view bounds for the destination rect - may differ from framebuffer on Retina */
    CGFloat view_height = self.bounds.size.height;
    CGFloat view_width = self.bounds.size.width;

    /* Debug: log dimension mismatch once */
    static int debug_logged = 0;
    if (!debug_logged && (view_width != _vgfxWindow->width || view_height != _vgfxWindow->height))
    {
        NSLog(@ "vgfx: View bounds (%.0f x %.0f) differ from framebuffer (%d x %d)",
              view_width,
              view_height,
              _vgfxWindow->width,
              _vgfxWindow->height);
        debug_logged = 1;
    }

    /*
     * Note: We deliberately DON'T flip the coordinate system here.
     * CGImage has origin at top-left (row 0 at top), and Cocoa's default
     * coordinate system has origin at bottom-left. When we draw without
     * flipping, CGContextDrawImage will flip the image for us (image appears
     * "upside down" from its data perspective, but correct visually).
     *
     * This is because CGContextDrawImage interprets the image with its
     * origin at the rect's origin, and the image fills the rect upward from there.
     */
    CGRect rect = CGRectMake(0, 0, view_width, view_height);
    CGContextDrawImage(context, rect, image);

    /* Cleanup (release Core Graphics objects) */
    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
}

@end

//===----------------------------------------------------------------------===//
// Window Delegate for Events
//===----------------------------------------------------------------------===//

@implementation VGFXWindowDelegate

/// @brief Handle window close button click.
/// @details Called when the user clicks the red close button (⊗).  We don't
///          actually close the window here - instead, we set a flag and enqueue
///          a CLOSE event for the application to handle.  This gives the app
///          a chance to save state, prompt the user, etc.  Returns NO to prevent
///          Cocoa from closing the window automatically.
///
/// @param sender The NSWindow that is attempting to close (unused)
/// @return NO (don't close the window automatically, let app handle it)
///
/// @post close_requested flag set to 1
/// @post CLOSE event enqueued in the event queue
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;

    if (!_vgfxWindow)
        return NO;

    vgfx_macos_platform *platform = (vgfx_macos_platform *)_vgfxWindow->platform_data;
    if (!platform)
        return NO;

    /* Mark close as requested (can be checked by the application) */
    platform->close_requested = 1;
    _vgfxWindow->close_requested = 1;

    /* Enqueue CLOSE event for the application to handle */
    vgfx_event_t event = {.type = VGFX_EVENT_CLOSE, .time_ms = vgfx_platform_now_ms()};
    vgfx_internal_enqueue_event(_vgfxWindow, &event);

    /* Don't actually close the window - let the application decide */
    return NO;
}

/// @brief Handle window resize.
/// @details Called when the window's size changes (user drag, maximize, etc.).
///          Updates the ViperGFX window dimensions and reallocates the
///          framebuffer to match the new size.  Enqueues a RESIZE event.
///
/// @param notification Notification object with resize details (unused)
///
/// @post Window dimensions updated to match new size
/// @post Framebuffer reallocated and cleared to black
/// @post RESIZE event enqueued in the event queue
///
/// @warning Framebuffer reallocation may fail (memory allocation).  In that
///          case, the window will have a NULL framebuffer and rendering will
///          display black (handled in drawRect:).
- (void)windowDidResize:(NSNotification *)notification
{
    (void)notification;

    if (!_vgfxWindow)
        return;

    vgfx_macos_platform *platform = (vgfx_macos_platform *)_vgfxWindow->platform_data;
    if (!platform || !platform->view)
        return;

    /* Get new view dimensions */
    NSRect contentRect = [platform->view bounds];
    int32_t new_width = (int32_t)contentRect.size.width;
    int32_t new_height = (int32_t)contentRect.size.height;

    /* Only handle if size actually changed (avoid redundant reallocation) */
    if (new_width == _vgfxWindow->width && new_height == _vgfxWindow->height)
    {
        return;
    }

    /* Update window dimensions */
    _vgfxWindow->width = new_width;
    _vgfxWindow->height = new_height;
    _vgfxWindow->stride = new_width * 4;

    /* Reallocate framebuffer to match new size */
    if (_vgfxWindow->pixels)
    {
        free(_vgfxWindow->pixels);
    }

    size_t buffer_size = (size_t)new_width * (size_t)new_height * 4;
    _vgfxWindow->pixels = (uint8_t *)malloc(buffer_size);

    if (_vgfxWindow->pixels)
    {
        /* Clear framebuffer to black (RGB = 0, 0, 0, A = 0) */
        memset(_vgfxWindow->pixels, 0, buffer_size);
    }

    /* Enqueue RESIZE event for the application to handle */
    vgfx_event_t event = {.type = VGFX_EVENT_RESIZE,
                          .time_ms = vgfx_platform_now_ms(),
                          .data.resize = {.width = new_width, .height = new_height}};
    vgfx_internal_enqueue_event(_vgfxWindow, &event);
}

/// @brief Handle window gaining focus.
/// @details Called when the window becomes the key window (receives keyboard
///          input).  Enqueues a FOCUS_GAINED event.
///
/// @param notification Notification object (unused)
///
/// @post FOCUS_GAINED event enqueued in the event queue
- (void)windowDidBecomeKey:(NSNotification *)notification
{
    (void)notification;

    if (!_vgfxWindow)
        return;

    vgfx_event_t event = {.type = VGFX_EVENT_FOCUS_GAINED, .time_ms = vgfx_platform_now_ms()};
    vgfx_internal_enqueue_event(_vgfxWindow, &event);
}

/// @brief Handle window losing focus.
/// @details Called when the window resigns key window status (loses keyboard
///          input).  Enqueues a FOCUS_LOST event.
///
/// @param notification Notification object (unused)
///
/// @post FOCUS_LOST event enqueued in the event queue
- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;

    if (!_vgfxWindow)
        return;

    vgfx_event_t event = {.type = VGFX_EVENT_FOCUS_LOST, .time_ms = vgfx_platform_now_ms()};
    vgfx_internal_enqueue_event(_vgfxWindow, &event);
}

@end

//===----------------------------------------------------------------------===//
// Platform API Implementation
//===----------------------------------------------------------------------===//

/// @brief Initialize platform-specific window resources for macOS.
/// @details Creates an NSWindow with a custom VGFXView for framebuffer display.
///          Initializes NSApplication (required for any Cocoa window), creates
///          the window with appropriate style mask, sets up the delegate, and
///          makes the window visible.
///
/// @param win    Pointer to the ViperGFX window structure (framebuffer already allocated)
/// @param params Window creation parameters (title, dimensions, resizable flag)
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  params != NULL
/// @pre  win->pixels != NULL (framebuffer allocated by vgfx_create_window)
/// @post On success: NSWindow created and visible, platform_data allocated
/// @post On failure: platform_data NULL, error set
///
/// @details The window is:
///            - Titled (has a title bar)
///            - Closable (red ⊗ button)
///            - Miniaturizable (yellow ⊖ button)
///            - Resizable (optional, based on params->resizable)
///            - Centered on screen
///            - Made key and ordered front (visible and focused)
int vgfx_platform_init_window(struct vgfx_window *win, const vgfx_window_params_t *params)
{
    if (!win || !params)
        return 0;

    @autoreleasepool
    {
        /* Ensure NSApplication is initialized (singleton) */
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        /* Allocate platform data structure */
        vgfx_macos_platform *platform =
            (vgfx_macos_platform *)calloc(1, sizeof(vgfx_macos_platform));
        if (!platform)
        {
            vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate platform data");
            return 0;
        }

        win->platform_data = platform;
        platform->close_requested = 0;

        /* Create window style mask (controls which buttons appear in title bar) */
        NSWindowStyleMask styleMask =
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;

        if (params->resizable)
        {
            styleMask |= NSWindowStyleMaskResizable;
        }

        /* Create NSWindow */
        NSRect contentRect = NSMakeRect(0, 0, params->width, params->height);
        platform->window =
            [[NSWindow alloc] initWithContentRect:contentRect
                                        styleMask:styleMask
                                          backing:NSBackingStoreBuffered /* Double-buffered */
                                            defer:NO];                   /* Create immediately */

        if (!platform->window)
        {
            free(platform);
            win->platform_data = NULL;
            vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create NSWindow");
            return 0;
        }

        /* Set window title */
        [platform->window setTitle:[NSString stringWithUTF8String:params->title]];

        /* Create custom view for framebuffer display */
        platform->view = [[VGFXView alloc] initWithFrame:contentRect];
        platform->view.vgfxWindow = win; /* Backlink for rendering */

        /* Set view as the window's content view */
        [platform->window setContentView:platform->view];

        /* Create and set window delegate */
        platform->delegate = [[VGFXWindowDelegate alloc] init];
        platform->delegate.vgfxWindow = win; /* Backlink for events */
        [platform->window setDelegate:platform->delegate];

        /* Center window on screen and make it visible */
        [platform->window center];
        [platform->window makeKeyAndOrderFront:nil];

        /* Activate the application (bring to foreground) */
        [NSApp activateIgnoringOtherApps:YES];

        return 1;
    }
}

/// @brief Destroy platform-specific window resources for macOS.
/// @details Closes the NSWindow, releases the delegate and view, and frees
///          the platform data structure.  Safe to call even if init failed.
///
/// @param win Pointer to the ViperGFX window structure
///
/// @pre  win != NULL
/// @post platform_data freed and set to NULL
/// @post NSWindow closed and released (if it existed)
void vgfx_platform_destroy_window(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;

    @autoreleasepool
    {
        vgfx_macos_platform *platform = (vgfx_macos_platform *)win->platform_data;

        /* Close and release window */
        if (platform->window)
        {
            [platform->window setDelegate:nil]; /* Clear delegate to avoid stale pointer */
            [platform->window close];
            platform->window = nil;
        }

        /* Release delegate (ARC will deallocate) */
        if (platform->delegate)
        {
            platform->delegate = nil;
        }

        /* Release view (ARC will deallocate) */
        if (platform->view)
        {
            platform->view = nil;
        }

        /* Free platform data */
        free(platform);
        win->platform_data = NULL;
    }
}

/// @brief Process pending macOS events and translate to ViperGFX events.
/// @details Polls the NSApplication event queue in non-blocking mode
///          (distantPast = don't wait).  For each NSEvent, sends it to the
///          application for normal processing, then translates it to a
///          vgfx_event_t and enqueues it.
///
///          Handles:
///            - Keyboard: NSEventTypeKeyDown/KeyUp → KEY_DOWN/KEY_UP
///            - Mouse move: NSEventTypeMouseMoved/Dragged → MOUSE_MOVE
///            - Mouse buttons: NSEventTypeLeftMouse* → MOUSE_DOWN/MOUSE_UP
///
/// @param win Pointer to the ViperGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->platform_data != NULL
/// @post All pending NSEvents processed and translated
/// @post win->key_state and win->mouse_* updated to reflect current input state
/// @post Corresponding vgfx_event_t enqueued for each NSEvent
///
/// @details Coordinate conversion:
///            - NSEvent coordinates: origin at bottom-left
///            - ViperGFX coordinates: origin at top-left
///            - Conversion: vgfx_y = (view_height - ns_y - 1)
int vgfx_platform_process_events(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    @autoreleasepool
    {
        vgfx_macos_platform *platform = (vgfx_macos_platform *)win->platform_data;

        /* Process all pending events without blocking (non-blocking poll) */
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast] /* Don't wait */
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]))
        { /* Remove from queue */

            /* Don't forward keyboard events to NSApp - we handle them directly.
               This prevents the system beep when arrow keys are pressed. */
            NSEventType eventType = [event type];
            if (eventType != NSEventTypeKeyDown && eventType != NSEventTypeKeyUp)
            {
                [NSApp sendEvent:event];
            }

            /* Translate to ViperGFX events */
            int64_t timestamp = vgfx_platform_now_ms();

            switch ([event type])
            {
                case NSEventTypeKeyDown:
                {
                    vgfx_key_t key =
                        translate_keycode([event keyCode], [event charactersIgnoringModifiers]);
                    if (key != VGFX_KEY_UNKNOWN && key < 512)
                    {
                        win->key_state[key] = 1; /* Update input state */

                        /* Extract modifier flags from NSEvent */
                        int mods = 0;
                        NSEventModifierFlags flags = [event modifierFlags];
                        if (flags & NSEventModifierFlagShift)
                            mods |= VGFX_MOD_SHIFT;
                        if (flags & NSEventModifierFlagControl)
                            mods |= VGFX_MOD_CTRL;
                        if (flags & NSEventModifierFlagOption)
                            mods |= VGFX_MOD_ALT;
                        if (flags & NSEventModifierFlagCommand)
                            mods |= VGFX_MOD_CMD;

                        vgfx_event_t vgfx_event = {
                            .type = VGFX_EVENT_KEY_DOWN,
                            .time_ms = timestamp,
                            .data.key = {.key = key,
                                         .is_repeat = [event isARepeat] ? 1 : 0,
                                         .modifiers = mods}};
                        vgfx_internal_enqueue_event(win, &vgfx_event);
                    }
                    break;
                }

                case NSEventTypeKeyUp:
                {
                    vgfx_key_t key =
                        translate_keycode([event keyCode], [event charactersIgnoringModifiers]);
                    if (key != VGFX_KEY_UNKNOWN && key < 512)
                    {
                        win->key_state[key] = 0; /* Update input state */

                        vgfx_event_t vgfx_event = {.type = VGFX_EVENT_KEY_UP,
                                                   .time_ms = timestamp,
                                                   .data.key = {.key = key, .is_repeat = 0}};
                        vgfx_internal_enqueue_event(win, &vgfx_event);
                    }
                    break;
                }

                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged:
                case NSEventTypeOtherMouseDragged:
                {
                    NSPoint location = [event locationInWindow];
                    NSRect contentRect = [platform->view bounds];

                    /* Convert to ViperGFX coordinate system (top-left origin) */
                    int32_t x = (int32_t)location.x;
                    int32_t y = (int32_t)(contentRect.size.height - location.y - 1);

                    win->mouse_x = x; /* Update input state */
                    win->mouse_y = y;

                    vgfx_event_t vgfx_event = {.type = VGFX_EVENT_MOUSE_MOVE,
                                               .time_ms = timestamp,
                                               .data.mouse_move = {.x = x, .y = y}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                    break;
                }

                case NSEventTypeLeftMouseDown:
                case NSEventTypeRightMouseDown:
                case NSEventTypeOtherMouseDown:
                {
                    NSPoint location = [event locationInWindow];
                    NSRect contentRect = [platform->view bounds];

                    int32_t x = (int32_t)location.x;
                    int32_t y = (int32_t)(contentRect.size.height - location.y - 1);

                    /* Determine which button was pressed */
                    vgfx_mouse_button_t button = VGFX_MOUSE_LEFT;
                    if ([event type] == NSEventTypeRightMouseDown)
                    {
                        button = VGFX_MOUSE_RIGHT;
                    }
                    else if ([event type] == NSEventTypeOtherMouseDown)
                    {
                        button = VGFX_MOUSE_MIDDLE;
                    }

                    if (button < 8)
                    {
                        win->mouse_button_state[button] = 1; /* Update input state */
                    }

                    vgfx_event_t vgfx_event = {
                        .type = VGFX_EVENT_MOUSE_DOWN,
                        .time_ms = timestamp,
                        .data.mouse_button = {.x = x, .y = y, .button = button}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                    break;
                }

                case NSEventTypeLeftMouseUp:
                case NSEventTypeRightMouseUp:
                case NSEventTypeOtherMouseUp:
                {
                    NSPoint location = [event locationInWindow];
                    NSRect contentRect = [platform->view bounds];

                    int32_t x = (int32_t)location.x;
                    int32_t y = (int32_t)(contentRect.size.height - location.y - 1);

                    /* Determine which button was released */
                    vgfx_mouse_button_t button = VGFX_MOUSE_LEFT;
                    if ([event type] == NSEventTypeRightMouseUp)
                    {
                        button = VGFX_MOUSE_RIGHT;
                    }
                    else if ([event type] == NSEventTypeOtherMouseUp)
                    {
                        button = VGFX_MOUSE_MIDDLE;
                    }

                    if (button < 8)
                    {
                        win->mouse_button_state[button] = 0; /* Update input state */
                    }

                    vgfx_event_t vgfx_event = {
                        .type = VGFX_EVENT_MOUSE_UP,
                        .time_ms = timestamp,
                        .data.mouse_button = {.x = x, .y = y, .button = button}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                    break;
                }

                default:
                    /* Ignore unhandled event types (scroll, gestures, etc.) */
                    break;
            }
        }

        return 1;
    }
}

/// @brief Present (blit) the framebuffer to the window.
/// @details Marks the VGFXView as needing display, then forces an immediate
///          redraw via displayIfNeeded.  This triggers the view's drawRect:
///          method, which converts the framebuffer to a CGImage and blits it
///          to the screen.
///
/// @param win Pointer to the ViperGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->pixels != NULL (framebuffer valid)
/// @pre  win->platform_data != NULL
/// @post Framebuffer contents visible on screen
int vgfx_platform_present(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    @autoreleasepool
    {
        vgfx_macos_platform *platform = (vgfx_macos_platform *)win->platform_data;

        if (!platform->view)
            return 0;

        /* Mark view as needing display (queues redraw) */
        [platform->view setNeedsDisplay:YES];

        /* Force immediate redraw (calls drawRect: synchronously) */
        [platform->view displayIfNeeded];

        return 1;
    }
}

/// @brief Get high-resolution timestamp in milliseconds.
/// @details Uses mach_absolute_time() for a monotonic high-resolution timer.
///          The epoch is arbitrary but consistent within a process.  Converts
///          mach time units to nanoseconds using the timebase, then to
///          milliseconds.
///
/// @return Milliseconds since arbitrary epoch (monotonic, never decreases)
///
/// @details mach_absolute_time():
///            - Returns CPU time base units (hardware-specific)
///            - mach_timebase_info() provides numer/denom for conversion
///            - nanoseconds = mach_time * numer / denom
///            - milliseconds = nanoseconds / 1000000
int64_t vgfx_platform_now_ms(void)
{
    static mach_timebase_info_data_t timebase;
    static int initialized = 0;

    if (!initialized)
    {
        mach_timebase_info(&timebase); /* Query conversion factors once */
        initialized = 1;
    }

    uint64_t now = mach_absolute_time();                 /* Get current time in mach units */
    uint64_t ns = now * timebase.numer / timebase.denom; /* Convert to nanoseconds */
    return (int64_t)(ns / 1000000);                      /* Convert to milliseconds */
}

/// @brief Sleep for the specified duration in milliseconds.
/// @details Uses nanosleep() for sub-second precision.  If ms <= 0, returns
///          immediately without sleeping.  Used by vgfx_update() for frame
///          rate limiting.
///
/// @param ms Duration to sleep in milliseconds
///
/// @post Thread sleeps for approximately ms milliseconds (may be longer)
void vgfx_platform_sleep_ms(int32_t ms)
{
    if (ms > 0)
    {
        struct timespec ts;
        ts.tv_sec = ms / 1000;              /* Whole seconds */
        ts.tv_nsec = (ms % 1000) * 1000000; /* Fractional seconds in nanoseconds */
        nanosleep(&ts, NULL);               /* Sleep (may be interrupted) */
    }
}

//===----------------------------------------------------------------------===//
// Clipboard Operations
//===----------------------------------------------------------------------===//

/// @brief Check if the clipboard contains data in the specified format.
/// @param format Clipboard format to check for
/// @return 1 if data is available, 0 otherwise
int vgfx_clipboard_has_format(vgfx_clipboard_format_t format)
{
    @autoreleasepool
    {
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];

        switch (format)
        {
            case VGFX_CLIPBOARD_TEXT:
                return [pasteboard availableTypeFromArray:@[ NSPasteboardTypeString ]] != nil ? 1
                                                                                              : 0;
            case VGFX_CLIPBOARD_HTML:
                return [pasteboard availableTypeFromArray:@[ NSPasteboardTypeHTML ]] != nil ? 1 : 0;
            case VGFX_CLIPBOARD_IMAGE:
                return [pasteboard
                           availableTypeFromArray:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ]] !=
                               nil
                           ? 1
                           : 0;
            case VGFX_CLIPBOARD_FILES:
                return [pasteboard availableTypeFromArray:@[ NSPasteboardTypeFileURL ]] != nil ? 1
                                                                                               : 0;
            default:
                return 0;
        }
    }
}

/// @brief Get text from the clipboard.
/// @details Returns a malloc'd UTF-8 string containing the clipboard text.
///          The caller is responsible for freeing the returned string.
/// @return Clipboard text (caller must free), or NULL if not available
char *vgfx_clipboard_get_text(void)
{
    @autoreleasepool
    {
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        NSString *string = [pasteboard stringForType:NSPasteboardTypeString];

        if (!string)
            return NULL;

        const char *utf8 = [string UTF8String];
        if (!utf8)
            return NULL;

        return strdup(utf8);
    }
}

/// @brief Set text to the clipboard.
/// @details Copies the specified UTF-8 string to the system clipboard.
/// @param text Text to copy (NULL clears text from clipboard)
void vgfx_clipboard_set_text(const char *text)
{
    @autoreleasepool
    {
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        if (text)
        {
            NSString *string = [NSString stringWithUTF8String:text];
            if (string)
            {
                [pasteboard setString:string forType:NSPasteboardTypeString];
            }
        }
    }
}

/// @brief Clear all clipboard contents.
void vgfx_clipboard_clear(void)
{
    @autoreleasepool
    {
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
    }
}

//===----------------------------------------------------------------------===//
// Window Title and Fullscreen
//===----------------------------------------------------------------------===//

/// @brief Set the window title.
/// @details Updates the NSWindow's title bar text.
///
/// @param win   Pointer to the window structure
/// @param title New title string (UTF-8)
void vgfx_platform_set_title(struct vgfx_window *win, const char *title)
{
    if (!win || !win->platform_data || !title)
        return;

    @autoreleasepool
    {
        vgfx_macos_platform *platform = (vgfx_macos_platform *)win->platform_data;

        if (platform->window)
        {
            [platform->window setTitle:[NSString stringWithUTF8String:title]];
        }
    }
}

/// @brief Set the window to fullscreen or windowed mode.
/// @details Toggles the NSWindow between fullscreen and windowed modes using
///          macOS's native fullscreen API (toggleFullScreen:).
///
/// @param win        Pointer to the window structure
/// @param fullscreen 1 for fullscreen, 0 for windowed
/// @return 1 on success, 0 on failure
int vgfx_platform_set_fullscreen(struct vgfx_window *win, int fullscreen)
{
    if (!win || !win->platform_data)
        return 0;

    @autoreleasepool
    {
        vgfx_macos_platform *platform = (vgfx_macos_platform *)win->platform_data;

        if (!platform->window)
            return 0;

        /* Check current fullscreen state */
        BOOL is_currently_fullscreen =
            ([platform->window styleMask] & NSWindowStyleMaskFullScreen) != 0;

        /* Only toggle if state needs to change */
        if (fullscreen && !is_currently_fullscreen)
        {
            [platform->window toggleFullScreen:nil];
        }
        else if (!fullscreen && is_currently_fullscreen)
        {
            [platform->window toggleFullScreen:nil];
        }

        return 1;
    }
}

/// @brief Check if the window is in fullscreen mode.
/// @param win Pointer to the window structure
/// @return 1 if fullscreen, 0 if windowed
int vgfx_platform_is_fullscreen(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    @autoreleasepool
    {
        vgfx_macos_platform *platform = (vgfx_macos_platform *)win->platform_data;

        if (!platform->window)
            return 0;

        return ([platform->window styleMask] & NSWindowStyleMaskFullScreen) != 0 ? 1 : 0;
    }
}

#endif /* __APPLE__ */

//===----------------------------------------------------------------------===//
// End of macOS Backend
//===----------------------------------------------------------------------===//
