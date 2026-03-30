# POLISH-13: Platform Key Translation for Non-US Keyboards

## Context
**Validated:** `rt_gui_app.c:382-419` has a hardcoded US QWERTY shift table:
```c
// Line 382: TODO: use platform-level translation.
// Lines 383-419: switch(key) { case '1': return '!'; case '2': return '@'; ... }
```

The GUI uses the custom **ViperGFX** platform layer (not SDL/GLFW).
No platform-specific key handling code exists — only the hardcoded switch.

**Complexity: S** | **Priority: P3**

## Design

### macOS: UCKeyTranslate (via Carbon framework)

Replace the switch table with a call to `UCKeyTranslate`:

```objc
#include <Carbon/Carbon.h>

static UniChar translateKeyWithModifiers(uint16_t keyCode, UInt32 modifiers) {
    TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
    CFDataRef layoutData = TISGetInputSourceProperty(source,
                                kTISPropertyUnicodeKeyLayoutData);
    if (!layoutData) { CFRelease(source); return 0; }

    const UCKeyboardLayout *layout =
        (const UCKeyboardLayout *)CFDataGetBytePtr(layoutData);
    UniChar chars[4];
    UniCharCount length = 0;
    UInt32 deadKeyState = 0;

    UCKeyTranslate(layout, keyCode, kUCKeyActionDown, modifiers >> 8,
                   LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                   &deadKeyState, 4, &length, chars);
    CFRelease(source);
    return length > 0 ? chars[0] : 0;
}
```

### Linux: XKB (via xkbcommon library)

```c
#include <xkbcommon/xkbcommon.h>
// Use xkb_state_key_get_utf32() with current keymap
```

### Windows: ToUnicode

```c
wchar_t buf[4];
BYTE keyState[256];
GetKeyboardState(keyState);
int result = ToUnicode(virtualKey, scanCode, keyState, buf, 4, 0);
```

### Integration Point

In `rt_gui_app.c`, replace the `switch(key)` at lines 383-419 with:
```c
#if defined(__APPLE__)
    return translateKeyWithModifiers(keyCode, shiftModifier);
#elif defined(__linux__)
    return xkb_translate(keyCode, shiftModifier);
#elif defined(_WIN32)
    return win32_translate(keyCode, shiftModifier);
#else
    // Fallback to US QWERTY table
    return us_qwerty_shift(key);
#endif
```

### Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_gui_app.c:382-419` | Replace hardcoded table with platform calls |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/viperlib/gui/README.md` — Note keyboard i18n support

## Verification
Switch macOS keyboard layout to German (QWERTZ) or French (AZERTY).
Type shifted characters in GUI text input. Verify correct output.
