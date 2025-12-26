# ViperOS Phase 5: Input & Polish

## Detailed Implementation Plan (C++)

**Duration:** 12 weeks (Months 13-15)  
**Goal:** Complete interactive shell experience  
**Milestone:** Full keyboard input, line editing, command history, tab completion  
**Prerequisites:** Phase 4 complete (filesystem, shell, commands)

---

## Executive Summary

Phase 5 transforms ViperOS from a functional system into a pleasant one. The shell gains real keyboard input with line
editing, history navigation, and tab completion. We add a loadable font system, more utilities, and polish the overall
user experience.

Key components:

1. **virtio-input Driver** — Keyboard and mouse event handling
2. **Input Subsystem** — Event queue, key translation, poll integration
3. **Console Input** — Line-oriented input with editing
4. **Line Editor** — Cursor movement, insert/delete, cut/paste
5. **Command History** — Up/down navigation, search
6. **Tab Completion** — Path and command completion
7. **Font System** — Loadable bitmap fonts (.vfont)
8. **Additional Commands** — Search, Sort, Status, Avail, etc.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│                          User Space                                 │
├────────────────────────────────────────────────────────────────────┤
│   vsh Shell                                                        │
│   ┌──────────────────────────────────────────────────────────────┐│
│   │ LineEditor                                                    ││
│   │ ┌────────────────────────────────────────────────────────┐   ││
│   │ │ Buffer: "Dir SYS:c█"                                    │   ││
│   │ │ Cursor: 9                                               │   ││
│   │ │ History: ["Dir", "List", "Type readme.txt"]            │   ││
│   │ └────────────────────────────────────────────────────────┘   ││
│   │                                                               ││
│   │ TabCompleter                                                  ││
│   │ ┌────────────────────────────────────────────────────────┐   ││
│   │ │ Matches: ["system.vcfg", "startup.bas"]                │   ││
│   │ └────────────────────────────────────────────────────────┘   ││
│   └──────────────────────────────────────────────────────────────┘│
├────────────────────────────────────────────────────────────────────┤
│                          Syscalls                                   │
│   InputGetHandle  InputPoll  SurfaceGetBuffer                      │
├────────────────────────────────────────────────────────────────────┤
│                          Kernel                                     │
├────────────────────────────────────────────────────────────────────┤
│   Input Subsystem                                                  │
│   ┌──────────────────────────────────────────────────────────────┐│
│   │ Event Queue ──► Key Translation ──► Pollable Handle          ││
│   └──────────────────────────────────────────────────────────────┘│
│                              │                                      │
│   virtio-input Driver        │                                      │
│   ┌──────────────────────────┴────────────────────────────────────┐│
│   │ Keyboard Events    Mouse Events                               ││
│   │ HID Codes ───────► VInputEvent                                ││
│   └───────────────────────────────────────────────────────────────┘│
│                              │                                      │
│   Font Manager                                                      │
│   ┌───────────────────────────────────────────────────────────────┐│
│   │ LoadFont  GetGlyph  RenderText                                ││
│   └───────────────────────────────────────────────────────────────┘│
└────────────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
kernel/
├── drivers/
│   └── virtio/
│       ├── input.cpp/.hpp          # virtio-input driver
│       └── input_codes.hpp         # HID to ViperOS key mapping
├── input/
│   ├── input.cpp/.hpp              # Input subsystem
│   ├── keyboard.cpp/.hpp           # Keyboard state, modifiers
│   ├── mouse.cpp/.hpp              # Mouse state, buttons
│   └── event.hpp                   # VInputEvent definition
├── console/
│   ├── console_input.cpp/.hpp      # Console input handling
│   └── readline.cpp/.hpp           # Line reading with echo
├── font/
│   ├── font.cpp/.hpp               # Font manager
│   ├── bitmap_font.cpp/.hpp        # Bitmap font renderer
│   └── vfont.hpp                   # .vfont file format
└── syscall/
    └── input_syscalls.cpp          # Input syscalls

user/
├── vsh/
│   ├── line_editor.cpp/.hpp        # Line editing
│   ├── history.cpp/.hpp            # Command history
│   ├── completion.cpp/.hpp         # Tab completion
│   └── keybindings.cpp/.hpp        # Key bindings
├── cmd/
│   ├── search.cpp                  # Search command
│   ├── sort.cpp                    # Sort command
│   ├── status.cpp                  # Status command
│   ├── avail.cpp                   # Avail command
│   ├── break.cpp                   # Break command
│   ├── wait.cpp                    # Wait command
│   ├── date.cpp                    # Date command
│   ├── time.cpp                    # Time command
│   ├── info.cpp                    # Info command
│   ├── protect.cpp                 # Protect command
│   ├── run.cpp                     # Run (background) command
│   └── execute.cpp                 # Execute script command
└── lib/
    ├── vinput.cpp/.hpp             # Input library
    └── vfont.cpp/.hpp              # Font library
```

---

## Milestones

| # | Milestone            | Duration   | Deliverable             |
|---|----------------------|------------|-------------------------|
| 1 | virtio-input Driver  | Week 1-2   | Keyboard/mouse events   |
| 2 | Input Subsystem      | Week 3     | Event queue, polling    |
| 3 | Key Translation      | Week 4     | HID to ASCII, modifiers |
| 4 | Console Input        | Week 5     | Basic character input   |
| 5 | Line Editor          | Week 6-7   | Cursor, insert, delete  |
| 6 | History & Completion | Week 8-9   | Up/down, tab            |
| 7 | Font System          | Week 10    | Loadable fonts          |
| 8 | Commands & Polish    | Week 11-12 | Additional utilities    |

---

## Milestone 1: virtio-input Driver

**Duration:** Weeks 1-2  
**Deliverable:** Receive keyboard and mouse events from QEMU

### 1.1 virtio-input Device

```cpp
// kernel/drivers/virtio/input.hpp
#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"
#include "../../input/event.hpp"

namespace viper::virtio {

// virtio-input config select values
constexpr u8 VIRTIO_INPUT_CFG_UNSET    = 0x00;
constexpr u8 VIRTIO_INPUT_CFG_ID_NAME  = 0x01;
constexpr u8 VIRTIO_INPUT_CFG_ID_SERIAL = 0x02;
constexpr u8 VIRTIO_INPUT_CFG_ID_DEVIDS = 0x03;
constexpr u8 VIRTIO_INPUT_CFG_PROP_BITS = 0x10;
constexpr u8 VIRTIO_INPUT_CFG_EV_BITS  = 0x11;
constexpr u8 VIRTIO_INPUT_CFG_ABS_INFO = 0x12;

// Event types (Linux evdev compatible)
constexpr u16 EV_SYN = 0x00;
constexpr u16 EV_KEY = 0x01;
constexpr u16 EV_REL = 0x02;
constexpr u16 EV_ABS = 0x03;

// Relative axes
constexpr u16 REL_X = 0x00;
constexpr u16 REL_Y = 0x01;
constexpr u16 REL_WHEEL = 0x08;

// virtio-input event (from device)
struct VirtioInputEvent {
    u16 type;
    u16 code;
    i32 value;
};

class InputDevice : public Device {
public:
    enum class Type { Keyboard, Mouse, Unknown };
    
