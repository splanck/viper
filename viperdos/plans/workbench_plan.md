# ViperDOS Workbench - Implementation Plan v3.0

## Vision

Create a fully-featured Amiga-inspired Workbench desktop environment for ViperDOS that provides:

- Intuitive graphical file system navigation
- Application launching and management
- System configuration utilities
- A cohesive, retro-modern aesthetic faithful to the original Amiga

The Workbench serves as the primary GUI interface for ViperDOS, bringing the beloved Amiga desktop experience to a
modern hybrid kernel operating system.

---

## Current State Assessment (January 26, 2025)

### COMPLETED Components

| Component         | Status       | Lines of Code | Notes                                                    |
|-------------------|--------------|---------------|----------------------------------------------------------|
| displayd          | **COMPLETE** | ~2000         | Compositor with double buffering, z-order                |
| libgui            | **COMPLETE** | ~3000         | Window creation, drawing, events                         |
| consoled          | **COMPLETE** | ~1500         | GUI terminal with ANSI support                           |
| **libwidget**     | **COMPLETE** | **8942**      | Full widget toolkit! Buttons, text, lists, dialogs       |
| Desktop           | **COMPLETE** | ~900          | Icons, menus, file browser, themes                       |
| File Browser      | **COMPLETE** | ~1400         | Navigation, context menus, properties dialog, copy/paste |
| Window Management | **COMPLETE** | ~1000         | Focus, z-order, minimize/maximize                        |
| **File Dialogs**  | **COMPLETE** | ~1174         | Open, Save, Folder selection dialogs                     |
| **Viewer**        | **COMPLETE** | ~850          | BMP image viewer with zoom                               |
| **Calculator**    | **COMPLETE** | ~795          | Full calculator with memory functions                    |
| **Clock**         | **COMPLETE** | ~794          | Analog/digital clock with uptime                         |
| guisysinfo        | **COMPLETE** | ~400          | System information display                               |

### PARTIALLY Complete Components

| Component     | Status          | What Works                        | What's Missing                |
|---------------|-----------------|-----------------------------------|-------------------------------|
| Preferences   | **90%**         | Display, categories, Cancel works | libprefs (persistent storage) |
| Task Manager  | **80%**         | Display, scrolling, selection     | Kill syscall, End Task button |
| **VEdit**     | **70%**         | Editing, Open/Save dialogs, nav   | Find/Replace, Undo, Selection |
| Desktop Icons | **WRONG STYLE** | Working but generic               | Need Amiga-style icons        |

### NOT Implemented

| Component    | Status          | Priority | Notes                            |
|--------------|-----------------|----------|----------------------------------|
| Amiga Icons  | **NOT STARTED** | HIGH     | Need wrench, question mark icons |
| libprefs     | **NOT STARTED** | MEDIUM   | Persistent preferences storage   |
| Kill Syscall | **NOT STARTED** | MEDIUM   | Kernel-side task termination     |

---

## Revised Phase Structure

### Phase 1: Amiga-Style Icons (IMMEDIATE - DO FIRST)

**Priority:** HIGH
**Effort:** Small (1-2 hours)
**Goal:** Make the desktop authentically Amiga-like

The current icons are generic and don't match the Amiga aesthetic. The Amiga used distinctive iconography that we should
replicate.

#### 1.1 Icon Replacements

| Current Icon  | New Design            | Amiga Reference                       |
|---------------|-----------------------|---------------------------------------|
| `settings_24` | **Wrench/Spanner**    | Amiga Prefs used a wrench tool icon   |
| `about_24`    | **Question Mark "?"** | Amiga help was a bold "?" in a circle |

#### 1.2 Wrench Icon Design (prefs_24)

```
Classic Amiga Prefs icon - a hand tool wrench:

        ████████
      ██        ██
    ██            ██
    ██    ████    ██
    ██  ████████  ██
      ████████████
          ██
          ██
        ██  ██
      ██      ██
    ██          ██
      ██      ██
```

Colors:

