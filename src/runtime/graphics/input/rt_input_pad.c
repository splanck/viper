//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_input_pad.c
// Purpose: Gamepad and controller input backend for Viper.Input. Manages state
//   for up to VIPER_PAD_MAX (4) simultaneously connected controllers, polling
//   button press/release/held edges, analog stick axes, and triggers each frame.
//   Provides platform-specific backends for macOS (IOKit HID), Linux (evdev),
//   and Windows (XInput), with a vibration API for force-feedback motors.
//
// Key invariants:
//   - rt_pad_poll_frame() must be called once per frame to latch pressed/released
//     edges; edges are valid only for the frame they are read.
//   - Analog stick values are in [-1.0, 1.0]; trigger values are in [0.0, 1.0].
//   - A configurable deadzone (default 0.1) is applied to stick axes before
//     returning values; inputs within the deadzone read as 0.0.
//   - Controller indices are in [0, VIPER_PAD_MAX); out-of-range indices return
//     safe zero/false values without trapping.
//   - Button codes map to the VIPER_PAD_BUTTON_* enum; axis codes to
//     VIPER_PAD_AXIS_*.
//
// Ownership/Lifetime:
//   - All state is stored in static globals (g_pads, g_pad_deadzone,
//     g_pad_initialized); no heap allocation is required for normal operation.
//   - Platform HID resources (IOKit manager on macOS) are allocated at init and
//     released by rt_pad_shutdown().
//
// Links: src/runtime/graphics/rt_input.h (keyboard/mouse layer, frame lifecycle),
//        src/runtime/graphics/rt_action.c (action mapping layer),
//        src/runtime/graphics/rt_inputmgr.h (high-level input manager)
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_input.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Gamepad/Controller Input Implementation
//=============================================================================

#include <math.h>

/// @brief State for a single gamepad
typedef struct {
    bool connected;
    char name[64];

    // Current button state
    bool buttons[VIPER_PAD_BUTTON_MAX];

    // Button events this frame
    bool pressed[VIPER_PAD_BUTTON_MAX];
    bool released[VIPER_PAD_BUTTON_MAX];

    // Analog stick values (-1.0 to 1.0)
    double left_x;
    double left_y;
    double right_x;
    double right_y;

    // Trigger values (0.0 to 1.0)
    double left_trigger;
    double right_trigger;

    // Vibration state
    double vibration_left;
    double vibration_right;
} rt_pad_state;

// Gamepad state for up to 4 controllers
static rt_pad_state g_pads[VIPER_PAD_MAX];

// Deadzone radius for analog sticks (default 0.1)
static double g_pad_deadzone = 0.1;

// Initialization flag
static bool g_pad_initialized = false;

//=============================================================================
// Platform-Specific Gamepad Backend
//=============================================================================

// ===========================================================================
// Each platform (macOS / Linux / Windows / ViperDOS) provides its own
// `platform_pad_poll` and `platform_pad_vibrate` implementations. The
// public API in `rt_pad_*` calls the platform variant via these
// forward declarations; only one definition is compiled in per build.
// ===========================================================================

/// @brief Platform-specific gamepad poll — refresh button/axis state into `g_pads[]`.
static void platform_pad_poll(void);

/// @brief Platform-specific rumble — set left/right motor strengths in [0,1].
static void platform_pad_vibrate(int64_t index, double left, double right);

#if defined(__APPLE__)
//-----------------------------------------------------------------------------
// macOS Implementation (IOKit HID Manager)
//-----------------------------------------------------------------------------

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>

typedef struct {
    IOHIDElementRef element;
    CFIndex min;
    CFIndex max;
} mac_axis;

typedef struct {
    IOHIDDeviceRef device;
    mac_axis left_x;
    mac_axis left_y;
    mac_axis right_x;
    mac_axis right_y;
    mac_axis left_trigger;
    mac_axis right_trigger;
    IOHIDElementRef hat;
    CFIndex hat_min;
    CFIndex hat_max;
    IOHIDElementRef buttons[VIPER_PAD_BUTTON_MAX];
} mac_pad;

static IOHIDManagerRef g_hid_manager = NULL;
static mac_pad g_mac_pads[VIPER_PAD_MAX];
static bool g_mac_initialized = false;

// ---------------------------------------------------------------------------
// macOS HID helpers — all of the `mac_*` functions wrap CoreFoundation /
// IOKit ref-counted objects (devices, axis elements, button elements).
// `mac_clear_pad` is the reverse of `mac_scan_devices`: it walks every
// retained element on a pad and releases it so we don't leak between
// device scans.
// ---------------------------------------------------------------------------

/// @brief Release an axis's HID element ref and zero its calibration min/max.
static void mac_release_axis(mac_axis *axis) {
    if (axis->element)
        CFRelease(axis->element);
    axis->element = NULL;
    axis->min = 0;
    axis->max = 0;
}

/// @brief Drop every IOKit ref retained for one pad slot (device, axes, hat, buttons).
/// Run before each rescan so re-plugging doesn't leak the prior device.
static void mac_clear_pad(mac_pad *pad) {
    if (pad->device)
        CFRelease(pad->device);
    pad->device = NULL;
    mac_release_axis(&pad->left_x);
    mac_release_axis(&pad->left_y);
    mac_release_axis(&pad->right_x);
    mac_release_axis(&pad->right_y);
    mac_release_axis(&pad->left_trigger);
    mac_release_axis(&pad->right_trigger);
    if (pad->hat)
        CFRelease(pad->hat);
    pad->hat = NULL;
    pad->hat_min = 0;
    pad->hat_max = 0;
    for (int i = 0; i < VIPER_PAD_BUTTON_MAX; ++i) {
        if (pad->buttons[i])
            CFRelease(pad->buttons[i]);
        pad->buttons[i] = NULL;
    }
}

/// @brief Build a HID match dictionary for a given GenericDesktop usage (Joystick / GamePad).
/// Used to constrain `IOHIDManagerSetDeviceMatchingMultiple` to gamepad-shaped devices only.
static CFMutableDictionaryRef mac_make_match(uint32_t usage) {
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int page = kHIDPage_GenericDesktop;
    CFNumberRef page_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
    CFNumberRef usage_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
    CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), page_num);
    CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), usage_num);
    CFRelease(page_num);
    CFRelease(usage_num);
    return dict;
}

/// @brief Capture an axis HID element + its calibration range for later normalisation.
static void mac_store_axis(mac_axis *axis, IOHIDElementRef element) {
    if (axis->element)
        CFRelease(axis->element);
    axis->element = element;
    CFRetain(element);
    axis->min = IOHIDElementGetLogicalMin(element);
    axis->max = IOHIDElementGetLogicalMax(element);
}