    bool init(VirtAddr base);
    
    // Poll for events (non-blocking)
    // Returns number of events read
    int poll(input::InputEvent* events, int max_events);
    
    Type type() const { return type_; }
    const char* name() const { return name_; }
    
private:
    Type type_ = Type::Unknown;
    char name_[64] = {};
    
    Virtqueue event_vq_;   // Events from device
    Virtqueue status_vq_;  // Status to device (LED, etc.)
    
    // Pre-allocated event buffers
    static constexpr int EVENT_BUFFER_COUNT = 64;
    VirtioInputEvent event_buffers_[EVENT_BUFFER_COUNT];
    PhysAddr event_buffers_phys_;
    
    void queue_event_buffers();
    void read_config_string(u8 select, u8 subsel, char* buf, int len);
};

// Device discovery
void input_probe();
InputDevice* keyboard_device();
InputDevice* mouse_device();

} // namespace viper::virtio
```

### 1.2 virtio-input Implementation

```cpp
// kernel/drivers/virtio/input.cpp
#include "input.hpp"
#include "../../lib/format.hpp"
#include "../../lib/string.hpp"
#include "../../mm/pmm.hpp"

namespace viper::virtio {

namespace {
    InputDevice kbd_dev;
    InputDevice mouse_dev;
    bool kbd_found = false;
    bool mouse_found = false;
}

InputDevice* keyboard_device() { return kbd_found ? &kbd_dev : nullptr; }
InputDevice* mouse_device() { return mouse_found ? &mouse_dev : nullptr; }

bool InputDevice::init(VirtAddr base) {
    if (!Device::init(base)) return false;
    
    if (device_id_ != VIRTIO_DEV_INPUT) {
        return false;
    }
    
    // Reset device
    write32(VIRTIO_MMIO_STATUS, 0);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(get_status() | VIRTIO_STATUS_DRIVER);
    
    // Read device name
    read_config_string(VIRTIO_INPUT_CFG_ID_NAME, 0, name_, sizeof(name_));
    
    // Determine device type from name
    if (strstr(name_, "keyboard") || strstr(name_, "Keyboard")) {
        type_ = Type::Keyboard;
    } else if (strstr(name_, "mouse") || strstr(name_, "Mouse") ||
               strstr(name_, "pointer") || strstr(name_, "Pointer")) {
        type_ = Type::Mouse;
    }
    
    kprintf("virtio-input: Found '%s' (type=%d)\n", name_, (int)type_);
    
    // Negotiate features
    if (!negotiate_features(0, 0)) {
        return false;
    }
    
    // Initialize event virtqueue
    if (!event_vq_.init(this, 0, 64)) {
        kprintf("virtio-input: Failed to init event queue\n");
        return false;
    }
    
    // Initialize status virtqueue (optional)
    status_vq_.init(this, 1, 8);
    
    // Allocate event buffers
    auto buf_result = pmm::alloc_page();
    if (!buf_result.is_ok()) return false;
    event_buffers_phys_ = buf_result.unwrap();
    
    // Queue event buffers for device to fill
    queue_event_buffers();
    
    // Driver ready
    set_status(get_status() | VIRTIO_STATUS_DRIVER_OK);
    
    return true;
}

void InputDevice::read_config_string(u8 select, u8 subsel, char* buf, int len) {
    // Write config select
    volatile u8* config = reinterpret_cast<volatile u8*>(base_.raw() + VIRTIO_MMIO_CONFIG);
    config[0] = select;
    config[1] = subsel;
    
    // Memory barrier
    asm volatile("dmb sy" ::: "memory");
    
    // Read size
    u8 size = config[2];
    if (size > len - 1) size = len - 1;
    
    // Read string
    for (int i = 0; i < size; i++) {
        buf[i] = config[8 + i];
    }
    buf[size] = '\0';
}

void InputDevice::queue_event_buffers() {
    for (int i = 0; i < EVENT_BUFFER_COUNT; i++) {
        i32 desc = event_vq_.alloc_desc();
        if (desc < 0) break;
        
        PhysAddr buf_addr = event_buffers_phys_ + i * sizeof(VirtioInputEvent);
        event_vq_.set_desc(desc, buf_addr, sizeof(VirtioInputEvent),
                          VRING_DESC_F_WRITE, 0);
        event_vq_.submit(desc);
    }
    event_vq_.kick();
}

int InputDevice::poll(input::InputEvent* events, int max_events) {
    int count = 0;
    
    while (count < max_events) {
        i32 desc = event_vq_.poll_used();
        if (desc < 0) break;
        
        // Get event from buffer
        int buf_idx = desc % EVENT_BUFFER_COUNT;
        VirtioInputEvent& ve = event_buffers_[buf_idx];
        
        // Copy to kernel via HHDM
        VirtioInputEvent* phys_buf = pmm::phys_to_virt(
            event_buffers_phys_ + buf_idx * sizeof(VirtioInputEvent)
        ).as_ptr<VirtioInputEvent>();
        
        // Convert to VInputEvent
        input::InputEvent& e = events[count];
        e.timestamp = timer::get_ns();
        
        if (phys_buf->type == EV_KEY) {
            e.type = input::EVENT_KEY;
            e.code = phys_buf->code;
            e.value = phys_buf->value;  // 0=release, 1=press, 2=repeat
            count++;
        } else if (phys_buf->type == EV_REL) {
            e.type = input::EVENT_MOUSE_MOVE;
            e.code = phys_buf->code;  // REL_X, REL_Y, REL_WHEEL
            e.value = phys_buf->value;
            count++;
        }
        
        // Re-queue buffer
        event_vq_.set_desc(desc, 
                          event_buffers_phys_ + buf_idx * sizeof(VirtioInputEvent),
                          sizeof(VirtioInputEvent), VRING_DESC_F_WRITE, 0);
        event_vq_.submit(desc);
    }
    
    if (count > 0) {
        event_vq_.kick();
    }
    
    return count;
}

void input_probe() {
    // Scan for virtio-input devices
    for (u64 addr = 0x0a000000; addr < 0x0a004000; addr += 0x200) {
        VirtAddr va = pmm::phys_to_virt(PhysAddr{addr});
        
        u32 magic = *reinterpret_cast<volatile u32*>(va.raw());
        if (magic != VIRTIO_MAGIC) continue;
        
        u32 dev_id = *reinterpret_cast<volatile u32*>(va.raw() + 8);
        if (dev_id != VIRTIO_DEV_INPUT) continue;
        
        // Try to initialize
        InputDevice temp;
        if (!temp.init(va)) continue;
        
        // Store based on type
        if (temp.type() == InputDevice::Type::Keyboard && !kbd_found) {
            kbd_dev = temp;
            kbd_found = true;
            kprintf("virtio-input: Keyboard at 0x%lx\n", addr);
        } else if (temp.type() == InputDevice::Type::Mouse && !mouse_found) {
            mouse_dev = temp;
            mouse_found = true;
            kprintf("virtio-input: Mouse at 0x%lx\n", addr);
        }
    }
}

} // namespace viper::virtio
```

---

## Milestone 2: Input Subsystem

**Duration:** Week 3  
**Deliverable:** Unified input event queue, pollable handles

### 2.1 Input Event Definition

```cpp
// kernel/input/event.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::input {