- Metal gray (0xFFAAAAAA) for body
- White (0xFFFFFFFF) highlights on upper-left edges
- Dark gray (0xFF555555) shadows on lower-right edges
- Black (0xFF000000) outline

#### 1.3 Question Mark Icon Design (help_24)

```
Classic Amiga Help icon - bold "?" on blue circle:

      ████████████
    ██            ██
  ██   ████████    ██
  ██   ██    ██    ██
  ██         ██    ██
  ██       ██      ██
  ██      ██       ██
  ██                ██
  ██      ██       ██
  ██      ██       ██
    ██            ██
      ████████████
```

Colors:

- Blue circle background (0xFF0055AA)
- White question mark (0xFFFFFFFF)
- Black outline (0xFF000000)

#### 1.4 Files to Modify

| File                               | Change                                      |
|------------------------------------|---------------------------------------------|
| `user/workbench/src/icons.cpp`     | Replace `settings_24` and `about_24` arrays |
| `user/workbench/include/icons.hpp` | Update comments to reflect new designs      |

#### 1.5 Testing Checklist

- [ ] Wrench icon displays correctly on desktop
- [ ] Question mark icon displays correctly on desktop
- [ ] Icons render properly when selected (highlighted)
- [ ] Icons look good on all theme backgrounds

---

### Phase 2: Complete VEdit Text Editor (HIGH PRIORITY)

**Priority:** HIGH
**Effort:** Medium (1-2 days)
**Goal:** Make VEdit a production-quality text editor

VEdit now has Open/Save dialogs working. The remaining features are critical for a usable editor.

#### 2.1 Current VEdit Status

| Feature              | Status    | Notes                      |
|----------------------|-----------|----------------------------|
| Basic editing        | ✅ Working | Insert, delete, navigation |
| Line numbers         | ✅ Working | Toggle via View menu       |
| Word wrap            | ✅ Working | Toggle via View menu       |
| File > New           | ✅ Working | Clears buffer              |
| File > Open...       | ✅ Working | Uses file dialog           |
| File > Save          | ✅ Working | Saves to current file      |
| File > Save As...    | ✅ Working | Uses file dialog           |
| Modified indicator   | ✅ Working | Shows "*" in status bar    |
| **Find (Ctrl+F)**    | ❌ Missing | Need search dialog         |
| **Replace (Ctrl+R)** | ❌ Missing | Need find/replace dialog   |
| **Go to Line**       | ❌ Missing | Need input dialog          |
| **Undo/Redo**        | ❌ Missing | Need operation history     |
| **Text Selection**   | ❌ Missing | Shift+arrows to select     |
| **Cut/Copy/Paste**   | ❌ Missing | Needs selection first      |

#### 2.2 Implementation Order

1. **Selection Support** (prerequisite for copy/paste)
    - Track selection start/end positions
    - Shift+Arrow to extend selection
    - Ctrl+A to select all
    - Render selection highlight

2. **Cut/Copy/Paste**
    - Global clipboard buffer
    - Ctrl+X, Ctrl+C, Ctrl+V
    - Edit menu items

3. **Find Dialog**
    - Text input for search term
    - "Find Next" (F3) and "Find Previous"
    - Highlight current match
    - Wrap around option

4. **Replace Dialog**
    - Extends Find dialog
    - Replace text input
    - "Replace", "Replace All" buttons

5. **Go to Line Dialog**
    - Simple number input
    - Ctrl+G shortcut
    - Jump to line number

6. **Undo/Redo**
    - Operation stack (insert, delete, replace)
    - Ctrl+Z undo, Ctrl+Y redo
    - Limit to 100 operations

#### 2.3 Files to Modify

```
user/vedit/
├── include/
│   ├── buffer.hpp     # Add undo stack, selection range
│   ├── editor.hpp     # Add selection state, clipboard
│   └── view.hpp       # Add selection rendering
└── src/
    ├── buffer.cpp     # Implement undo/redo
    ├── editor.cpp     # Selection handling, clipboard
    ├── view.cpp       # Selection highlight drawing
    └── main.cpp       # Wire up new keyboard shortcuts
```

#### 2.4 Menu Structure (Updated)