/// @brief Map a HID Button usage code (1..11) to a Viper `VIPER_PAD_*` button index.
/// Returns -1 for usages outside the standard 11-button gamepad layout.
static int mac_button_index(uint32_t usage) {
    switch (usage) {
        case 1:
            return VIPER_PAD_A;
        case 2:
            return VIPER_PAD_B;
        case 3:
            return VIPER_PAD_X;
        case 4:
            return VIPER_PAD_Y;
        case 5:
            return VIPER_PAD_LB;
        case 6:
            return VIPER_PAD_RB;
        case 7:
            return VIPER_PAD_BACK;
        case 8:
            return VIPER_PAD_START;
        case 9:
            return VIPER_PAD_LSTICK;
        case 10:
            return VIPER_PAD_RSTICK;
        case 11:
            return VIPER_PAD_GUIDE;
        default:
            return -1;
    }
}

/// @brief Enumerate matched HID gamepads and bind their elements into `g_mac_pads[]`.
///
/// Replaces the previous device list — every reattach event re-runs
/// this. For each detected device we walk its element list once and
/// classify each element by its GenericDesktop / Button HID usage:
/// X/Y → left stick, Rx/Ry → right stick, Z/Rz → triggers, Hatswitch
/// → D-pad, Slider → fallback trigger, Button N → button table.
static void mac_scan_devices(void) {
    for (int i = 0; i < VIPER_PAD_MAX; ++i) {
        mac_clear_pad(&g_mac_pads[i]);
        g_pads[i].connected = false;
        g_pads[i].name[0] = '\0';
    }

    if (!g_hid_manager)
        return;

    CFSetRef devices = IOHIDManagerCopyDevices(g_hid_manager);
    if (!devices)
        return;

    CFIndex count = CFSetGetCount(devices);
    if (count <= 0) {
        CFRelease(devices);
        return;
    }

    IOHIDDeviceRef *device_list = (IOHIDDeviceRef *)calloc((size_t)count, sizeof(IOHIDDeviceRef));
    if (!device_list) {
        CFRelease(devices);
        return;
    }
    CFSetGetValues(devices, (const void **)(void *)device_list);

    int pad_index = 0;
    for (CFIndex i = 0; i < count && pad_index < VIPER_PAD_MAX; ++i) {
        IOHIDDeviceRef device = device_list[i];
        if (!device)
            continue;

        mac_pad *pad = &g_mac_pads[pad_index];
        pad->device = device;
        CFRetain(device);

        CFTypeRef product = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
        if (product && CFGetTypeID(product) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)product,
                               g_pads[pad_index].name,
                               sizeof(g_pads[pad_index].name),
                               kCFStringEncodingUTF8);
        } else {
            snprintf(g_pads[pad_index].name,
                     sizeof(g_pads[pad_index].name),
                     "HID Gamepad %d",
                     pad_index);
        }
        g_pads[pad_index].connected = true;

        CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone);
        if (elements) {
            CFIndex elem_count = CFArrayGetCount(elements);
            for (CFIndex e = 0; e < elem_count; ++e) {
                IOHIDElementRef elem =
                    (IOHIDElementRef)(uintptr_t)CFArrayGetValueAtIndex(elements, e);
                if (!elem)
                    continue;

                IOHIDElementType type = IOHIDElementGetType(elem);
                if (type != kIOHIDElementTypeInput_Button && type != kIOHIDElementTypeInput_Misc &&
                    type != kIOHIDElementTypeInput_Axis) {
                    continue;
                }

                uint32_t page = IOHIDElementGetUsagePage(elem);
                uint32_t usage = IOHIDElementGetUsage(elem);
                if (page == kHIDPage_GenericDesktop) {
                    switch (usage) {
                        case kHIDUsage_GD_X:
                            mac_store_axis(&pad->left_x, elem);
                            break;
                        case kHIDUsage_GD_Y:
                            mac_store_axis(&pad->left_y, elem);
                            break;
                        case kHIDUsage_GD_Rx:
                            mac_store_axis(&pad->right_x, elem);
                            break;
                        case kHIDUsage_GD_Ry:
                            mac_store_axis(&pad->right_y, elem);
                            break;
                        case kHIDUsage_GD_Z:
                            mac_store_axis(&pad->left_trigger, elem);
                            break;
                        case kHIDUsage_GD_Rz:
                            mac_store_axis(&pad->right_trigger, elem);
                            break;
                        case kHIDUsage_GD_Hatswitch:
                            pad->hat = elem;
                            CFRetain(elem);
                            pad->hat_min = IOHIDElementGetLogicalMin(elem);
                            pad->hat_max = IOHIDElementGetLogicalMax(elem);
                            break;
                        case kHIDUsage_GD_Slider:
                            if (!pad->right_trigger.element)
                                mac_store_axis(&pad->right_trigger, elem);
                            else if (!pad->left_trigger.element)
                                mac_store_axis(&pad->left_trigger, elem);
                            break;
                        default:
                            break;
                    }
                } else if (page == kHIDPage_Button) {
                    int index = mac_button_index(usage);
                    if (index >= 0 && index < VIPER_PAD_BUTTON_MAX && !pad->buttons[index]) {
                        pad->buttons[index] = elem;
                        CFRetain(elem);
                    }
                }
            }
            CFRelease(elements);
        }

        pad_index++;
    }

    free(device_list);
    CFRelease(devices);
}

/// @brief Lazily create the IOHID manager and register it for the standard gamepad usages.
///
/// One-time setup. Sets up a matching dictionary array for both
/// `Joystick` and `GamePad` GenericDesktop usages so we catch
/// X-Input controllers (which advertise as `GamePad`) as well as
/// classic joysticks. After this returns, `mac_scan_devices` can
/// enumerate the manager's bound device list.
static void mac_init_manager(void) {
    if (g_mac_initialized)
        return;

    g_hid_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (g_hid_manager) {
        CFMutableDictionaryRef matches[3];
        matches[0] = mac_make_match(kHIDUsage_GD_GamePad);
        matches[1] = mac_make_match(kHIDUsage_GD_Joystick);
        matches[2] = mac_make_match(kHIDUsage_GD_MultiAxisController);

        CFArrayRef match_array = CFArrayCreate(
            kCFAllocatorDefault, (const void **)(void *)matches, 3, &kCFTypeArrayCallBacks);
        IOHIDManagerSetDeviceMatchingMultiple(g_hid_manager, match_array);
        IOHIDManagerOpen(g_hid_manager, kIOHIDOptionsTypeNone);

        CFRelease(match_array);
        for (int i = 0; i < 3; ++i)
            CFRelease(matches[i]);
    }

    mac_scan_devices();
    g_mac_initialized = true;
}