// Event types
constexpr u32 EVENT_NONE       = 0;
constexpr u32 EVENT_KEY        = 1;
constexpr u32 EVENT_MOUSE_MOVE = 2;
constexpr u32 EVENT_MOUSE_BTN  = 3;

// Key states
constexpr i32 KEY_RELEASED = 0;
constexpr i32 KEY_PRESSED  = 1;
constexpr i32 KEY_REPEAT   = 2;

// Mouse buttons
constexpr u32 MOUSE_BTN_LEFT   = 0x110;
constexpr u32 MOUSE_BTN_RIGHT  = 0x111;
constexpr u32 MOUSE_BTN_MIDDLE = 0x112;

struct InputEvent {
    u32 type;
    u32 code;
    i32 value;
    u32 _pad;
    u64 timestamp;
};
static_assert(sizeof(InputEvent) == 24);

} // namespace viper::input
```

### 2.2 Input Subsystem

```cpp
// kernel/input/input.hpp
#pragma once

#include "event.hpp"
#include "../sync/wait_queue.hpp"

namespace viper::input {

// Ring buffer for input events
class EventQueue {
public:
    static constexpr usize CAPACITY = 256;
    
    bool push(const InputEvent& event);
    bool pop(InputEvent& event);
    bool empty() const { return head_ == tail_; }
    usize count() const;
    
    // For polling
    sync::WaitQueue& wait_queue() { return waiters_; }
    
private:
    InputEvent events_[CAPACITY];
    volatile usize head_ = 0;
    volatile usize tail_ = 0;
    sync::WaitQueue waiters_;
};

// Input device handle (for user space)
struct InputHandle {
    enum class Type { Keyboard, Mouse };
    Type type;
    EventQueue* queue;
};

// Global input state
void init();
void poll_devices();  // Called from timer interrupt

// Keyboard state
bool key_pressed(u32 code);
u32 modifiers();  // Shift, Ctrl, Alt flags

// Create handles for user space
InputHandle* create_keyboard_handle();
InputHandle* create_mouse_handle();
void destroy_handle(InputHandle* h);

// Poll events (for syscall)
int poll_events(InputHandle* h, InputEvent* events, int max);

} // namespace viper::input
```

### 2.3 Input Subsystem Implementation

```cpp
// kernel/input/input.cpp
#include "input.hpp"
#include "keyboard.hpp"
#include "../drivers/virtio/input.hpp"
#include "../lib/format.hpp"

namespace viper::input {

namespace {
    EventQueue keyboard_queue;
    EventQueue mouse_queue;
    
    // Keyboard state
    bool key_state[256] = {};
    u32 current_modifiers = 0;
}

bool EventQueue::push(const InputEvent& event) {
    usize next_tail = (tail_ + 1) % CAPACITY;
    if (next_tail == head_) return false;  // Full
    
    events_[tail_] = event;
    tail_ = next_tail;
    
    // Wake any waiters
    waiters_.wake_one();
    
    return true;
}

bool EventQueue::pop(InputEvent& event) {
    if (head_ == tail_) return false;  // Empty
    
    event = events_[head_];
    head_ = (head_ + 1) % CAPACITY;
    return true;
}

usize EventQueue::count() const {
    if (tail_ >= head_) return tail_ - head_;
    return CAPACITY - head_ + tail_;
}

void init() {
    virtio::input_probe();
    kprintf("Input: Subsystem initialized\n");
}

void poll_devices() {
    // Poll keyboard
    if (auto* kbd = virtio::keyboard_device()) {
        InputEvent events[16];
        int n = kbd->poll(events, 16);
        
        for (int i = 0; i < n; i++) {
            // Update key state
            if (events[i].type == EVENT_KEY) {
                keyboard::process_key(events[i]);
            }
            keyboard_queue.push(events[i]);
        }
    }
    
    // Poll mouse
    if (auto* mouse = virtio::mouse_device()) {
        InputEvent events[16];
        int n = mouse->poll(events, 16);
        
        for (int i = 0; i < n; i++) {
            mouse_queue.push(events[i]);
        }
    }
}

bool key_pressed(u32 code) {
    if (code < 256) return key_state[code];
    return false;
}

u32 modifiers() {
    return current_modifiers;
}

InputHandle* create_keyboard_handle() {
    auto* h = new InputHandle;
    h->type = InputHandle::Type::Keyboard;
    h->queue = &keyboard_queue;
    return h;
}

InputHandle* create_mouse_handle() {
    auto* h = new InputHandle;
    h->type = InputHandle::Type::Mouse;
    h->queue = &mouse_queue;
    return h;
}

void destroy_handle(InputHandle* h) {
    delete h;
}

int poll_events(InputHandle* h, InputEvent* events, int max) {
    if (!h || !h->queue) return 0;
    
    int count = 0;
    while (count < max) {
        if (!h->queue->pop(events[count])) break;
        count++;
    }
    return count;
}

} // namespace viper::input
```

---

## Milestone 3: Key Translation

**Duration:** Week 4  
**Deliverable:** HID codes to ASCII, modifier handling

### 3.1 Key Codes

```cpp
// kernel/input/keycodes.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::input {

// USB HID key codes (subset)
namespace Key {
    constexpr u32 A = 0x04;
    constexpr u32 B = 0x05;
    constexpr u32 C = 0x06;
    // ... through Z = 0x1D
    
    constexpr u32 N1 = 0x1E;  // 1 and !
    constexpr u32 N2 = 0x1F;  // 2 and @
    // ... through N0 = 0x27
    
    constexpr u32 Enter = 0x28;
    constexpr u32 Escape = 0x29;
    constexpr u32 Backspace = 0x2A;
    constexpr u32 Tab = 0x2B;
    constexpr u32 Space = 0x2C;
    
