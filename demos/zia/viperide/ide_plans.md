# ViperIDE — Comprehensive Development Plan

**Project:** ViperIDE - A First-Class IDE for Viper
**Implementation Language:** Zia
**Target Languages:** Zia, BASIC (extensible to future languages)
**Status:** Planning Phase
**Last Updated:** 2026-01-14

---

## Executive Summary

ViperIDE is an ambitious, full-featured integrated development environment for the Viper platform, written entirely in Zia using the Viper.GUI.* runtime classes. The IDE will provide first-class support for Zia and Viper BASIC, with an extensible architecture designed to accommodate future languages.

The vision is to create a professional-grade development environment that rivals mainstream IDEs while showcasing the capabilities of the Viper platform itself — proving that Viper can build sophisticated applications.

---

## Table of Contents

1. [Vision & Goals](#1-vision--goals)
2. [Architecture Overview](#2-architecture-overview)
3. [Core Components](#3-core-components)
4. [UI Layout & Design](#4-ui-layout--design)
5. [Feature Specifications](#5-feature-specifications)
6. [Language Support System](#6-language-support-system)
7. [Project Management](#7-project-management)
8. [Build & Run Integration](#8-build--run-integration)
9. [Debugging Support](#9-debugging-support)
10. [Extensions & Plugins](#10-extensions--plugins)
11. [Runtime Requirements](#11-runtime-requirements)
12. [Implementation Phases](#12-implementation-phases)
13. [File Structure](#13-file-structure)
14. [Technical Challenges](#14-technical-challenges)
15. [Success Metrics](#15-success-metrics)

---

## 1. Vision & Goals

### 1.1 Vision Statement

> ViperIDE will be the definitive development environment for Viper, providing an intuitive, powerful, and extensible platform that makes writing Zia and BASIC programs a joy while demonstrating the full capabilities of the Viper runtime.

### 1.2 Primary Goals

1. **Professional Quality** — Match the polish and usability of mainstream IDEs
2. **Language First-Class Support** — Deep integration with Zia and BASIC compilers
3. **Self-Hosting Showcase** — Prove Viper can build complex applications
4. **Extensibility** — Plugin architecture for future languages and tools
5. **Performance** — Smooth 60fps UI, instant feedback, fast compilation
6. **Cross-Platform** — Run on any platform Viper supports

### 1.3 Target Users

- **Beginners** — Learning Zia or BASIC with helpful guidance
- **Hobbyists** — Building games and applications for fun
- **Educators** — Teaching programming concepts
- **Viper Contributors** — Developing the Viper platform itself

### 1.4 Non-Goals (v1.0)

- Remote development / SSH
- Team collaboration features
- Mobile platform support
- Visual UI designers (drag-and-drop)

---

## 2. Architecture Overview

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         ViperIDE                                │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   UI Layer  │  │  Commands   │  │    Extension Host       │  │
│  │  (Widgets)  │  │  & Actions  │  │    (Plugin System)      │  │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘  │
│         │                │                     │                │
│  ┌──────┴────────────────┴─────────────────────┴─────────────┐  │
│  │                    Core Services                          │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────────┐  │  │
│  │  │ Editor  │ │ Project │ │ Language│ │ Build/Debug     │  │  │
│  │  │ Manager │ │ Manager │ │ Services│ │ Integration     │  │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                  │
│  ┌───────────────────────────┴───────────────────────────────┐  │
│  │                   Viper Runtime                            │  │
│  │   Viper.GUI.*  │  Viper.File.*  │  Viper.Exec.*  │  ...   │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Core Subsystems

| Subsystem | Responsibility |
|-----------|----------------|
| **UI Layer** | All visual components, layouts, theming |
| **Editor Manager** | Document handling, tabs, editing operations |
| **Project Manager** | Workspace, file tree, project configuration |
| **Language Services** | Syntax highlighting, completion, diagnostics |
| **Build Integration** | Compile, run, output capture |
| **Debug Integration** | Breakpoints, stepping, variable inspection |
| **Command System** | Keyboard shortcuts, command palette |
| **Extension Host** | Plugin loading, API exposure |
| **Settings Manager** | User preferences, workspace settings |

### 2.3 Design Principles

1. **Entity-Based Architecture** — Core components as Zia entities
2. **Event-Driven** — Decoupled communication via events
3. **Lazy Loading** — Load features on demand
4. **Stateless Views** — UI reflects model state, not vice versa
5. **Undo Everywhere** — All operations reversible

---

## 3. Core Components

### 3.1 Editor Component

The heart of the IDE — a powerful code editor with modern features.

#### 3.1.1 Basic Editing
- Multi-cursor editing
- Block selection (Alt+drag)
- Line operations (move, duplicate, delete)
- Smart indentation
- Auto-closing brackets/quotes
- Whitespace visualization
- Line wrapping (soft/hard)

#### 3.1.2 Navigation
- Go to line (Ctrl+G)
- Go to symbol (Ctrl+Shift+O)
- Go to definition (F12)
- Find references (Shift+F12)
- Bracket matching and navigation
- Bookmarks

#### 3.1.3 Selection
- Expand/shrink selection semantically
- Select all occurrences
- Multi-cursor from selection
- Column selection mode

#### 3.1.4 Code Intelligence
- Syntax highlighting (language-aware)
- Code folding (regions, functions, blocks)
- Minimap with code preview
- Breadcrumb navigation
- Hover information
- Signature help
- Quick fixes and refactoring

### 3.2 File Explorer

#### 3.2.1 Tree View Features
- Hierarchical file/folder display
- Expand/collapse folders
- File type icons
- Modified indicator
- Git status indicators (future)

#### 3.2.2 Operations
- Create file/folder
- Rename (F2)
- Delete (with confirmation)
- Copy/paste/duplicate
- Drag and drop reordering
- Multi-select operations

#### 3.2.3 Filtering
- Search/filter files
- Hide patterns (e.g., build/, .git/)
- Show/hide hidden files

### 3.3 Tab Bar

#### 3.3.1 Features
- Reorderable tabs (drag)
- Close button per tab
- Modified indicator (dot)
- Tab overflow menu
- Split editor (horizontal/vertical)
- Tab groups

#### 3.3.2 Behaviors
- Middle-click to close
- Double-click to pin
- Right-click context menu
- Tab preview on hover

### 3.4 Status Bar

#### 3.4.1 Left Zone
- Current file path
- Git branch (future)
- Errors/warnings count

#### 3.4.2 Center Zone
- Build status
- Background task progress

#### 3.4.3 Right Zone
- Line:Column position
- Selection count
- File encoding (UTF-8)
- Line ending (LF/CRLF)
- Language mode
- Indentation (spaces/tabs)

### 3.5 Output Panel

#### 3.5.1 Tabs
- **Output** — Build output, compiler messages
- **Problems** — Errors and warnings list
- **Terminal** — Integrated terminal (future)
- **Debug Console** — Debug output

#### 3.5.2 Features
- ANSI color support
- Clickable file:line references
- Clear button
- Filter/search
- Word wrap toggle

### 3.6 Sidebar Panels

#### 3.6.1 Explorer Panel
- File tree
- Open editors list
- Outline view (symbols in current file)

#### 3.6.2 Search Panel
- Find in files
- Replace in files
- Search history
- Include/exclude patterns
- Regex support

#### 3.6.3 Debug Panel
- Variables view
- Watch expressions
- Call stack
- Breakpoints list

#### 3.6.4 Extensions Panel (Future)
- Installed extensions
- Extension marketplace
- Extension settings

---

## 4. UI Layout & Design

### 4.1 Main Window Layout

```
┌─────────────────────────────────────────────────────────────────┐
│ Menu Bar                                                        │
│ [File] [Edit] [View] [Go] [Run] [Help]                          │
├─────────────────────────────────────────────────────────────────┤
│ Toolbar                                                         │
│ [New] [Open] [Save] [SaveAll] │ [Undo] [Redo] │ [Run] [Debug]   │
├────────┬────────────────────────────────────────────────────────┤
│        │ Tab Bar                                                │
│        │ [main.zia] [utils.zia] [config.zia*] [+]               │
│        ├────────────────────────────────────────┬───────────────┤
│  Side  │                                        │               │
│  Bar   │                                        │   Minimap     │
│        │           Code Editor                  │               │
│ [Files]│                                        │               │
│ [Srch] │   1 │ module Main;                     │               │
│ [Debug]│   2 │                                  │               │
│        │   3 │ func start() {                   │               │
│        │   4 │     Viper.Terminal.Say("Hi");    │               │
│        │   5 │ }                                │               │
│        │                                        │               │
│        ├────────────────────────────────────────┴───────────────┤
│        │ Output Panel                           [Output][Probs] │
│        │ > Building main.zia...                                 │
│        │ > Build succeeded (0.23s)                              │
├────────┴────────────────────────────────────────────────────────┤
│ Status Bar                                                      │
│ main.zia │ Zia │ UTF-8 │ LF │ Ln 4, Col 12 │ Spaces: 4 │ Ready │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 Layout Implementation

Using Viper.GUI layout containers:

```
App
└── VBox (main container)
    ├── MenuBar
    ├── Toolbar (HBox)
    ├── SplitPane (horizontal) — sidebar | main
    │   ├── Sidebar (VBox)
    │   │   ├── SidebarTabs
    │   │   └── SidebarContent (Explorer/Search/Debug)
    │   └── SplitPane (vertical) — editor | output
    │       ├── EditorArea (VBox)
    │       │   ├── TabBar
    │       │   └── SplitPane — editor | minimap
    │       │       ├── CodeEditor
    │       │       └── Minimap
    │       └── OutputPanel (VBox)
    │           ├── OutputTabs
    │           └── OutputContent
    └── StatusBar (HBox)
```

### 4.3 Theming

#### 4.3.1 Built-in Themes
- **Viper Dark** (default) — Dark theme optimized for coding
- **Viper Light** — Light theme for bright environments
- **High Contrast** — Accessibility-focused theme

#### 4.3.2 Theme Components
- Editor colors (background, foreground, selection)
- Syntax colors (keywords, strings, comments, etc.)
- UI colors (panels, borders, buttons)
- Icon themes

### 4.4 Responsive Design

- Minimum window size: 800x600
- Collapsible sidebar
- Collapsible output panel
- Resizable split panes
- Panel position memory

---

## 5. Feature Specifications

### 5.1 File Operations

| Feature | Shortcut | Description |
|---------|----------|-------------|
| New File | Ctrl+N | Create new untitled file |
| New Window | Ctrl+Shift+N | Open new IDE window |
| Open File | Ctrl+O | Open file dialog |
| Open Folder | Ctrl+K Ctrl+O | Open folder as workspace |
| Open Recent | Ctrl+R | Recent files/folders |
| Save | Ctrl+S | Save current file |
| Save As | Ctrl+Shift+S | Save with new name |
| Save All | Ctrl+Alt+S | Save all modified files |
| Close | Ctrl+W | Close current tab |
| Close All | Ctrl+K Ctrl+W | Close all tabs |

### 5.2 Edit Operations

| Feature | Shortcut | Description |
|---------|----------|-------------|
| Undo | Ctrl+Z | Undo last change |
| Redo | Ctrl+Y | Redo undone change |
| Cut | Ctrl+X | Cut selection |
| Copy | Ctrl+C | Copy selection |
| Paste | Ctrl+V | Paste clipboard |
| Select All | Ctrl+A | Select all text |
| Find | Ctrl+F | Find in file |
| Replace | Ctrl+H | Find and replace |
| Find in Files | Ctrl+Shift+F | Search workspace |
| Toggle Comment | Ctrl+/ | Comment/uncomment line |
| Format Document | Ctrl+Shift+I | Auto-format code |

### 5.3 View Operations

| Feature | Shortcut | Description |
|---------|----------|-------------|
| Command Palette | Ctrl+Shift+P | Open command search |
| Quick Open | Ctrl+P | Quick file open |
| Toggle Sidebar | Ctrl+B | Show/hide sidebar |
| Toggle Output | Ctrl+` | Show/hide output |
| Zoom In | Ctrl+= | Increase font size |
| Zoom Out | Ctrl+- | Decrease font size |
| Reset Zoom | Ctrl+0 | Reset to default |
| Toggle Fullscreen | F11 | Fullscreen mode |
| Split Editor | Ctrl+\ | Split view |

### 5.4 Navigation

| Feature | Shortcut | Description |
|---------|----------|-------------|
| Go to Line | Ctrl+G | Jump to line number |
| Go to Symbol | Ctrl+Shift+O | Find symbol in file |
| Go to Definition | F12 | Jump to definition |
| Peek Definition | Alt+F12 | Inline definition view |
| Go Back | Alt+Left | Previous location |
| Go Forward | Alt+Right | Next location |
| Next Tab | Ctrl+Tab | Switch to next tab |
| Prev Tab | Ctrl+Shift+Tab | Switch to prev tab |

### 5.5 Code Intelligence

| Feature | Trigger | Description |
|---------|---------|-------------|
| Auto-Complete | Type or Ctrl+Space | Show completions |
| Signature Help | ( | Show function signature |
| Hover Info | Mouse hover | Show type/docs |
| Quick Fix | Ctrl+. | Show available fixes |
| Rename Symbol | F2 | Rename across files |
| Find References | Shift+F12 | Find all usages |

### 5.6 Build & Run

| Feature | Shortcut | Description |
|---------|----------|-------------|
| Run | F5 | Run current file |
| Run Without Debug | Ctrl+F5 | Run without debugger |
| Build | Ctrl+B | Build project |
| Clean | Ctrl+Shift+B | Clean build |
| Stop | Shift+F5 | Stop running program |

### 5.7 Debug

| Feature | Shortcut | Description |
|---------|----------|-------------|
| Toggle Breakpoint | F9 | Add/remove breakpoint |
| Start Debugging | F5 | Start with debugger |
| Step Over | F10 | Execute current line |
| Step Into | F11 | Step into function |
| Step Out | Shift+F11 | Step out of function |
| Continue | F5 | Continue execution |
| Restart | Ctrl+Shift+F5 | Restart debugging |

---

## 6. Language Support System

### 6.1 Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Language Service Interface                   │
├─────────────────────────────────────────────────────────────────┤
│  getSyntaxHighlighting(text) → TokenList                        │
│  getCompletions(file, position) → CompletionList                │
│  getHoverInfo(file, position) → HoverInfo                       │
│  getDiagnostics(file) → DiagnosticList                          │
│  getDefinition(file, position) → Location                       │
│  getReferences(file, position) → LocationList                   │
│  getSymbols(file) → SymbolList                                  │
│  formatDocument(file) → TextEdits                               │
│  getRenameEdits(file, position, newName) → WorkspaceEdit        │
└─────────────────────────────────────────────────────────────────┘
                    ▲                           ▲
                    │                           │
         ┌──────────┴──────────┐     ┌──────────┴──────────┐
         │  Zia Language       │     │  BASIC Language     │
         │  Service            │     │  Service            │
         └─────────────────────┘     └─────────────────────┘
```

### 6.2 Zia Language Service

#### 6.2.1 Syntax Highlighting
- Keywords: `module`, `import`, `func`, `entity`, `value`, `var`, `final`, `if`, `else`, `while`, `for`, `return`, `new`
- Types: `Integer`, `Number`, `String`, `Boolean`, custom types
- Literals: numbers, strings, booleans
- Comments: `//` and `/* */`
- Operators and punctuation

#### 6.2.2 Code Completion
- Module members
- Entity fields and methods
- Function parameters
- Local variables
- Import suggestions
- Runtime library (`Viper.*`)

#### 6.2.3 Diagnostics
- Syntax errors from parser
- Type errors from semantic analysis
- Unused variable warnings
- Import resolution errors

#### 6.2.4 Navigation
- Go to function definition
- Go to entity definition
- Go to imported module
- Find all references

### 6.3 BASIC Language Service

#### 6.3.1 Syntax Highlighting
- Keywords: `DIM`, `LET`, `IF`, `THEN`, `ELSE`, `END IF`, `FOR`, `NEXT`, `WHILE`, `WEND`, `SUB`, `FUNCTION`, `CLASS`
- Line numbers (optional)
- String literals
- Comments: `REM` and `'`
- Built-in functions

#### 6.3.2 Code Completion
- Built-in statements
- Built-in functions
- User-defined subs/functions
- Class members
- Variable names

#### 6.3.3 Diagnostics
- Syntax errors
- Type mismatches
- Undefined variables/functions
- Deprecated feature warnings

### 6.4 Adding New Languages

Language support follows a plugin architecture:

```zia
entity LanguageService {
    String id;           // "zia", "basic", "il"
    String name;         // "Zia", "BASIC", "Viper IL"
    List[String] extensions;  // [".zia"], [".bas"], [".il"]

    func getHighlighting(text: String) -> List[Token];
    func getCompletions(file: String, line: Integer, col: Integer) -> List[Completion];
    func getDiagnostics(file: String) -> List[Diagnostic];
    // ... etc
}
```

---

## 7. Project Management

### 7.1 Workspace Model

```
Workspace
├── name: String
├── rootPath: String
├── folders: List[WorkspaceFolder]
├── settings: WorkspaceSettings
└── state: WorkspaceState

WorkspaceFolder
├── path: String
├── name: String
└── excludePatterns: List[String]
```

### 7.2 Project File (.viperproj)

```json
{
    "name": "MyProject",
    "version": "1.0.0",
    "language": "zia",
    "main": "src/main.zia",
    "sources": ["src/**/*.zia"],
    "exclude": ["build/", "test/"],
    "dependencies": [],
    "build": {
        "output": "build/",
        "target": "native",
        "optimization": "release"
    },
    "run": {
        "arguments": [],
        "workingDirectory": "."
    }
}
```

### 7.3 Recent Projects

- Track recently opened files and folders
- Quick access from welcome screen
- Pinnable favorites
- Clear history option

### 7.4 File Templates

Provide templates for new files:
- Empty Zia module
- Zia entity
- Zia main with entry point
- BASIC program
- BASIC class
- Test file

---

## 8. Build & Run Integration

### 8.1 Build System

#### 8.1.1 Zia Projects
```
zia <file.zia>           # Run directly
zia <file.zia> --emit-il # Compile to IL
ilc codegen arm64 <file.il> -o <output>  # Native compile
```

#### 8.1.2 BASIC Projects
```
vbasic <file.bas>           # Run directly
vbasic <file.bas> --emit-il # Compile to IL
```

### 8.2 Build Tasks

```json
{
    "tasks": [
        {
            "name": "Build",
            "command": "zia",
            "args": ["${file}", "--emit-il", "-o", "${fileBasenameNoExt}.il"],
            "group": "build"
        },
        {
            "name": "Run",
            "command": "zia",
            "args": ["${file}"],
            "group": "run"
        }
    ]
}
```

### 8.3 Output Capture

- Capture stdout/stderr from compiler
- Parse error messages for clickable links
- Display in Output panel
- Populate Problems panel with diagnostics

### 8.4 Run Configurations

- Run current file
- Run main file
- Run with arguments
- Run in terminal
- Run with environment variables

---

## 9. Debugging Support

### 9.1 Debug Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Debug UI       │────▶│  Debug Adapter  │────▶│  Viper VM       │
│  (ViperIDE)     │◀────│  Protocol       │◀────│  (with debug)   │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

### 9.2 Debug Features

#### 9.2.1 Breakpoints
- Line breakpoints
- Conditional breakpoints
- Hit count breakpoints
- Logpoints (print without stopping)
- Exception breakpoints

#### 9.2.2 Execution Control
- Continue (F5)
- Pause
- Step Over (F10)
- Step Into (F11)
- Step Out (Shift+F11)
- Restart
- Stop

#### 9.2.3 Inspection
- Variables panel (locals, globals)
- Watch expressions
- Call stack
- Hover to inspect
- Inline values in editor

#### 9.2.4 Debug Console
- Evaluate expressions
- REPL-style interaction
- Output display

### 9.3 Debug Configuration

```json
{
    "configurations": [
        {
            "name": "Debug Current File",
            "type": "zia",
            "request": "launch",
            "program": "${file}",
            "stopOnEntry": false
        }
    ]
}
```

---

## 10. Extensions & Plugins

### 10.1 Extension Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       Extension Host                            │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Extension 1 │  │ Extension 2 │  │      Extension N        │  │
│  │ (Language)  │  │  (Theme)    │  │      (Tool)             │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                      Extension API                              │
│  - registerLanguage()      - registerCommand()                  │
│  - registerTheme()         - registerView()                     │
│  - registerFormatter()     - onFileOpen()                       │
│  - registerDebugger()      - onFileSave()                       │
└─────────────────────────────────────────────────────────────────┘
```

### 10.2 Extension Types

| Type | Purpose | Examples |
|------|---------|----------|
| Language | Add language support | Lua, IL |
| Theme | Color schemes | Monokai, Solarized |
| Formatter | Code formatting | Prettier-style |
| Linter | Code analysis | Style checker |
| Debugger | Debug adapter | Custom debuggers |
| Tool | External integration | Git, Docker |
| Snippet | Code snippets | Common patterns |

### 10.3 Extension Manifest

```json
{
    "name": "viper-il",
    "displayName": "Viper IL Language",
    "version": "1.0.0",
    "description": "Viper IL language support for ViperIDE",
    "main": "extension.zia",
    "contributes": {
        "languages": [{
            "id": "il",
            "extensions": [".il"],
            "configuration": "./language-configuration.json"
        }],
        "grammars": [{
            "language": "il",
            "scopeName": "source.il",
            "path": "./syntaxes/il.json"
        }]
    }
}
```

### 10.4 Built-in Extensions

- Zia Language (core)
- BASIC Language (core)
- Viper IL Language
- Dark Theme
- Light Theme
- File Icons

---

## 11. Runtime Requirements

### 11.1 Currently Available (Can Use Now)

These Viper.GUI.* classes are exposed in the runtime:

| Widget | Status | Notes |
|--------|--------|-------|
| App | Available | Main window |
| Label | Available | Text display |
| Button | Available | Clickable button |
| TextInput | Available | Single-line input |
| Checkbox | Available | Toggle |
| RadioButton | Available | Radio selection |
| Slider | Available | Value slider |
| Spinner | Available | Numeric input |
| ProgressBar | Available | Progress display |
| Dropdown | Available | Selection list |
| ListBox | Available | Scrollable list |
| Image | Available | Image display |
| CodeEditor | Available | Code editing |
| VBox | Available | Vertical layout |
| HBox | Available | Horizontal layout |
| ScrollView | Available | Scrollable container |
| SplitPane | Available | Resizable split |
| TabBar | Available | Tab interface |
| TreeView | Available | Hierarchical tree |
| Font | Available | Font loading |

### 11.2 Required Additions (Must Be Exposed)

These exist in C++ (`vg_ide_widgets.h`) but need runtime bindings:

| Widget | Priority | IDE Use Case |
|--------|----------|--------------|
| **MenuBar** | Critical | File/Edit/View menus |
| **Menu** | Critical | Menu items |
| **MenuItem** | Critical | Menu actions |
| **ContextMenu** | Critical | Right-click menus |
| **Toolbar** | Critical | Quick actions bar |
| **StatusBar** | Critical | Status information |
| **Dialog** | Critical | Confirmations, alerts |
| **FileDialog** | Critical | Open/Save dialogs |
| **MessageBox** | High | Alerts and confirmations |
| **FindReplaceBar** | High | Search in editor |
| **CommandPalette** | High | Quick command access |
| **Tooltip** | High | Hover information |
| **Breadcrumb** | Medium | Path navigation |
| **Minimap** | Medium | Code overview |
| **NotificationToast** | Medium | Non-blocking alerts |

### 11.3 Required New Features

| Feature | Priority | Description |
|---------|----------|-------------|
| **Keyboard Shortcuts** | Critical | Global hotkey handling |
| **Clipboard** | Critical | System clipboard access |
| **Drag and Drop** | High | File/tab dragging |
| **Focus Management** | High | Keyboard navigation |
| **Cursor Styles** | Medium | Custom cursors |
| **Window State** | Medium | Maximize, minimize |
| **Multi-Window** | Low | Multiple IDE windows |

### 11.4 CodeEditor Enhancements Needed

| Feature | Priority | Description |
|---------|----------|-------------|
| **Syntax Highlighting API** | Critical | Set token colors |
| **Gutter** | Critical | Line numbers, icons |
| **Folding API** | High | Collapse regions |
| **Multiple Cursors** | High | Multi-cursor editing |
| **Markers** | High | Error underlines, etc. |
| **Decorations** | High | Line highlighting |
| **Selection Events** | Medium | Selection callbacks |
| **Scroll Events** | Medium | Scroll position callbacks |

### 11.5 Additional Runtime Needs

| Module | Feature | Use Case |
|--------|---------|----------|
| Viper.Exec | Process spawn with I/O | Compiler integration |
| Viper.Exec | Kill process | Stop running program |
| Viper.File | Watch for changes | External file edits |
| Viper.Path | Path manipulation | Project navigation |
| Viper.Environment | Env variables | Build configuration |

---

## 12. Implementation Phases

### Phase 1: Foundation (MVP)

**Goal:** Basic text editor with file operations

**Duration:** 4-6 weeks

**Features:**
- [ ] Main window with basic layout
- [ ] Single CodeEditor widget
- [ ] File open/save using native dialogs (or workaround)
- [ ] Basic toolbar (New, Open, Save)
- [ ] Tab bar for multiple files
- [ ] Simple status bar
- [ ] Basic keyboard shortcuts

**Dependencies:**
- Existing GUI widgets sufficient
- May need FileDialog workaround

---

### Phase 2: Project Support

**Goal:** Folder-based project management

**Duration:** 4-6 weeks

**Features:**
- [ ] File explorer tree view
- [ ] Open folder as workspace
- [ ] Create/rename/delete files
- [ ] Project file (.viperproj)
- [ ] Recent files/folders
- [ ] File icons by type

**Dependencies:**
- TreeView (available)
- File operations (available)

---

### Phase 3: Language Intelligence

**Goal:** Zia and BASIC language support

**Duration:** 6-8 weeks

**Features:**
- [ ] Syntax highlighting for Zia
- [ ] Syntax highlighting for BASIC
- [ ] Basic auto-completion
- [ ] Error/warning display
- [ ] Go to definition
- [ ] Symbol outline

**Dependencies:**
- CodeEditor highlighting API (needs enhancement)
- Compiler integration for diagnostics

---

### Phase 4: Build Integration

**Goal:** Compile and run from IDE

**Duration:** 3-4 weeks

**Features:**
- [ ] Run current file
- [ ] Build project
- [ ] Output panel for compiler output
- [ ] Problems panel for errors
- [ ] Clickable error locations
- [ ] Stop running process

**Dependencies:**
- Viper.Exec with I/O capture
- Output parsing

---

### Phase 5: Advanced Editing

**Goal:** Professional editor features

**Duration:** 4-6 weeks

**Features:**
- [ ] Find and replace
- [ ] Find in files
- [ ] Multi-cursor editing
- [ ] Code folding
- [ ] Minimap
- [ ] Split editor view

**Dependencies:**
- FindReplaceBar widget
- Minimap widget
- CodeEditor enhancements

---

### Phase 6: Menu System

**Goal:** Full menu bar with all actions

**Duration:** 2-3 weeks

**Features:**
- [ ] Menu bar (File, Edit, View, Go, Run, Help)
- [ ] Context menus (right-click)
- [ ] Keyboard shortcut display
- [ ] Menu item icons

**Dependencies:**
- MenuBar widget (needs exposure)
- ContextMenu widget (needs exposure)

---

### Phase 7: Command Palette

**Goal:** Quick command access

**Duration:** 2-3 weeks

**Features:**
- [ ] Command palette (Ctrl+Shift+P)
- [ ] Quick file open (Ctrl+P)
- [ ] Go to symbol (Ctrl+Shift+O)
- [ ] Go to line (Ctrl+G)
- [ ] Fuzzy search

**Dependencies:**
- CommandPalette widget (needs exposure)

---

### Phase 8: Debugging

**Goal:** Integrated debugging

**Duration:** 6-8 weeks

**Features:**
- [ ] Breakpoint management
- [ ] Step debugging
- [ ] Variable inspection
- [ ] Call stack view
- [ ] Debug console
- [ ] Watch expressions

**Dependencies:**
- VM debug mode support
- Debug adapter protocol

---

### Phase 9: Extensions

**Goal:** Plugin system

**Duration:** 4-6 weeks

**Features:**
- [ ] Extension loading
- [ ] Extension API
- [ ] Extension settings
- [ ] Extension marketplace (basic)

**Dependencies:**
- Dynamic code loading (if possible)
- Or static plugin registration

---

### Phase 10: Polish

**Goal:** Production-ready quality

**Duration:** 4-6 weeks

**Features:**
- [ ] Settings UI
- [ ] Theme customization
- [ ] Performance optimization
- [ ] Error handling
- [ ] User documentation
- [ ] Keyboard navigation

---

## 13. File Structure

```
demos/zia/viperide/
├── main.zia                 # Entry point
├── ide_plans.md             # This document
│
├── core/                    # Core IDE services
│   ├── app.zia              # Application lifecycle
│   ├── commands.zia         # Command registry
│   ├── keybindings.zia      # Keyboard shortcuts
│   ├── settings.zia         # Settings manager
│   └── events.zia           # Event system
│
├── ui/                      # UI components
│   ├── main_window.zia      # Main window layout
│   ├── menu_bar.zia         # Menu bar
│   ├── toolbar.zia          # Toolbar
│   ├── sidebar.zia          # Sidebar container
│   ├── status_bar.zia       # Status bar
│   └── dialogs/             # Dialog implementations
│       ├── file_dialog.zia
│       ├── find_dialog.zia
│       └── settings_dialog.zia
│
├── editor/                  # Editor components
│   ├── editor_manager.zia   # Multi-editor management
│   ├── editor_tab.zia       # Single editor instance
│   ├── editor_group.zia     # Split editor group
│   └── minimap.zia          # Minimap widget
│
├── explorer/                # File explorer
│   ├── file_tree.zia        # Tree view
│   ├── file_item.zia        # Tree item
│   └── file_icons.zia       # Icon mapping
│
├── project/                 # Project management
│   ├── workspace.zia        # Workspace model
│   ├── project.zia          # Project file handling
│   └── recent.zia           # Recent files/folders
│
├── languages/               # Language services
│   ├── language_service.zia # Base interface
│   ├── zia_service.zia      # Zia language support
│   ├── basic_service.zia    # BASIC language support
│   └── highlighting.zia     # Syntax highlighting
│
├── build/                   # Build system
│   ├── build_manager.zia    # Build orchestration
│   ├── task_runner.zia      # Task execution
│   └── output_parser.zia    # Output parsing
│
├── debug/                   # Debugging
│   ├── debug_manager.zia    # Debug session
│   ├── breakpoints.zia      # Breakpoint management
│   └── variables.zia        # Variable inspection
│
├── search/                  # Search functionality
│   ├── find_replace.zia     # Find/replace
│   ├── file_search.zia      # Find in files
│   └── symbol_search.zia    # Symbol search
│
├── output/                  # Output panels
│   ├── output_panel.zia     # Output tab
│   ├── problems_panel.zia   # Problems list
│   └── debug_console.zia    # Debug output
│
├── extensions/              # Extension system
│   ├── extension_host.zia   # Extension loading
│   ├── extension_api.zia    # Public API
│   └── builtin/             # Built-in extensions
│       ├── zia_language/
│       └── basic_language/
│
├── themes/                  # Theme definitions
│   ├── dark.zia             # Dark theme
│   └── light.zia            # Light theme
│
└── resources/               # Static resources
    ├── icons/               # Icon files
    ├── fonts/               # Bundled fonts
    └── templates/           # File templates
```

---

## 14. Technical Challenges

### 14.1 Performance

| Challenge | Mitigation |
|-----------|------------|
| Large file editing | Virtual scrolling, lazy rendering |
| Syntax highlighting | Incremental tokenization |
| File tree with many files | Virtual tree, lazy loading |
| Multiple open editors | Tab lifecycle management |

### 14.2 Memory

| Challenge | Mitigation |
|-----------|------------|
| Large documents | Line-based storage, not full string |
| Undo history | Bounded history with memory limit |
| Multiple buffers | LRU cache for inactive tabs |

### 14.3 Responsiveness

| Challenge | Mitigation |
|-----------|------------|
| Long-running operations | Background tasks with progress |
| Compilation | Async compilation, cancelable |
| File search | Streaming results |

### 14.4 Cross-Platform

| Challenge | Mitigation |
|-----------|------------|
| File paths | Use Viper.Path for normalization |
| Line endings | Auto-detect, configurable |
| Keyboard shortcuts | Platform-aware bindings |

---

## 15. Success Metrics

### 15.1 Functionality Metrics

- [ ] Can create, edit, and save Zia files
- [ ] Can create, edit, and save BASIC files
- [ ] Can navigate project file tree
- [ ] Can compile and run programs
- [ ] Can view compiler errors with click-to-source
- [ ] Can search in files
- [ ] Can use keyboard shortcuts for all major actions

### 15.2 Performance Metrics

- [ ] 60 fps UI rendering
- [ ] < 100ms file open time (< 1MB files)
- [ ] < 500ms syntax highlighting (< 10K lines)
- [ ] < 1s project load time (< 1000 files)
- [ ] < 50MB memory baseline

### 15.3 Usability Metrics

- [ ] Intuitive for VS Code / mainstream IDE users
- [ ] All common operations accessible via keyboard
- [ ] Clear feedback for all actions
- [ ] Helpful error messages

### 15.4 Quality Metrics

- [ ] No crashes in normal operation
- [ ] No data loss (auto-save, recovery)
- [ ] Consistent UI behavior
- [ ] Comprehensive keyboard navigation

---

## Appendix A: Keyboard Shortcuts Reference

### File Operations
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| New File | Ctrl+N | Cmd+N |
| Open File | Ctrl+O | Cmd+O |
| Save | Ctrl+S | Cmd+S |
| Save As | Ctrl+Shift+S | Cmd+Shift+S |
| Close Tab | Ctrl+W | Cmd+W |

### Editing
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Undo | Ctrl+Z | Cmd+Z |
| Redo | Ctrl+Y | Cmd+Shift+Z |
| Cut | Ctrl+X | Cmd+X |
| Copy | Ctrl+C | Cmd+C |
| Paste | Ctrl+V | Cmd+V |
| Find | Ctrl+F | Cmd+F |
| Replace | Ctrl+H | Cmd+H |
| Comment | Ctrl+/ | Cmd+/ |

### Navigation
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Go to Line | Ctrl+G | Cmd+G |
| Go to File | Ctrl+P | Cmd+P |
| Command Palette | Ctrl+Shift+P | Cmd+Shift+P |
| Go to Definition | F12 | F12 |
| Go Back | Alt+Left | Cmd+[ |

### View
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Toggle Sidebar | Ctrl+B | Cmd+B |
| Toggle Output | Ctrl+` | Cmd+` |
| Zoom In | Ctrl+= | Cmd+= |
| Zoom Out | Ctrl+- | Cmd+- |

### Run/Debug
| Action | Windows/Linux | macOS |
|--------|---------------|-------|
| Run | F5 | F5 |
| Debug | Ctrl+F5 | Cmd+F5 |
| Stop | Shift+F5 | Shift+F5 |
| Toggle Breakpoint | F9 | F9 |
| Step Over | F10 | F10 |
| Step Into | F11 | F11 |

---

## Appendix B: Color Tokens for Syntax Highlighting

### Zia Tokens
| Token | Example | Suggested Color |
|-------|---------|-----------------|
| keyword | `func`, `if`, `return` | Purple |
| keyword.control | `if`, `else`, `while` | Purple |
| keyword.declaration | `var`, `final`, `func` | Purple |
| entity.name.type | `Player`, `List` | Cyan |
| entity.name.function | `start`, `update` | Yellow |
| variable | `count`, `name` | White |
| string | `"hello"` | Green |
| number | `42`, `3.14` | Orange |
| comment | `// comment` | Gray |
| operator | `+`, `-`, `==` | White |
| punctuation | `{`, `}`, `;` | White |

### BASIC Tokens
| Token | Example | Suggested Color |
|-------|---------|-----------------|
| keyword | `DIM`, `LET`, `IF` | Purple |
| keyword.control | `IF`, `FOR`, `WHILE` | Purple |
| function.builtin | `PRINT`, `INPUT` | Yellow |
| variable | `X`, `Name$` | White |
| string | `"hello"` | Green |
| number | `42`, `3.14` | Orange |
| comment | `REM comment`, `' comment` | Gray |
| operator | `+`, `-`, `=` | White |
| line.number | `10`, `20` | Gray |

---

## Appendix C: Sample .viperproj File

```json
{
    "name": "MyGame",
    "version": "0.1.0",
    "description": "A sample Zia game project",
    "language": "zia",
    "main": "src/main.zia",

    "sources": {
        "include": [
            "src/**/*.zia"
        ],
        "exclude": [
            "src/tests/**"
        ]
    },

    "build": {
        "output": "build/",
        "target": "vm",
        "emitIL": false,
        "optimizations": true
    },

    "run": {
        "program": "${main}",
        "arguments": [],
        "workingDirectory": "${projectRoot}",
        "environment": {}
    },

    "debug": {
        "program": "${main}",
        "stopOnEntry": false,
        "trace": false
    },

    "editor": {
        "tabSize": 4,
        "insertSpaces": true,
        "trimTrailingWhitespace": true,
        "insertFinalNewline": true
    }
}
```

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-14 | 0.1.0 | Initial comprehensive plan |

---

*This document is a living specification. Update as implementation progresses and requirements evolve.*