**File Menu:**

- New (Ctrl+N) ✅
- Open... (Ctrl+O) ✅
- Save (Ctrl+S) ✅
- Save As... (Ctrl+Shift+S) ✅

- ---

- Quit (Ctrl+Q) ✅

**Edit Menu:**

- Undo (Ctrl+Z) - **NEW**
- Redo (Ctrl+Y) - **NEW**

- ---

- Cut (Ctrl+X) - **NEW**
- Copy (Ctrl+C) - **NEW**
- Paste (Ctrl+V) - **NEW**

- ---

- Select All (Ctrl+A) - **NEW**

**Search Menu:** - **NEW MENU**

- Find... (Ctrl+F)
- Find Next (F3)
- Replace... (Ctrl+R)

- ---

- Go to Line... (Ctrl+G)

**View Menu:**

- Line Numbers ✅
- Word Wrap ✅

---

### Phase 3: Task Manager Completion (MEDIUM PRIORITY)

**Priority:** MEDIUM
**Effort:** Medium (requires kernel changes)
**Goal:** Make End Task and Priority buttons functional

#### 3.1 Kernel Changes Required

1. **SYS_TASK_KILL syscall** (new)
    - Takes PID as argument
    - Permission check: can't kill kernel tasks (PID 0-4)
    - Returns success/failure

2. **SYS_TASK_SET_PRIORITY syscall** (may exist, verify)
    - Takes PID and new priority
    - Priority range: 0-15
    - Permission check: can only change own tasks or children

#### 3.2 Task Manager Changes

1. **End Task button:**
    - Show confirmation dialog: "Terminate process 'name' (PID X)?"
    - Call kill syscall on confirm
    - Refresh task list
    - Handle errors (permission denied, already exited)

2. **Priority button:**
    - Show input dialog: "Enter new priority (0-15):"
    - Call set_priority syscall
    - Refresh task list

#### 3.3 Files to Modify

```
kernel/syscall/dispatch.cpp     # Add SYS_TASK_KILL handler
kernel/sched/task.cpp           # Implement task_kill()
user/taskman/main.cpp           # Wire up buttons
user/libwidget/include/dialog.h # Add confirmation/input dialogs
```

---

### Phase 4: Preferences Persistence (LOW PRIORITY)

**Priority:** LOW
**Effort:** Medium
**Goal:** Save and load preferences between sessions

#### 4.1 libprefs Library

Simple INI-style preferences storage:

```cpp
// user/libprefs/include/prefs.h

// Open/create preferences file
prefs_t* prefs_open(const char* filename);

// Get values
const char* prefs_get_string(prefs_t* p, const char* section,
                              const char* key, const char* default_val);
int prefs_get_int(prefs_t* p, const char* section,
                  const char* key, int default_val);

// Set values
void prefs_set_string(prefs_t* p, const char* section,
                      const char* key, const char* value);
void prefs_set_int(prefs_t* p, const char* section,
                   const char* key, int value);

// Save to disk
int prefs_save(prefs_t* p);

// Close
void prefs_close(prefs_t* p);
```

#### 4.2 Storage Format

```ini
; /sys/prefs/workbench.prefs

[Screen]
theme=ClassicAmiga

[Input]
double_click_ms=400
pointer_speed=5

[Clock]
format=24hour
show_seconds=true
```

#### 4.3 Files to Create

```
user/libprefs/
├── CMakeLists.txt
├── include/
│   └── prefs.h
└── src/
    └── prefs.cpp
```

---

## Implementation Priority Order

### IMMEDIATE (Do First)

1. **Phase 1: Amiga Icons** - Small effort, high visual impact
    - Create wrench icon for Prefs
    - Create question mark icon for Help

### SHORT TERM (This Week)

2. **Phase 2: VEdit Completion** - Most important app
    - Add selection support
    - Add cut/copy/paste
    - Add Find/Replace dialogs
    - Add Go to Line
    - Add Undo/Redo

### MEDIUM TERM (Next Week)