    constexpr u32 Minus = 0x2D;      // - and _
    constexpr u32 Equal = 0x2E;      // = and +
    constexpr u32 LeftBracket = 0x2F;
    constexpr u32 RightBracket = 0x30;
    constexpr u32 Backslash = 0x31;
    constexpr u32 Semicolon = 0x33;
    constexpr u32 Quote = 0x34;
    constexpr u32 Grave = 0x35;      // ` and ~
    constexpr u32 Comma = 0x36;
    constexpr u32 Period = 0x37;
    constexpr u32 Slash = 0x38;
    
    constexpr u32 CapsLock = 0x39;
    
    constexpr u32 F1 = 0x3A;
    // ... through F12 = 0x45
    
    constexpr u32 Insert = 0x49;
    constexpr u32 Home = 0x4A;
    constexpr u32 PageUp = 0x4B;
    constexpr u32 Delete = 0x4C;
    constexpr u32 End = 0x4D;
    constexpr u32 PageDown = 0x4E;
    constexpr u32 Right = 0x4F;
    constexpr u32 Left = 0x50;
    constexpr u32 Down = 0x51;
    constexpr u32 Up = 0x52;
    
    constexpr u32 LeftCtrl = 0xE0;
    constexpr u32 LeftShift = 0xE1;
    constexpr u32 LeftAlt = 0xE2;
    constexpr u32 LeftMeta = 0xE3;
    constexpr u32 RightCtrl = 0xE4;
    constexpr u32 RightShift = 0xE5;
    constexpr u32 RightAlt = 0xE6;
    constexpr u32 RightMeta = 0xE7;
}

// Modifier flags
constexpr u32 MOD_SHIFT = 1 << 0;
constexpr u32 MOD_CTRL  = 1 << 1;
constexpr u32 MOD_ALT   = 1 << 2;
constexpr u32 MOD_META  = 1 << 3;
constexpr u32 MOD_CAPS  = 1 << 4;

} // namespace viper::input
```

### 3.2 Keyboard Processing

```cpp
// kernel/input/keyboard.hpp
#pragma once

#include "event.hpp"
#include "keycodes.hpp"

namespace viper::input::keyboard {

// Process a key event, update modifier state
void process_key(const InputEvent& event);

// Translate key code to ASCII character
// Returns 0 if not a printable character
char to_ascii(u32 code, u32 modifiers);

// Get current modifier state
u32 get_modifiers();

// Special key checks
bool is_modifier(u32 code);
bool is_printable(u32 code);

} // namespace viper::input::keyboard
```

```cpp
// kernel/input/keyboard.cpp
#include "keyboard.hpp"

namespace viper::input::keyboard {

namespace {
    u32 current_mods = 0;
    bool caps_lock = false;
    
    // US keyboard layout - lowercase
    const char ascii_table[] = {
        0, 0, 0, 0,                     // 0x00-0x03
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '\n', 0, '\b', '\t', ' ',       // Enter, Esc, Backspace, Tab, Space
        '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/',
    };
    
    // Shifted characters
    const char shift_table[] = {
        0, 0, 0, 0,
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
        '\n', 0, '\b', '\t', ' ',
        '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?',
    };
}

void process_key(const InputEvent& event) {
    if (event.type != EVENT_KEY) return;
    
    bool pressed = (event.value != KEY_RELEASED);
    
    // Update modifier state
    switch (event.code) {
        case Key::LeftShift:
        case Key::RightShift:
            if (pressed) current_mods |= MOD_SHIFT;
            else current_mods &= ~MOD_SHIFT;
            break;
        case Key::LeftCtrl:
        case Key::RightCtrl:
            if (pressed) current_mods |= MOD_CTRL;
            else current_mods &= ~MOD_CTRL;
            break;
        case Key::LeftAlt:
        case Key::RightAlt:
            if (pressed) current_mods |= MOD_ALT;
            else current_mods &= ~MOD_ALT;
            break;
        case Key::CapsLock:
            if (pressed) {
                caps_lock = !caps_lock;
                if (caps_lock) current_mods |= MOD_CAPS;
                else current_mods &= ~MOD_CAPS;
            }
            break;
    }
}

char to_ascii(u32 code, u32 modifiers) {
    if (code >= sizeof(ascii_table)) return 0;
    
    bool shift = (modifiers & MOD_SHIFT) != 0;
    bool caps = (modifiers & MOD_CAPS) != 0;
    
    // Letters: caps XOR shift
    if (code >= Key::A && code <= Key::A + 25) {
        if (shift ^ caps) {
            return shift_table[code];
        } else {
            return ascii_table[code];
        }
    }
    
    // Other keys: just shift
    if (shift) {
        return shift_table[code];
    }
    return ascii_table[code];
}

u32 get_modifiers() {
    return current_mods;
}

bool is_modifier(u32 code) {
    return code >= Key::LeftCtrl && code <= Key::RightMeta;
}

bool is_printable(u32 code) {
    if (code >= sizeof(ascii_table)) return false;
    return ascii_table[code] != 0;
}

} // namespace viper::input::keyboard
```

---

## Milestone 4: Console Input

**Duration:** Week 5  
**Deliverable:** Character-by-character input to console

### 4.1 Console Input

```cpp
// kernel/console/console_input.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::console {

// Initialize console input
void input_init();

// Read a single character (blocking)
char getchar();

// Read a single character (non-blocking, returns -1 if none)
int getchar_nonblock();

// Check if input available
bool input_available();

// Read a line with echo (blocking)
int readline(char* buf, int max_len);

} // namespace viper::console
```

### 4.2 Input Syscalls

```cpp
// kernel/syscall/input_syscalls.cpp
#include "dispatch.hpp"
#include "../input/input.hpp"
#include "../viper/viper.hpp"
#include "../cap/table.hpp"

namespace viper::syscall {

// InputGetHandle(device_type) -> handle
// device_type: 0=keyboard, 1=mouse
SyscallResult sys_input_get_handle(proc::Viper* v, u32 device_type) {
    input::InputHandle* h = nullptr;
    
    if (device_type == 0) {
        h = input::create_keyboard_handle();
    } else if (device_type == 1) {
        h = input::create_mouse_handle();
    } else {
        return {VERR_INVALID_ARG, 0, 0, 0};
    }
    
    if (!h) {
        return {VERR_NOT_FOUND, 0, 0, 0};
    }
    
    auto handle = v->cap_table->insert(h, cap::Kind::Input, cap::CAP_READ);
    if (!handle.is_ok()) {
        input::destroy_handle(h);
        return {handle.get_error(), 0, 0, 0};
    }
    
    return {VOK, handle.unwrap(), 0, 0};
}

// InputPoll(handle, events_buf, max_events) -> num_events
SyscallResult sys_input_poll(proc::Viper* v, u32 h, u64 buf, u64 max) {
    auto* entry = v->cap_table->get_checked(h, cap::Kind::Input);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* input_h = static_cast<input::InputHandle*>(entry->object);
    
    int n = input::poll_events(input_h, 
                               reinterpret_cast<input::InputEvent*>(buf),
                               max);
    
    return {VOK, static_cast<u64>(n), 0, 0};
}

} // namespace viper::syscall
```

---

## Milestone 5: Line Editor

**Duration:** Weeks 6-7  
**Deliverable:** Full line editing with cursor movement

### 5.1 Line Editor (User Space)

```cpp
// user/vsh/line_editor.hpp
#pragma once