/// @brief Map a raw HID axis reading to the [-1, +1] range using device calibration.
static double mac_normalize_axis(CFIndex value, CFIndex min, CFIndex max) {
    if (max == min)
        return 0.0;
    double norm = ((double)value - (double)min) / ((double)max - (double)min);
    return norm * 2.0 - 1.0;
}

/// @brief Map a raw trigger HID reading to the [0, 1] range (triggers don't go negative).
static double mac_normalize_trigger(CFIndex value, CFIndex min, CFIndex max) {
    if (max == min)
        return 0.0;
    double norm = ((double)value - (double)min) / ((double)max - (double)min);
    if (norm < 0.0)
        norm = 0.0;
    if (norm > 1.0)
        norm = 1.0;
    return norm;
}

/// @brief Read the current integer value of one HID element.
/// Wraps `IOHIDDeviceGetValue` and copies out the integer payload.
/// @return true on success, false if the element couldn't be read.
static bool mac_read_value(IOHIDDeviceRef device, IOHIDElementRef element, CFIndex *out) {
    IOHIDValueRef value_ref = NULL;
    if (!element)
        return false;
    if (IOHIDDeviceGetValue(device, element, &value_ref) != kIOReturnSuccess || !value_ref)
        return false;
    *out = IOHIDValueGetIntegerValue(value_ref);
    return true;
}

/// @brief Translate a HID hat-switch value (0–7, 8=center) into D-pad button bits.
///
/// HID hats encode 8 directions clockwise starting from "up = 0".
/// We unpack each into the corresponding `up/down/left/right` button
/// flags so callers don't see the raw hat encoding.
static void mac_apply_hat(rt_pad_state *pad, int hat_value) {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;

    switch (hat_value) {
        case 0: // Up
            up = true;
            break;
        case 1: // Up-right
            up = true;
            right = true;
            break;
        case 2: // Right
            right = true;
            break;
        case 3: // Down-right
            down = true;
            right = true;
            break;
        case 4: // Down
            down = true;
            break;
        case 5: // Down-left
            down = true;
            left = true;
            break;
        case 6: // Left
            left = true;
            break;
        case 7: // Up-left
            up = true;
            left = true;
            break;
        default:
            break;
    }

    pad->buttons[VIPER_PAD_UP] = up;
    pad->buttons[VIPER_PAD_DOWN] = down;
    pad->buttons[VIPER_PAD_LEFT] = left;
    pad->buttons[VIPER_PAD_RIGHT] = right;
}

/// @brief macOS IOKit HID implementation of platform_pad_poll.
/// @details Lazily initializes the IOKit HID manager on first call, then scans
///   for newly connected devices if no pad is currently marked connected.
///   For each registered device slot, this reads the current value of every
///   axis element (left stick, right stick, triggers) via mac_read_value, maps
///   raw hardware ranges to [-1, +1] / [0, 1] using mac_normalize_axis and
///   mac_normalize_trigger, reads all mapped button elements, and converts any
///   hat-switch element into the four discrete D-pad button flags.
///   The results are written directly into the shared g_pads[] array so that
///   the public rt_pad_* API functions can read consistent state.
static void platform_pad_poll(void) {
    if (!g_mac_initialized)
        mac_init_manager();

    if (!g_hid_manager)
        return;

    if (!g_pads[0].connected)
        mac_scan_devices();

    for (int i = 0; i < VIPER_PAD_MAX; ++i) {
        mac_pad *pad = &g_mac_pads[i];
        if (!pad->device) {
            g_pads[i].connected = false;
            continue;
        }

        g_pads[i].connected = true;
        for (int b = 0; b < VIPER_PAD_BUTTON_MAX; ++b)
            g_pads[i].buttons[b] = false;
        g_pads[i].left_x = 0.0;
        g_pads[i].left_y = 0.0;
        g_pads[i].right_x = 0.0;
        g_pads[i].right_y = 0.0;
        g_pads[i].left_trigger = 0.0;
        g_pads[i].right_trigger = 0.0;

        CFIndex value = 0;
        if (mac_read_value(pad->device, pad->left_x.element, &value))
            g_pads[i].left_x = mac_normalize_axis(value, pad->left_x.min, pad->left_x.max);
        if (mac_read_value(pad->device, pad->left_y.element, &value))
            g_pads[i].left_y = mac_normalize_axis(value, pad->left_y.min, pad->left_y.max);
        if (mac_read_value(pad->device, pad->right_x.element, &value))
            g_pads[i].right_x = mac_normalize_axis(value, pad->right_x.min, pad->right_x.max);
        if (mac_read_value(pad->device, pad->right_y.element, &value))
            g_pads[i].right_y = mac_normalize_axis(value, pad->right_y.min, pad->right_y.max);
        if (mac_read_value(pad->device, pad->left_trigger.element, &value))
            g_pads[i].left_trigger =
                mac_normalize_trigger(value, pad->left_trigger.min, pad->left_trigger.max);
        if (mac_read_value(pad->device, pad->right_trigger.element, &value))
            g_pads[i].right_trigger =
                mac_normalize_trigger(value, pad->right_trigger.min, pad->right_trigger.max);

        for (int b = 0; b < VIPER_PAD_BUTTON_MAX; ++b) {
            if (!pad->buttons[b])
                continue;
            CFIndex btn_value = 0;
            if (mac_read_value(pad->device, pad->buttons[b], &btn_value))
                g_pads[i].buttons[b] = btn_value != 0;
        }

        if (pad->hat && mac_read_value(pad->device, pad->hat, &value)) {
            mac_apply_hat(&g_pads[i], (int)value);
        }
    }
}

/// @brief macOS IOKit HID implementation of platform_pad_vibrate — no-op.
/// @details The generic IOKit HID API does not expose force-feedback output
///   reports for gamepads.  Rumble motors are device-specific and require
///   vendor extensions (e.g. the MFi FFBDevice protocol or Sony's DualShock
///   HID reports) that are not available through the generic IOHIDManager
///   interface used by this backend.  Parameters are silenced to avoid
///   compiler warnings.
static void platform_pad_vibrate(int64_t index, double left, double right) {
    (void)index;
    (void)left;
    (void)right;
    // Vibration is not available via generic HID APIs on macOS.
}

#elif defined(__linux__)
//-----------------------------------------------------------------------------
// Linux Implementation (evdev)
//-----------------------------------------------------------------------------

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    bool has_rumble;
    int rumble_id;
    dev_t device_rdev;
    ino_t device_ino;
    int abs_min[ABS_MAX + 1];
    int abs_max[ABS_MAX + 1];
} linux_pad;