3. **Phase 3: Task Manager** - Requires kernel work
    - Add kill syscall
    - Wire up End Task button
    - Wire up Priority button

### LOW PRIORITY (When Time Permits)

4. **Phase 4: libprefs** - Nice to have
    - Create preferences library
    - Wire up Prefs app save/load

---

## What's Already Done (Removed from Plan)

The following items from the original plan have been completed and are no longer tracked:

| Item            | Completed Date | Notes                                         |
|-----------------|----------------|-----------------------------------------------|
| libwidget       | Jan 2025       | 8942 lines, full widget toolkit               |
| File dialogs    | Jan 2025       | Open, Save, Folder dialogs in libwidget       |
| Image Viewer    | Jan 2025       | BMP viewer with zoom                          |
| Calculator      | Jan 2025       | Full calculator with memory                   |
| Clock           | Jan 2025       | Analog/digital with uptime                    |
| File Browser    | Jan 2025       | Context menus, properties, copy/paste         |
| Theme System    | Jan 2025       | 4 themes: Classic, Dark, Modern, HighContrast |
| VEdit Open/Save | Jan 2025       | File dialogs integrated                       |

---

## Testing Checklist

### Phase 1 Tests (Icons)

- [ ] Wrench icon displays on desktop for "Prefs"
- [ ] Question mark icon displays on desktop for "Help"
- [ ] Icons render correctly when selected
- [ ] Icons look good on all 4 themes

### Phase 2 Tests (VEdit)

- [ ] Shift+Arrow selects text
- [ ] Selection is visually highlighted
- [ ] Ctrl+A selects all text
- [ ] Ctrl+C copies selection to clipboard
- [ ] Ctrl+X cuts selection to clipboard
- [ ] Ctrl+V pastes from clipboard
- [ ] Ctrl+F opens Find dialog
- [ ] Find locates text and highlights match
- [ ] F3 finds next occurrence
- [ ] Ctrl+R opens Replace dialog
- [ ] Replace works correctly
- [ ] Replace All works correctly
- [ ] Ctrl+G opens Go to Line dialog
- [ ] Go to Line jumps to correct line
- [ ] Ctrl+Z undoes last action
- [ ] Ctrl+Y redoes undone action
- [ ] Undo works for multiple operations

### Phase 3 Tests (Task Manager)

- [ ] End Task shows confirmation dialog
- [ ] End Task terminates the selected process
- [ ] End Task refreshes the list
- [ ] Cannot kill kernel processes (PID 0-4)
- [ ] Priority button shows input dialog
- [ ] Priority change is applied

### Phase 4 Tests (Preferences)

- [ ] Preferences save to file on "Save"
- [ ] Preferences load on app start
- [ ] Theme preference persists across restarts
- [ ] Invalid preferences file is handled gracefully

---

## Success Criteria

The ViperDOS Workbench will be considered **feature complete** when:

1. **Authentic Amiga Look**: Desktop uses proper Amiga-style icons (wrench, question mark)

2. **Full Text Editor**: VEdit has Find/Replace, Undo/Redo, and clipboard support

3. **Task Management**: Task Manager can terminate and reprioritize processes

4. **Persistent Settings**: Preferences are saved and loaded between sessions

5. **Complete App Suite**: Viewer, Calculator, and Clock are all functional ✅

6. **Widget Toolkit**: Applications can use libwidget for consistent UI ✅

---

*Document Version: 3.0*
*Last Updated: January 26, 2025*
*Author: ViperDOS Development Team*

### Changelog

**v3.0 (2025-01-26)**

- Major update to reflect actual implementation status
- Marked libwidget, Viewer, Calculator, Clock as COMPLETE
- Marked File Dialogs as COMPLETE
- Updated VEdit status (Open/Save dialogs now working)
- Reorganized phases to focus on remaining work
- Added detailed Amiga icon design specifications
- Removed completed items from active plan
- Added comprehensive testing checklists

**v2.0 (2025-01-26)**

- Complete rewrite with honest assessment
- Added Phase 3.5 for icon updates
- Added libdialog as prerequisite

**v1.0 (2025-01)**

- Initial plan document