#include <stdint.h>

namespace editor {

class LineEditor {
public:
    static constexpr int MAX_LINE = 1024;
    
    LineEditor();
    
    // Process a key event, returns true if line complete (Enter pressed)
    bool process_key(uint32_t code, uint32_t modifiers);
    
    // Get the current line
    const char* line() const { return buffer_; }
    int length() const { return len_; }
    
    // Reset for new input
    void reset();
    
    // Set initial content (for history recall)
    void set_line(const char* text);
    
    // Cursor position
    int cursor() const { return cursor_; }
    
private:
    char buffer_[MAX_LINE];
    int len_ = 0;
    int cursor_ = 0;
    
    // Editing operations
    void insert_char(char c);
    void delete_char();       // Delete at cursor
    void backspace();         // Delete before cursor
    void move_left();
    void move_right();
    void move_home();
    void move_end();
    void move_word_left();
    void move_word_right();
    void kill_to_end();       // Ctrl+K
    void kill_to_start();     // Ctrl+U
    void clear_line();
    
    // Redraw
    void redraw();
    void redraw_from_cursor();
};

} // namespace editor
```

### 5.2 Line Editor Implementation

```cpp
// user/vsh/line_editor.cpp
#include "line_editor.hpp"
#include "../lib/vsys.hpp"
#include "../lib/vinput.hpp"

namespace editor {

namespace {
    // ANSI escape sequences for cursor control
    void move_cursor_left(int n) {
        if (n <= 0) return;
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "\x1b[%dD", n);
        vsys::debug_print(buf, len);
    }
    
    void move_cursor_right(int n) {
        if (n <= 0) return;
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "\x1b[%dC", n);
        vsys::debug_print(buf, len);
    }
    
    void clear_to_eol() {
        vsys::debug_print("\x1b[K", 3);
    }
}

LineEditor::LineEditor() {
    reset();
}

void LineEditor::reset() {
    buffer_[0] = '\0';
    len_ = 0;
    cursor_ = 0;
}

void LineEditor::set_line(const char* text) {
    int i = 0;
    while (text[i] && i < MAX_LINE - 1) {
        buffer_[i] = text[i];
        i++;
    }
    buffer_[i] = '\0';
    len_ = i;
    cursor_ = i;
}

bool LineEditor::process_key(uint32_t code, uint32_t modifiers) {
    using namespace vinput;
    
    bool ctrl = (modifiers & MOD_CTRL) != 0;
    
    // Control keys
    if (ctrl) {
        switch (code) {
            case Key::A: move_home(); return false;
            case Key::E: move_end(); return false;
            case Key::B: move_left(); return false;
            case Key::F: move_right(); return false;
            case Key::D: delete_char(); return false;
            case Key::H: backspace(); return false;
            case Key::K: kill_to_end(); return false;
            case Key::U: kill_to_start(); return false;
            case Key::W: /* delete word */ return false;
            case Key::C: /* interrupt */ return true;  // Signal interrupt
        }
    }
    
    // Special keys
    switch (code) {
        case Key::Enter:
            vsys::debug_print("\n", 1);
            return true;
            
        case Key::Backspace:
            backspace();
            return false;
            
        case Key::Delete:
            delete_char();
            return false;
            
        case Key::Left:
            if (ctrl) move_word_left();
            else move_left();
            return false;
            
        case Key::Right:
            if (ctrl) move_word_right();
            else move_right();
            return false;
            
        case Key::Home:
            move_home();
            return false;
            
        case Key::End:
            move_end();
            return false;
            
        case Key::Up:
        case Key::Down:
            // History - handled by caller
            return false;
    }
    
    // Printable character
    char c = vinput::to_ascii(code, modifiers);
    if (c && c != '\n' && c != '\b') {
        insert_char(c);
    }
    
    return false;
}

void LineEditor::insert_char(char c) {
    if (len_ >= MAX_LINE - 1) return;
    
    // Shift characters right
    for (int i = len_; i > cursor_; i--) {
        buffer_[i] = buffer_[i - 1];
    }
    
    buffer_[cursor_] = c;
    cursor_++;
    len_++;
    buffer_[len_] = '\0';
    
    // Redraw from cursor
    redraw_from_cursor();
}

void LineEditor::backspace() {
    if (cursor_ == 0) return;
    
    // Shift characters left
    for (int i = cursor_ - 1; i < len_ - 1; i++) {
        buffer_[i] = buffer_[i + 1];
    }
    
    cursor_--;
    len_--;
    buffer_[len_] = '\0';
    
    // Move cursor back and redraw
    move_cursor_left(1);
    redraw_from_cursor();
}

void LineEditor::delete_char() {
    if (cursor_ >= len_) return;
    
    // Shift characters left
    for (int i = cursor_; i < len_ - 1; i++) {
        buffer_[i] = buffer_[i + 1];
    }
    
    len_--;
    buffer_[len_] = '\0';
    
    redraw_from_cursor();
}

void LineEditor::move_left() {
    if (cursor_ > 0) {
        cursor_--;
        move_cursor_left(1);
    }
}

void LineEditor::move_right() {
    if (cursor_ < len_) {
        cursor_++;
        move_cursor_right(1);
    }
}

void LineEditor::move_home() {
    if (cursor_ > 0) {
        move_cursor_left(cursor_);
        cursor_ = 0;
    }
}

void LineEditor::move_end() {
    if (cursor_ < len_) {
        move_cursor_right(len_ - cursor_);
        cursor_ = len_;
    }
}

void LineEditor::move_word_left() {
    // Skip spaces
    while (cursor_ > 0 && buffer_[cursor_ - 1] == ' ') {
        cursor_--;
    }
    // Skip word
    while (cursor_ > 0 && buffer_[cursor_ - 1] != ' ') {
        cursor_--;
    }
    // Update display
    redraw();
}

void LineEditor::move_word_right() {
    // Skip word
    while (cursor_ < len_ && buffer_[cursor_] != ' ') {
        cursor_++;
    }
    // Skip spaces
    while (cursor_ < len_ && buffer_[cursor_] == ' ') {
        cursor_++;
    }
    redraw();
}

void LineEditor::kill_to_end() {
    len_ = cursor_;
    buffer_[len_] = '\0';
    clear_to_eol();
}

void LineEditor::kill_to_start() {
    // Shift remaining text to start
    int remaining = len_ - cursor_;
    for (int i = 0; i < remaining; i++) {
        buffer_[i] = buffer_[cursor_ + i];
    }
    len_ = remaining;
    buffer_[len_] = '\0';
    cursor_ = 0;
    
    redraw();
}