static linux_pad g_linux_pads[VIPER_PAD_MAX];
static bool g_linux_initialized = false;

// ---------------------------------------------------------------------------
// Linux evdev gamepad helpers — read raw events from /dev/input/eventN
// devices. Each `linux_*` function below is the analogue of the
// corresponding `mac_*` helper above, but for the Linux input subsystem.
// ---------------------------------------------------------------------------

/// @brief Test bit `bit` in a Linux ioctl bitset (EVIOCGBIT-shaped buffer).
static bool linux_test_bit(const unsigned long *bits, int bit) {
    return (bits[bit / (int)(8 * sizeof(unsigned long))] >>
            (bit % (int)(8 * sizeof(unsigned long)))) &
           1UL;
}

/// @brief Heuristic — does this input device look like a gamepad/joystick?
///
/// Requires both EV_KEY (button events) and EV_ABS (axes) capabilities,
/// plus at least one of the standard gamepad button keys
/// (BTN_GAMEPAD/BTN_JOYSTICK/BTN_SOUTH/BTN_NORTH). Filters out
/// keyboards, mice, and touchpads that share the /dev/input namespace.
static bool linux_is_gamepad(int fd) {
    unsigned long ev_bits[(EV_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];
    unsigned long key_bits[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];

    memset(ev_bits, 0, sizeof(ev_bits));
    memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0)
        return false;
    if (!linux_test_bit(ev_bits, EV_KEY) || !linux_test_bit(ev_bits, EV_ABS))
        return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return false;

    return linux_test_bit(key_bits, BTN_GAMEPAD) || linux_test_bit(key_bits, BTN_JOYSTICK) ||
           linux_test_bit(key_bits, BTN_SOUTH) || linux_test_bit(key_bits, BTN_NORTH);
}

/// @brief Close the device fd (if open) and reset the pad's calibration to defaults.
static void linux_reset_pad(linux_pad *pad) {
    if (pad->fd >= 0)
        close(pad->fd);
    pad->fd = -1;
    pad->has_rumble = false;
    pad->rumble_id = -1;
    pad->device_rdev = 0;
    pad->device_ino = 0;
    for (int i = 0; i <= ABS_MAX; ++i) {
        pad->abs_min[i] = -32768;
        pad->abs_max[i] = 32767;
    }
}

/// @brief Clear the public logical state for one Linux gamepad slot.
/// @details Used for initialization and disconnect handling so stale buttons, axes, triggers, and
///          names do not remain visible after the underlying evdev fd disappears.
/// @param index Slot index in @c g_pads.
static void linux_clear_logical_pad(int index) {
    if (index < 0 || index >= VIPER_PAD_MAX)
        return;
    rt_pad_state zero = {0};
    g_pads[index] = zero;
}

/// @brief Return whether an evdev device identity is already bound to a slot.
/// @param rdev Device special-file id from stat/fstat.
/// @param ino Inode from stat/fstat.
/// @return true if any open Linux pad slot already owns the same device.
static bool linux_device_already_bound(dev_t rdev, ino_t ino) {
    for (int i = 0; i < VIPER_PAD_MAX; ++i) {
        if (g_linux_pads[i].fd >= 0 && g_linux_pads[i].device_rdev == rdev &&
            g_linux_pads[i].device_ino == ino)
            return true;
    }
    return false;
}

/// @brief Find the first unbound Linux gamepad slot.
/// @return Slot index in [0, VIPER_PAD_MAX), or -1 when all slots are occupied.
static int linux_find_free_pad_slot(void) {
    for (int i = 0; i < VIPER_PAD_MAX; ++i) {
        if (g_linux_pads[i].fd < 0)
            return i;
    }
    return -1;
}

/// @brief Map a raw evdev axis reading to the [-1, +1] range using its absinfo range.
static double linux_normalize_axis(int value, int min, int max) {
    if (max == min)
        return 0.0;
    double norm = ((double)value - (double)min) / ((double)max - (double)min);
    return norm * 2.0 - 1.0;
}

/// @brief Map a raw evdev trigger reading to [0, 1] (clamped — triggers can't go negative).
static double linux_normalize_trigger(int value, int min, int max) {
    if (max == min)
        return 0.0;
    double norm = ((double)value - (double)min) / ((double)max - (double)min);
    if (norm < 0.0)
        norm = 0.0;
    if (norm > 1.0)
        norm = 1.0;
    return norm;
}

/// @brief Translate an evdev D-pad axis (`ABS_HAT0X` or `ABS_HAT0Y`) into directional buttons.
/// `is_x` selects which axis: true → left/right, false → up/down.
static void linux_apply_hat(rt_pad_state *pad, int value, bool is_x) {
    if (is_x) {
        pad->buttons[VIPER_PAD_LEFT] = value < 0;
        pad->buttons[VIPER_PAD_RIGHT] = value > 0;
    } else {
        pad->buttons[VIPER_PAD_UP] = value < 0;
        pad->buttons[VIPER_PAD_DOWN] = value > 0;
    }
}

/// @brief Scan `/dev/input/event*` and bind newly discovered gamepads into free slots.
/// @details Already-open devices are skipped by comparing their device identity. This makes the
///          scan safe to call after initialization for hotplug support without duplicating existing
///          controllers into empty slots.
static void linux_scan_gamepads(void) {
    DIR *dir = opendir("/dev/input");
    if (!dir)
        return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        int pad_index = linux_find_free_pad_slot();
        if (pad_index < 0)
            break;
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;

        char path[256];
        int written = snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;

        struct stat path_stat;
        if (stat(path, &path_stat) != 0)
            continue;
        if (linux_device_already_bound(path_stat.st_rdev, path_stat.st_ino))
            continue;

        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
            fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        struct stat fd_stat;
        if (fstat(fd, &fd_stat) != 0 ||
            linux_device_already_bound(fd_stat.st_rdev, fd_stat.st_ino)) {
            close(fd);
            continue;
        }

        if (!linux_is_gamepad(fd)) {
            close(fd);
            continue;
        }

        linux_pad *pad = &g_linux_pads[pad_index];
        linux_reset_pad(pad);
        pad->fd = fd;
        pad->device_rdev = fd_stat.st_rdev;
        pad->device_ino = fd_stat.st_ino;
        linux_clear_logical_pad(pad_index);

        char name[64];
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            name[sizeof(name) - 1u] = '\0';
            snprintf(g_pads[pad_index].name, sizeof(g_pads[pad_index].name), "%s", name);
        } else {
            snprintf(g_pads[pad_index].name,
                     sizeof(g_pads[pad_index].name),
                     "Linux Gamepad %d",
                     pad_index);
        }

        unsigned long ff_bits[(FF_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];
        memset(ff_bits, 0, sizeof(ff_bits));
        if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) >= 0 &&
            linux_test_bit(ff_bits, FF_RUMBLE)) {
            pad->has_rumble = true;
        }

        int abs_codes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
        for (size_t a = 0; a < sizeof(abs_codes) / sizeof(abs_codes[0]); ++a) {
            struct input_absinfo absinfo;
            if (ioctl(fd, EVIOCGABS(abs_codes[a]), &absinfo) == 0) {
                pad->abs_min[abs_codes[a]] = absinfo.minimum;
                pad->abs_max[abs_codes[a]] = absinfo.maximum;
            }
        }

        g_pads[pad_index].connected = true;
    }

    closedir(dir);
}