void LineEditor::redraw() {
    // Move to start of line, clear, reprint
    move_cursor_left(cursor_);
    clear_to_eol();
    vsys::debug_print(buffer_, len_);
    
    // Move cursor to correct position
    if (cursor_ < len_) {
        move_cursor_left(len_ - cursor_);
    }
}

void LineEditor::redraw_from_cursor() {
    // Print from cursor to end
    vsys::debug_print(buffer_ + cursor_, len_ - cursor_);
    clear_to_eol();
    
    // Move cursor back to correct position
    if (cursor_ < len_) {
        move_cursor_left(len_ - cursor_);
    }
}

} // namespace editor
```

---

## Milestone 6: History & Completion

**Duration:** Weeks 8-9  
**Deliverable:** Command history navigation, tab completion

### 6.1 Command History

```cpp
// user/vsh/history.hpp
#pragma once

namespace history {

constexpr int MAX_HISTORY = 100;
constexpr int MAX_LINE_LEN = 1024;

void init();

// Add a line to history
void add(const char* line);

// Navigate history (returns nullptr if at end)
const char* prev();  // Up arrow
const char* next();  // Down arrow

// Reset navigation (call when starting new input)
void reset_nav();

// Search history
const char* search(const char* prefix);

// Save/load from file
void save(const char* path);
void load(const char* path);

} // namespace history
```

```cpp
// user/vsh/history.cpp
#include "history.hpp"
#include "../lib/vsys.hpp"
#include <string.h>

namespace history {

namespace {
    char lines[MAX_HISTORY][MAX_LINE_LEN];
    int count = 0;
    int nav_pos = -1;  // -1 = current input, 0..count-1 = history
}

void init() {
    count = 0;
    nav_pos = -1;
}

void add(const char* line) {
    if (!line || !line[0]) return;
    
    // Don't add duplicates
    if (count > 0 && strcmp(lines[count - 1], line) == 0) {
        return;
    }
    
    // Shift if full
    if (count >= MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(lines[i], lines[i + 1]);
        }
        count = MAX_HISTORY - 1;
    }
    
    strncpy(lines[count], line, MAX_LINE_LEN - 1);
    lines[count][MAX_LINE_LEN - 1] = '\0';
    count++;
    nav_pos = -1;
}

const char* prev() {
    if (count == 0) return nullptr;
    
    if (nav_pos < 0) {
        nav_pos = count - 1;
    } else if (nav_pos > 0) {
        nav_pos--;
    } else {
        return nullptr;  // At oldest
    }
    
    return lines[nav_pos];
}

const char* next() {
    if (nav_pos < 0) return nullptr;
    
    if (nav_pos < count - 1) {
        nav_pos++;
        return lines[nav_pos];
    } else {
        nav_pos = -1;
        return nullptr;  // Back to current input
    }
}

void reset_nav() {
    nav_pos = -1;
}

const char* search(const char* prefix) {
    int prefix_len = strlen(prefix);
    
    for (int i = count - 1; i >= 0; i--) {
        if (strncmp(lines[i], prefix, prefix_len) == 0) {
            return lines[i];
        }
    }
    
    return nullptr;
}

} // namespace history
```

### 6.2 Tab Completion

```cpp
// user/vsh/completion.hpp
#pragma once

namespace completion {

struct Matches {
    static constexpr int MAX_MATCHES = 64;
    char* items[MAX_MATCHES];
    int count;
    int common_prefix_len;
};

// Complete a partial word
// Returns matches, caller must free items
Matches complete(const char* line, int cursor_pos);

// Free matches
void free_matches(Matches& m);

// Complete command name
Matches complete_command(const char* prefix);

// Complete file path
Matches complete_path(const char* prefix);

} // namespace completion
```

```cpp
// user/vsh/completion.cpp
#include "completion.hpp"
#include "../lib/vsys.hpp"
#include "../lib/vio.hpp"
#include <string.h>
#include <stdlib.h>

namespace completion {

namespace {
    char* strdup_local(const char* s) {
        int len = strlen(s);
        char* r = (char*)malloc(len + 1);
        if (r) strcpy(r, s);
        return r;
    }
    
    int common_prefix(const char* a, const char* b) {
        int i = 0;
        while (a[i] && b[i] && a[i] == b[i]) i++;
        return i;
    }
}

Matches complete_command(const char* prefix) {
    Matches m = {};
    int prefix_len = strlen(prefix);
    
    // Search C: directory for commands
    uint32_t c_dir = vio::open_assign("C");
    if (c_dir == 0) return m;
    
    DirInfo entries[64];
    int n = vsys::fs_readdir(c_dir, entries, 64);
    
    for (int i = 0; i < n && m.count < Matches::MAX_MATCHES; i++) {
        // Check if matches prefix and is a .vpr file
        if (strncasecmp(entries[i].name, prefix, prefix_len) == 0) {
            // Strip .vpr extension
            char* name = strdup_local(entries[i].name);
            char* dot = strrchr(name, '.');
            if (dot && strcasecmp(dot, ".vpr") == 0) {
                *dot = '\0';
            }
            m.items[m.count++] = name;
        }
    }
    
    vsys::fs_close(c_dir);
    
    // Calculate common prefix
    if (m.count > 1) {
        m.common_prefix_len = strlen(m.items[0]);
        for (int i = 1; i < m.count; i++) {
            int cp = common_prefix(m.items[0], m.items[i]);
            if (cp < m.common_prefix_len) {
                m.common_prefix_len = cp;
            }
        }
    } else if (m.count == 1) {
        m.common_prefix_len = strlen(m.items[0]);
    }
    
    return m;
}

Matches complete_path(const char* prefix) {
    Matches m = {};
    
    // Parse path into directory and partial name
    const char* last_sep = strrchr(prefix, '\\');
    const char* last_colon = strrchr(prefix, ':');
    const char* split = last_sep > last_colon ? last_sep : last_colon;
    
    char dir_path[256];
    const char* partial;
    
    if (split) {
        int dir_len = split - prefix + 1;
        strncpy(dir_path, prefix, dir_len);
        dir_path[dir_len] = '\0';
        partial = split + 1;
    } else {
        strcpy(dir_path, ".");
        partial = prefix;
    }
    
    int partial_len = strlen(partial);
    
    // Open directory
    uint32_t dir = vio::resolve_path(dir_path);
    if (dir == 0) return m;
    
    DirInfo entries[64];
    int n = vsys::fs_readdir(dir, entries, 64);
    
    for (int i = 0; i < n && m.count < Matches::MAX_MATCHES; i++) {
        if (strncasecmp(entries[i].name, partial, partial_len) == 0) {
            // Build full path
            char* full = (char*)malloc(256);
            if (split) {
                snprintf(full, 256, "%s%s", dir_path, entries[i].name);
            } else {
                strcpy(full, entries[i].name);
            }
            
            // Add \ for directories
            if (entries[i].kind == VKIND_DIRECTORY) {
                strcat(full, "\\");
            }
            
            m.items[m.count++] = full;
        }
    }
    
    vsys::fs_close(dir);
    
    // Calculate common prefix
    if (m.count > 1) {
        m.common_prefix_len = strlen(m.items[0]);
        for (int i = 1; i < m.count; i++) {
            int cp = common_prefix(m.items[0], m.items[i]);
            if (cp < m.common_prefix_len) {
                m.common_prefix_len = cp;
            }
        }
    } else if (m.count == 1) {
        m.common_prefix_len = strlen(m.items[0]);
    }
    
    return m;
}

Matches complete(const char* line, int cursor_pos) {
    // Find word start
    int word_start = cursor_pos;
    while (word_start > 0 && line[word_start - 1] != ' ') {
        word_start--;
    }
    
    // Extract partial word
    char partial[256];
    int len = cursor_pos - word_start;
    strncpy(partial, line + word_start, len);
    partial[len] = '\0';
    
    // Determine completion type
    // If first word or after ; or |, complete command
    bool is_command = (word_start == 0);
    if (!is_command) {
        for (int i = word_start - 1; i >= 0; i--) {
            if (line[i] == ';' || line[i] == '|') {
                is_command = true;
                break;
            }
            if (line[i] != ' ') break;
        }
    }
    
    if (is_command && !strchr(partial, ':') && !strchr(partial, '\\')) {
        return complete_command(partial);
    } else {
        return complete_path(partial);
    }
}

void free_matches(Matches& m) {
    for (int i = 0; i < m.count; i++) {
        free(m.items[i]);
    }
    m.count = 0;
}

} // namespace completion
```

---

## Milestone 7: Font System

**Duration:** Week 10  
**Deliverable:** Loadable bitmap fonts

### 7.1 Font File Format

```cpp
// kernel/font/vfont.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::font {

constexpr u32 VFONT_MAGIC = 0x544E4656;  // "VFNT"

// .vfont file header
struct VFontHeader {
    u32 magic;
    u32 version;
    u32 flags;
    u16 width;           // Glyph width in pixels
    u16 height;          // Glyph height in pixels
    u16 first_char;      // First character code
    u16 last_char;       // Last character code
    u32 glyph_offset;    // Offset to glyph data
    u32 glyph_size;      // Bytes per glyph
    char name[32];
    u8 _reserved[16];
};

// Glyph data follows header:
// For each character from first_char to last_char:
//   u8 bitmap[glyph_size]
// Bitmap is row-major, MSB first

} // namespace viper::font
```

### 7.2 Font Manager

```cpp
// kernel/font/font.hpp
#pragma once

#include "../lib/types.hpp"
#include "vfont.hpp"

namespace viper::font {

class Font {
public:
    bool load(const u8* data, usize size);
    bool load_from_file(const char* path);
    void unload();
    
    // Get glyph bitmap
    const u8* glyph(u32 codepoint) const;
    
    // Metrics
    u16 width() const { return header_.width; }
    u16 height() const { return header_.height; }
    u16 first_char() const { return header_.first_char; }
    u16 last_char() const { return header_.last_char; }
    const char* name() const { return header_.name; }
    
private:
    VFontHeader header_;
    u8* glyph_data_ = nullptr;
    bool loaded_ = false;
};

// Font manager
void init();

// Load a font from filesystem
Font* load_font(const char* name);

// Get default font
Font* default_font();

// Unload a font
void unload_font(Font* font);

} // namespace viper::font
```

### 7.3 Font Rendering

```cpp
// kernel/font/bitmap_font.cpp
#include "font.hpp"
#include "../fs/vfs.hpp"
#include "../console/console.hpp"
#include "../lib/string.hpp"

namespace viper::font {

namespace {
    Font system_font;
    Font* current_font = nullptr;
    
    // Built-in Topaz font for fallback
    extern const u8 topaz_font_data[];
    extern const usize topaz_font_size;
}

bool Font::load(const u8* data, usize size) {
    if (size < sizeof(VFontHeader)) return false;
    
    memcpy(&header_, data, sizeof(VFontHeader));
    
    if (header_.magic != VFONT_MAGIC) return false;
    
    usize num_glyphs = header_.last_char - header_.first_char + 1;
    usize glyph_data_size = num_glyphs * header_.glyph_size;
    
    if (size < header_.glyph_offset + glyph_data_size) return false;
    
    glyph_data_ = new u8[glyph_data_size];
    memcpy(glyph_data_, data + header_.glyph_offset, glyph_data_size);
    
    loaded_ = true;
    return true;
}

bool Font::load_from_file(const char* path) {
    auto file = fs::open_path(path, fs::VFS_READ);
    if (!file.is_ok()) return false;
    
    fs::FileInfo info;
    fs::stat(file.unwrap(), &info);
    
    u8* data = new u8[info.size];
    fs::read(file.unwrap(), data, info.size);
    fs::close(file.unwrap());
    
    bool result = load(data, info.size);
    delete[] data;
    
    return result;
}

void Font::unload() {
    delete[] glyph_data_;
    glyph_data_ = nullptr;
    loaded_ = false;
}

const u8* Font::glyph(u32 codepoint) const {
    if (!loaded_) return nullptr;
    if (codepoint < header_.first_char || codepoint > header_.last_char) {
        return nullptr;
    }
    
    u32 index = codepoint - header_.first_char;
    return glyph_data_ + index * header_.glyph_size;
}

void init() {
    // Load built-in font
    system_font.load(topaz_font_data, topaz_font_size);
    current_font = &system_font;
    
    // Try to load from filesystem
    Font* loaded = load_font("FONTS:topaz.vfont");
    if (loaded) {
        current_font = loaded;
    }
    
    kprintf("Font: Initialized (%s %dx%d)\n", 
            current_font->name(),
            current_font->width(),
            current_font->height());
}

Font* load_font(const char* path) {
    Font* font = new Font;
    if (font->load_from_file(path)) {
        return font;
    }
    delete font;
    return nullptr;
}

Font* default_font() {
    return current_font;
}

void unload_font(Font* font) {
    if (font && font != &system_font) {
        font->unload();
        delete font;
    }
}

} // namespace viper::font
```

---

## Milestone 8: Commands & Polish

**Duration:** Weeks 11-12  
**Deliverable:** Additional utilities, polish

### 8.1 Search Command

```cpp
// user/cmd/search.cpp
#include "../lib/vsys.hpp"
#include "../lib/vio.hpp"
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        print("Usage: Search <pattern> [file...]\n");
        return 5;
    }
    
    const char* pattern = argv[1];
    
    if (argc == 2) {
        // Read from stdin (or last file)
        char line[1024];
        while (vio::readline(line, sizeof(line)) >= 0) {
            if (strstr(line, pattern)) {
                print(line);
                print("\n");
            }
        }
    } else {
        // Search files
        for (int i = 2; i < argc; i++) {
            uint32_t file = vio::open_path(argv[i], VFS_READ);
            if (file == 0) {
                print("Cannot open: ");
                print(argv[i]);
                print("\n");
                continue;
            }
            
            char line[1024];
            int line_num = 0;
            
            while (vio::read_line(file, line, sizeof(line)) >= 0) {
                line_num++;
                if (strstr(line, pattern)) {
                    if (argc > 3) {
                        print(argv[i]);
                        print(":");
                    }
                    print_int(line_num);
                    print(": ");
                    print(line);
                    print("\n");
                }
            }
            
            vsys::fs_close(file);
        }
    }
    
    return 0;
}
```

### 8.2 Status Command

```cpp
// user/cmd/status.cpp
#include "../lib/vsys.hpp"