/// @brief Initialize Linux pad slots and perform the first evdev scan.
/// @details The expensive state reset happens once. Device scanning is reusable so later polls can
///          discover hotplugged controllers when slots are available.
static void linux_pad_init(void) {
    if (g_linux_initialized)
        return;

    for (int i = 0; i < VIPER_PAD_MAX; ++i) {
        g_linux_pads[i].fd = -1;
        linux_reset_pad(&g_linux_pads[i]);
        linux_clear_logical_pad(i);
    }

    linux_scan_gamepads();
    g_linux_initialized = true;
}

/// @brief Linux evdev implementation of platform_pad_poll.
/// @details Calls linux_pad_init to ensure /dev/input/event* devices have been
///   scanned and opened, and rescans free slots for hotplugged devices. For each active pad file
///   descriptor, drains the evdev
///   event queue in a non-blocking read loop: EV_KEY events update button state
///   (BTN_SOUTH/EAST/WEST/NORTH map to A/B/X/Y; shoulder, stick-click, and
///   guide buttons follow), while EV_ABS events update analog axes and triggers
///   (ABS_X/Y for the left stick, ABS_RX/RY for the right stick, ABS_Z/RZ for
///   the triggers, and ABS_HAT0X/Y for D-pad via linux_apply_hat).  If the read
///   returns ENODEV the device has been unplugged, so the pad is reset and
///   marked disconnected.
static void platform_pad_poll(void) {
    linux_pad_init();
    if (linux_find_free_pad_slot() >= 0)
        linux_scan_gamepads();
    bool need_rescan = false;

    for (int i = 0; i < VIPER_PAD_MAX; ++i) {
        linux_pad *pad = &g_linux_pads[i];
        if (pad->fd < 0) {
            linux_clear_logical_pad(i);
            continue;
        }

        g_pads[i].connected = true;

        struct input_event ev;
        ssize_t n = 0;
        while ((n = read(pad->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY) {
                bool down = ev.value != 0;
                switch (ev.code) {
                    case BTN_SOUTH:
                        g_pads[i].buttons[VIPER_PAD_A] = down;
                        break;
                    case BTN_EAST:
                        g_pads[i].buttons[VIPER_PAD_B] = down;
                        break;
                    case BTN_WEST:
                        g_pads[i].buttons[VIPER_PAD_X] = down;
                        break;
                    case BTN_NORTH:
                        g_pads[i].buttons[VIPER_PAD_Y] = down;
                        break;
                    case BTN_TL:
                        g_pads[i].buttons[VIPER_PAD_LB] = down;
                        break;
                    case BTN_TR:
                        g_pads[i].buttons[VIPER_PAD_RB] = down;
                        break;
                    case BTN_SELECT:
                        g_pads[i].buttons[VIPER_PAD_BACK] = down;
                        break;
                    case BTN_START:
                        g_pads[i].buttons[VIPER_PAD_START] = down;
                        break;
                    case BTN_THUMBL:
                        g_pads[i].buttons[VIPER_PAD_LSTICK] = down;
                        break;
                    case BTN_THUMBR:
                        g_pads[i].buttons[VIPER_PAD_RSTICK] = down;
                        break;
                    case BTN_MODE:
                        g_pads[i].buttons[VIPER_PAD_GUIDE] = down;
                        break;
                    case BTN_DPAD_UP:
                        g_pads[i].buttons[VIPER_PAD_UP] = down;
                        break;
                    case BTN_DPAD_DOWN:
                        g_pads[i].buttons[VIPER_PAD_DOWN] = down;
                        break;
                    case BTN_DPAD_LEFT:
                        g_pads[i].buttons[VIPER_PAD_LEFT] = down;
                        break;
                    case BTN_DPAD_RIGHT:
                        g_pads[i].buttons[VIPER_PAD_RIGHT] = down;
                        break;
                    default:
                        break;
                }
            } else if (ev.type == EV_ABS) {
                switch (ev.code) {
                    case ABS_X:
                        g_pads[i].left_x = linux_normalize_axis(
                            ev.value, pad->abs_min[ABS_X], pad->abs_max[ABS_X]);
                        break;
                    case ABS_Y:
                        g_pads[i].left_y = linux_normalize_axis(
                            ev.value, pad->abs_min[ABS_Y], pad->abs_max[ABS_Y]);
                        break;
                    case ABS_RX:
                        g_pads[i].right_x = linux_normalize_axis(
                            ev.value, pad->abs_min[ABS_RX], pad->abs_max[ABS_RX]);
                        break;
                    case ABS_RY:
                        g_pads[i].right_y = linux_normalize_axis(
                            ev.value, pad->abs_min[ABS_RY], pad->abs_max[ABS_RY]);
                        break;
                    case ABS_Z:
                        g_pads[i].left_trigger = linux_normalize_trigger(
                            ev.value, pad->abs_min[ABS_Z], pad->abs_max[ABS_Z]);
                        break;
                    case ABS_RZ:
                        g_pads[i].right_trigger = linux_normalize_trigger(
                            ev.value, pad->abs_min[ABS_RZ], pad->abs_max[ABS_RZ]);
                        break;
                    case ABS_HAT0X:
                        linux_apply_hat(&g_pads[i], ev.value, true);
                        break;
                    case ABS_HAT0Y:
                        linux_apply_hat(&g_pads[i], ev.value, false);
                        break;
                    default:
                        break;
                }
            }
        }

        if (n < 0 && errno == ENODEV) {
            linux_reset_pad(pad);
            linux_clear_logical_pad(i);
            need_rescan = true;
        }
    }

    if (need_rescan && linux_find_free_pad_slot() >= 0)
        linux_scan_gamepads();
}

/// @brief Linux evdev FF_RUMBLE implementation of platform_pad_vibrate.
/// @details Submits a force-feedback effect to the evdev device using the
///   kernel's FF_RUMBLE interface.  The caller-supplied [left, right] strengths
///   are clamped to [0, 1], then scaled to the 0–0xFFFF uint16 range expected
///   by ff_effect.u.rumble.strong_magnitude (low-frequency / left motor) and
///   weak_magnitude (high-frequency / right motor).  EVIOCSFF uploads the
///   effect and EV_FF/play activates it for 1000 ms.  If EVIOCSFF fails (e.g.
///   the device was hot-unplugged), has_rumble is cleared so future calls
///   short-circuit immediately.  Index bounds are validated before dereferencing
///   g_linux_pads.
static void platform_pad_vibrate(int64_t index, double left, double right) {
    if (index < 0 || index >= VIPER_PAD_MAX)
        return;
    linux_pad *pad = &g_linux_pads[index];
    if (!pad->has_rumble || pad->fd < 0)
        return;

    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = pad->rumble_id;
    double left_amp = left;
    double right_amp = right;
    if (left_amp < 0.0)
        left_amp = 0.0;
    if (left_amp > 1.0)
        left_amp = 1.0;
    if (right_amp < 0.0)
        right_amp = 0.0;
    if (right_amp > 1.0)
        right_amp = 1.0;

    effect.u.rumble.strong_magnitude = (uint16_t)(left_amp * 0xFFFF);
    effect.u.rumble.weak_magnitude = (uint16_t)(right_amp * 0xFFFF);
    effect.replay.length = 1000;
    effect.replay.delay = 0;

    if (ioctl(pad->fd, EVIOCSFF, &effect) < 0) {
        pad->has_rumble = false;
        return;
    }
    pad->rumble_id = effect.id;

    struct input_event play;
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;
    write(pad->fd, &play, sizeof(play));
}

#elif defined(_WIN32)
//-----------------------------------------------------------------------------
// Windows Implementation (XInput)
//-----------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Ensure architecture is defined before including Windows headers (MSVC C11 workaround)
#if defined(_M_AMD64) && !defined(_AMD64_)
#define _AMD64_
#elif defined(_M_IX86) && !defined(_X86_)
#define _X86_
#elif defined(_M_ARM64) && !defined(_ARM64_)
#define _ARM64_
#endif
#include <Xinput.h>
#include <windows.h>

/// @brief Windows XInput implementation of platform_pad_poll.
/// @details Calls XInputGetState for each pad slot in [0, VIPER_PAD_MAX).
///   On ERROR_SUCCESS the pad is marked connected: all digital buttons are
///   extracted from the XINPUT_GAMEPAD.wButtons bitmask, analog sticks are
///   normalized from the signed 16-bit SHORT range to [-1, +1] (using -32768
///   as the negative divisor to handle the asymmetric two's-complement range
///   correctly), and triggers are mapped from BYTE [0, 255] to [0, 1].
///   The guide button is conditionally compiled under XINPUT_GAMEPAD_GUIDE
///   since it is absent from some XInput SDK versions.  When XInputGetState
///   returns any other error the pad is marked disconnected and all state is
///   zeroed.
static void platform_pad_poll(void) {
    for (DWORD i = 0; i < VIPER_PAD_MAX; ++i) {
        XINPUT_STATE state;
        DWORD result = XInputGetState(i, &state);
        if (result == ERROR_SUCCESS) {
            g_pads[i].connected = true;
            snprintf(g_pads[i].name, sizeof(g_pads[i].name), "XInput Pad %lu", (unsigned long)i);

            WORD buttons = state.Gamepad.wButtons;
            g_pads[i].buttons[VIPER_PAD_A] = (buttons & XINPUT_GAMEPAD_A) != 0;
            g_pads[i].buttons[VIPER_PAD_B] = (buttons & XINPUT_GAMEPAD_B) != 0;
            g_pads[i].buttons[VIPER_PAD_X] = (buttons & XINPUT_GAMEPAD_X) != 0;
            g_pads[i].buttons[VIPER_PAD_Y] = (buttons & XINPUT_GAMEPAD_Y) != 0;
            g_pads[i].buttons[VIPER_PAD_LB] = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            g_pads[i].buttons[VIPER_PAD_RB] = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
            g_pads[i].buttons[VIPER_PAD_BACK] = (buttons & XINPUT_GAMEPAD_BACK) != 0;
            g_pads[i].buttons[VIPER_PAD_START] = (buttons & XINPUT_GAMEPAD_START) != 0;
            g_pads[i].buttons[VIPER_PAD_LSTICK] = (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
            g_pads[i].buttons[VIPER_PAD_RSTICK] = (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
            g_pads[i].buttons[VIPER_PAD_UP] = (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            g_pads[i].buttons[VIPER_PAD_DOWN] = (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
            g_pads[i].buttons[VIPER_PAD_LEFT] = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
            g_pads[i].buttons[VIPER_PAD_RIGHT] = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
#ifdef XINPUT_GAMEPAD_GUIDE
            g_pads[i].buttons[VIPER_PAD_GUIDE] = (buttons & XINPUT_GAMEPAD_GUIDE) != 0;
#else
            g_pads[i].buttons[VIPER_PAD_GUIDE] = false;
#endif

            SHORT lx = state.Gamepad.sThumbLX;
            SHORT ly = state.Gamepad.sThumbLY;
            SHORT rx = state.Gamepad.sThumbRX;
            SHORT ry = state.Gamepad.sThumbRY;

            g_pads[i].left_x = (lx < 0) ? (double)lx / 32768.0 : (double)lx / 32767.0;
            g_pads[i].left_y = (ly < 0) ? (double)ly / 32768.0 : (double)ly / 32767.0;
            g_pads[i].right_x = (rx < 0) ? (double)rx / 32768.0 : (double)rx / 32767.0;
            g_pads[i].right_y = (ry < 0) ? (double)ry / 32768.0 : (double)ry / 32767.0;

            g_pads[i].left_trigger = state.Gamepad.bLeftTrigger / 255.0;
            g_pads[i].right_trigger = state.Gamepad.bRightTrigger / 255.0;
        } else {
            g_pads[i].connected = false;
            g_pads[i].name[0] = '\0';
            for (int b = 0; b < VIPER_PAD_BUTTON_MAX; ++b)
                g_pads[i].buttons[b] = false;
            g_pads[i].left_x = 0.0;
            g_pads[i].left_y = 0.0;
            g_pads[i].right_x = 0.0;
            g_pads[i].right_y = 0.0;
            g_pads[i].left_trigger = 0.0;
            g_pads[i].right_trigger = 0.0;
        }
    }
}

/// @brief Windows XInput implementation of platform_pad_vibrate.
/// @details Clamps the caller-supplied [left, right] strengths to [0, 1], scales
///   them to the 0–65535 WORD range required by XINPUT_VIBRATION, and calls
///   XInputSetState to activate the motors.  The left motor is the large
///   low-frequency rumble motor; the right motor is the small high-frequency
///   vibration motor — mapping left→wLeftMotorSpeed and right→wRightMotorSpeed
///   preserves the conventional strong/weak semantics.  Index bounds are
///   validated before the XInput call.
static void platform_pad_vibrate(int64_t index, double left, double right) {
    if (index < 0 || index >= VIPER_PAD_MAX)
        return;

    double left_amp = left;
    double right_amp = right;
    if (left_amp < 0.0)
        left_amp = 0.0;
    if (left_amp > 1.0)
        left_amp = 1.0;
    if (right_amp < 0.0)
        right_amp = 0.0;
    if (right_amp > 1.0)
        right_amp = 1.0;

    XINPUT_VIBRATION vib;
    vib.wLeftMotorSpeed = (WORD)(left_amp * 65535.0);
    vib.wRightMotorSpeed = (WORD)(right_amp * 65535.0);
    XInputSetState((DWORD)index, &vib);
}

#else
//-----------------------------------------------------------------------------
// Unsupported Platform
//-----------------------------------------------------------------------------

/// @brief Fallback no-op implementation of platform_pad_poll for unsupported platforms.
/// @details Compiled when none of the recognised platform macros (__APPLE__,
///   __linux__, _WIN32) are defined.  All g_pads[] entries remain in their
///   default-initialised disconnected state; no hardware is queried.
static void platform_pad_poll(void) {
    // No gamepad support on this platform
}

/// @brief Fallback no-op implementation of platform_pad_vibrate for unsupported platforms.
/// @details Compiled when none of the recognised platform macros are defined.
///   Vibration output requires platform-specific APIs (IOKit FF, evdev FF_RUMBLE,
///   XInput) that are unavailable here.  Parameters are suppressed to avoid
///   unused-variable warnings.
static void platform_pad_vibrate(int64_t index, double left, double right) {
    (void)index;
    (void)left;
    (void)right;
}

#endif

//=============================================================================
// Deadzone Application
//=============================================================================

/// @brief Apply per-axis deadzone to stick value
static double apply_deadzone(double value) {
    if (g_pad_deadzone <= 0.0)
        return value;

    double abs_value = fabs(value);
    if (abs_value < g_pad_deadzone)
        return 0.0;

    // Rescale remaining range to 0..1
    double sign = value < 0 ? -1.0 : 1.0;
    return sign * (abs_value - g_pad_deadzone) / (1.0 - g_pad_deadzone);
}

/// @brief Clamp value to valid range
static double clamp_axis(double value, double min_val, double max_val) {
    if (value < min_val)
        return min_val;
    if (value > max_val)
        return max_val;
    return value;
}

//=============================================================================
// Initialization
//=============================================================================

/// @brief Initialize the gamepad subsystem (zeroes all pad state, sets default deadzone).
void rt_pad_init(void) {
    RT_ASSERT_MAIN_THREAD();
    if (g_pad_initialized)
        return;

    for (int i = 0; i < VIPER_PAD_MAX; i++) {
        g_pads[i].connected = false;
        g_pads[i].name[0] = '\0';

        for (int b = 0; b < VIPER_PAD_BUTTON_MAX; b++) {
            g_pads[i].buttons[b] = false;
            g_pads[i].pressed[b] = false;
            g_pads[i].released[b] = false;
        }

        g_pads[i].left_x = 0.0;
        g_pads[i].left_y = 0.0;
        g_pads[i].right_x = 0.0;
        g_pads[i].right_y = 0.0;
        g_pads[i].left_trigger = 0.0;
        g_pads[i].right_trigger = 0.0;
        g_pads[i].vibration_left = 0.0;
        g_pads[i].vibration_right = 0.0;
    }

    g_pad_deadzone = 0.1;
    g_pad_initialized = true;
}

/// @brief Clear per-frame pressed/released flags for all gamepads. Call once at frame start.
void rt_pad_begin_frame(void) {
    RT_ASSERT_MAIN_THREAD();
    // Clear per-frame event flags
    for (int i = 0; i < VIPER_PAD_MAX; i++) {
        for (int b = 0; b < VIPER_PAD_BUTTON_MAX; b++) {
            g_pads[i].pressed[b] = false;
            g_pads[i].released[b] = false;
        }
    }
}

/// @brief Poll all gamepads for current state and detect button press/release edges.
void rt_pad_poll(void) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_pad_initialized)
        rt_pad_init();

    // Store previous button states for edge detection
    bool prev_buttons[VIPER_PAD_MAX][VIPER_PAD_BUTTON_MAX];
    for (int i = 0; i < VIPER_PAD_MAX; i++) {
        for (int b = 0; b < VIPER_PAD_BUTTON_MAX; b++) {
            prev_buttons[i][b] = g_pads[i].buttons[b];
        }
    }

    // Platform-specific polling updates g_pads state
    platform_pad_poll();

    // Detect button press/release events
    for (int i = 0; i < VIPER_PAD_MAX; i++) {
        if (!g_pads[i].connected)
            continue;

        for (int b = 0; b < VIPER_PAD_BUTTON_MAX; b++) {
            bool was_down = prev_buttons[i][b];
            bool is_down = g_pads[i].buttons[b];

            if (is_down && !was_down)
                g_pads[i].pressed[b] = true;
            else if (!is_down && was_down)
                g_pads[i].released[b] = true;
        }
    }
}

//=============================================================================
// Controller Enumeration
//=============================================================================

/// @brief Get the number of currently connected gamepads.
int64_t rt_pad_count(void) {
    RT_ASSERT_MAIN_THREAD();
    int64_t count = 0;
    for (int i = 0; i < VIPER_PAD_MAX; i++) {
        if (g_pads[i].connected)
            count++;
    }
    return count;
}

/// @brief Check whether a gamepad is connected at the given slot index.
int8_t rt_pad_is_connected(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0;
    return g_pads[index].connected ? 1 : 0;
}

/// @brief Get the name of a connected gamepad (e.g., "Xbox Controller").
rt_string rt_pad_name(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX || !g_pads[index].connected)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(g_pads[index].name, strlen(g_pads[index].name));
}

//=============================================================================
// Button State (Polling)
//=============================================================================

/// @brief Check whether a gamepad button is currently held down.
int8_t rt_pad_is_down(int64_t index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0;
    if (button < 0 || button >= VIPER_PAD_BUTTON_MAX)
        return 0;
    if (!g_pads[index].connected)
        return 0;

    return g_pads[index].buttons[button] ? 1 : 0;
}

/// @brief Check whether a gamepad button is currently not held down.
int8_t rt_pad_is_up(int64_t index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 1;
    if (button < 0 || button >= VIPER_PAD_BUTTON_MAX)
        return 1;
    if (!g_pads[index].connected)
        return 1;

    return g_pads[index].buttons[button] ? 0 : 1;
}

//=============================================================================
// Button Events (Since Last Poll)
//=============================================================================

/// @brief Check whether a gamepad button was pressed this frame (edge-triggered).
int8_t rt_pad_was_pressed(int64_t index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0;
    if (button < 0 || button >= VIPER_PAD_BUTTON_MAX)
        return 0;
    if (!g_pads[index].connected)
        return 0;

    return g_pads[index].pressed[button] ? 1 : 0;
}

/// @brief Check whether a gamepad button was released this frame (edge-triggered).
int8_t rt_pad_was_released(int64_t index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0;
    if (button < 0 || button >= VIPER_PAD_BUTTON_MAX)
        return 0;
    if (!g_pads[index].connected)
        return 0;

    return g_pads[index].released[button] ? 1 : 0;
}

//=============================================================================
// Analog Inputs
//=============================================================================

/// @brief Get the left stick X axis value (-1.0 to 1.0, deadzone applied).
double rt_pad_left_x(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0.0;
    if (!g_pads[index].connected)
        return 0.0;

    return apply_deadzone(clamp_axis(g_pads[index].left_x, -1.0, 1.0));
}

/// @brief Get the left stick Y axis value (-1.0 to 1.0, deadzone applied).
double rt_pad_left_y(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0.0;
    if (!g_pads[index].connected)
        return 0.0;

    return apply_deadzone(clamp_axis(g_pads[index].left_y, -1.0, 1.0));
}

/// @brief Get the right stick X axis value (-1.0 to 1.0, deadzone applied).
double rt_pad_right_x(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0.0;
    if (!g_pads[index].connected)
        return 0.0;

    return apply_deadzone(clamp_axis(g_pads[index].right_x, -1.0, 1.0));
}

/// @brief Get the right stick Y axis value (-1.0 to 1.0, deadzone applied).
double rt_pad_right_y(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0.0;
    if (!g_pads[index].connected)
        return 0.0;

    return apply_deadzone(clamp_axis(g_pads[index].right_y, -1.0, 1.0));
}

/// @brief Get the left trigger value (0.0 = released, 1.0 = fully pressed).
double rt_pad_left_trigger(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0.0;
    if (!g_pads[index].connected)
        return 0.0;

    return clamp_axis(g_pads[index].left_trigger, 0.0, 1.0);
}

/// @brief Get the right trigger value (0.0 = released, 1.0 = fully pressed).
double rt_pad_right_trigger(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return 0.0;
    if (!g_pads[index].connected)
        return 0.0;

    return clamp_axis(g_pads[index].right_trigger, 0.0, 1.0);
}

//=============================================================================
// Deadzone Handling
//=============================================================================

/// @brief Set the analog stick deadzone radius (0.0 to 1.0, default 0.1).
void rt_pad_set_deadzone(double radius) {
    RT_ASSERT_MAIN_THREAD();
    g_pad_deadzone = clamp_axis(radius, 0.0, 1.0);
}

/// @brief Get the current analog stick deadzone radius.
double rt_pad_get_deadzone(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_pad_deadzone;
}

//=============================================================================
// Vibration/Rumble
//=============================================================================

/// @brief Set gamepad vibration motor intensities (0.0 to 1.0; platform-dependent).
void rt_pad_vibrate(int64_t index, double left_motor, double right_motor) {
    RT_ASSERT_MAIN_THREAD();
    if (index < 0 || index >= VIPER_PAD_MAX)
        return;
    if (!g_pads[index].connected)
        return;

    double left = clamp_axis(left_motor, 0.0, 1.0);
    double right = clamp_axis(right_motor, 0.0, 1.0);

    g_pads[index].vibration_left = left;
    g_pads[index].vibration_right = right;

    platform_pad_vibrate(index, left, right);
}

/// @brief Stop all vibration on a gamepad.
void rt_pad_stop_vibration(int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    rt_pad_vibrate(index, 0.0, 0.0);
}

//=============================================================================
// Button Constant Getters
//=============================================================================

// ===========================================================================
// Button-constant getters — exposed to the language layer so user code
// can write `Pad.IsButtonDown(0, Pad.ButtonA)` without hardcoding the
// internal `VIPER_PAD_*` enum values. Each is a trivial constant return.
// ===========================================================================

/// @brief Return the integer index for the A (south face) button.
int64_t rt_pad_button_a(void) {
    return VIPER_PAD_A;
}

/// @brief Return the integer index for the B (east face) button.
int64_t rt_pad_button_b(void) {
    return VIPER_PAD_B;
}

/// @brief Return the integer index for the X (west face) button.
int64_t rt_pad_button_x(void) {
    return VIPER_PAD_X;
}

/// @brief Return the integer index for the Y (north face) button.
int64_t rt_pad_button_y(void) {
    return VIPER_PAD_Y;
}

/// @brief Return the integer index for the left bumper (LB / L1) button.
int64_t rt_pad_button_lb(void) {
    return VIPER_PAD_LB;
}

/// @brief Return the integer index for the right bumper (RB / R1) button.
int64_t rt_pad_button_rb(void) {
    return VIPER_PAD_RB;
}

/// @brief Return the integer index for the Back / Select button.
int64_t rt_pad_button_back(void) {
    return VIPER_PAD_BACK;
}

/// @brief Return the integer index for the Start / Menu button.
int64_t rt_pad_button_start(void) {
    return VIPER_PAD_START;
}

/// @brief Return the integer index for the left-stick click (L3) button.
int64_t rt_pad_button_lstick(void) {
    return VIPER_PAD_LSTICK;
}

/// @brief Return the integer index for the right-stick click (R3) button.
int64_t rt_pad_button_rstick(void) {
    return VIPER_PAD_RSTICK;
}

/// @brief Return the integer index for the D-pad Up button.
int64_t rt_pad_button_up(void) {
    return VIPER_PAD_UP;
}

/// @brief Return the integer index for the D-pad Down button.
int64_t rt_pad_button_down(void) {
    return VIPER_PAD_DOWN;
}

/// @brief Return the integer index for the D-pad Left button.
int64_t rt_pad_button_left(void) {
    return VIPER_PAD_LEFT;
}

/// @brief Return the integer index for the D-pad Right button.
int64_t rt_pad_button_right(void) {
    return VIPER_PAD_RIGHT;
}

/// @brief Return the integer index for the Guide / Home / Xbox button.
int64_t rt_pad_button_guide(void) {
    return VIPER_PAD_GUIDE;
}