int main(int argc, char** argv) {
    print("ViperOS Status\n");
    print("==============\n\n");
    
    // Time
    uint64_t now = vsys::time_now();
    uint64_t secs = now / 1000000000ULL;
    uint64_t mins = secs / 60;
    uint64_t hours = mins / 60;
    
    print("Uptime: ");
    print_int(hours);
    print("h ");
    print_int(mins % 60);
    print("m ");
    print_int(secs % 60);
    print("s\n");
    
    // TODO: Memory usage, task count, etc.
    
    return 0;
}
```

### 8.3 Avail Command

```cpp
// user/cmd/avail.cpp
#include "../lib/vsys.hpp"

int main(int argc, char** argv) {
    // Get memory info via syscall
    uint64_t total, free, used;
    
    // TODO: Add memory info syscall
    total = 128 * 1024 * 1024;  // Placeholder
    free = 64 * 1024 * 1024;
    used = total - free;
    
    print("Memory Usage\n");
    print("============\n\n");
    
    print("Total: ");
    print_size(total);
    print("\n");
    
    print("Used:  ");
    print_size(used);
    print("\n");
    
    print("Free:  ");
    print_size(free);
    print("\n");
    
    return 0;
}
```

### 8.4 Updated vsh Main Loop

```cpp
// user/vsh/vsh.cpp (updated)
#include "line_editor.hpp"
#include "history.hpp"
#include "completion.hpp"
#include "../lib/vsys.hpp"
#include "../lib/vinput.hpp"

namespace {
    editor::LineEditor line_editor;
    char saved_line[1024];  // For history navigation
}

void handle_tab() {
    auto matches = completion::complete(
        line_editor.line(),
        line_editor.cursor()
    );
    
    if (matches.count == 0) {
        // Beep or flash
        return;
    }
    
    if (matches.count == 1) {
        // Complete with single match
        // Replace current word with match
        // ...
    } else {
        // Show matches
        print("\n");
        for (int i = 0; i < matches.count; i++) {
            print(matches.items[i]);
            print("  ");
        }
        print("\n");
        print_prompt();
        print(line_editor.line());
    }
    
    completion::free_matches(matches);
}

void handle_up() {
    if (history::nav_pos < 0) {
        // Save current input
        strcpy(saved_line, line_editor.line());
    }
    
    const char* hist = history::prev();
    if (hist) {
        line_editor.set_line(hist);
        // Redraw
    }
}

void handle_down() {
    const char* hist = history::next();
    if (hist) {
        line_editor.set_line(hist);
    } else {
        line_editor.set_line(saved_line);
    }
    // Redraw
}

int main() {
    print("ViperOS Shell v0.2\n");
    
    history::init();
    history::load("HOME:history");
    
    // Get keyboard handle
    uint32_t kbd = 0;
    vsys::input_get_handle(0, &kbd);
    
    while (true) {
        print_prompt();
        line_editor.reset();
        history::reset_nav();
        
        // Input loop
        bool done = false;
        while (!done) {
            InputEvent events[8];
            int n = vsys::input_poll(kbd, events, 8);
            
            for (int i = 0; i < n; i++) {
                if (events[i].type != EVENT_KEY) continue;
                if (events[i].value == KEY_RELEASED) continue;
                
                uint32_t mods = vinput::get_modifiers();
                uint32_t code = events[i].code;
                
                // Special handling
                if (code == Key::Tab) {
                    handle_tab();
                    continue;
                }
                if (code == Key::Up) {
                    handle_up();
                    continue;
                }
                if (code == Key::Down) {
                    handle_down();
                    continue;
                }
                
                // Process in line editor
                done = line_editor.process_key(code, mods);
            }
            
            if (n == 0) {
                vsys::task_yield();
            }
        }
        
        const char* line = line_editor.line();
        if (line[0] == '\0') continue;
        
        // Add to history
        history::add(line);
        
        // Execute
        // ... (existing execution code)
    }
    
    history::save("HOME:history");
    return 0;
}
```

---

## Weekly Schedule

| Week | Focus                | Deliverables                      |
|------|----------------------|-----------------------------------|
| 1    | virtio-input device  | Device discovery, virtqueue setup |
| 2    | virtio-input events  | Keyboard/mouse event reading      |
| 3    | Input subsystem      | Event queue, polling, handles     |
| 4    | Key translation      | HID to ASCII, modifiers           |
| 5    | Console input        | Getchar, basic readline           |
| 6    | Line editor core     | Insert, delete, cursor            |
| 7    | Line editor advanced | Word movement, kill lines         |
| 8    | Command history      | Up/down navigation, save/load     |
| 9    | Tab completion       | Command and path completion       |
| 10   | Font system          | .vfont loading, rendering         |
| 11   | Additional commands  | Search, Sort, Status, Avail       |
| 12   | Polish & testing     | Bug fixes, stability              |

---

## Definition of Done (Phase 5)

- [ ] Keyboard input works in QEMU
- [ ] Mouse events received (for future use)
- [ ] Line editing with cursor movement
- [ ] Backspace and Delete work
- [ ] Ctrl+A/E for Home/End
- [ ] Ctrl+K/U for kill lines
- [ ] Up/Down arrow for history
- [ ] Tab completion for commands
- [ ] Tab completion for paths
- [ ] History saved/loaded from file
- [ ] Font loadable from FONTS:
- [ ] Search, Status, Avail commands work
- [ ] Shell feels responsive and usable
- [ ] 30+ minutes of interactive use stable

---

## Phase 6 Preview

Phase 6 (Networking) adds connectivity:

1. **virtio-net** — Network device driver
2. **TCP/IP Stack** — Minimal implementation
3. **DNS Client** — Name resolution
4. **HTTP Client** — Web requests
5. **Viper.Net** — User-space networking library

Phase 5's input system will be essential for interactive network tools.

---

*"A good shell disappears—you stop noticing it and just work."*
