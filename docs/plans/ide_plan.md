# ViperIDE — Complete Design Specification

**Project:** ViperIDE
**Language:** Zia (self-hosted on the Viper platform)
**Target Languages:** Zia, Viper Basic
**Source Location:** `demos/zia/viperide/`
**Status:** Design Complete
**Date:** 2026-01-27

---

## Table of Contents

1. [Vision & Design Philosophy](#1-vision--design-philosophy)
2. [Architecture Overview](#2-architecture-overview)
3. [Module / File Structure](#3-module--file-structure)
4. [UI Layout](#4-ui-layout)
5. [Data Models](#5-data-models)
6. [Core Components](#6-core-components)
7. [Key Algorithms](#7-key-algorithms)
8. [Configuration Formats](#8-configuration-formats)
9. [Default Keymap](#9-default-keymap)
10. [Implementation Phases](#10-implementation-phases)
11. [Risk Analysis](#11-risk-analysis)

---

## 1. Vision & Design Philosophy

### 1.1 Vision

ViperIDE is a professional-grade IDE for Zia and Viper Basic, written entirely in
Zia using the `Viper.GUI.*` widget library. It serves two purposes:

1. **Primary:** Provide a first-class development experience for Viper platform languages
2. **Showcase:** Prove the Viper platform can build complex, real-world desktop applications

The IDE targets the same feature class as VS Code or JetBrains — syntax highlighting,
autocomplete, project management, integrated build/debug, git integration, command
palette, find/replace, minimap, and a fully themeable UI.

### 1.2 Design Principles

| Principle | Rationale |
|-----------|-----------|
| **Polling event loop** | Viper.GUI uses an immediate-mode polling pattern (`Poll → check state → Render`), not callbacks. All UI logic lives in the main loop. |
| **Entity-based components** | Each subsystem is a Zia entity with clear state and methods. Entities communicate through shared state and method calls, not events or signals. |
| **Background threads for heavy work** | Compilation, git operations, file search, and file watching run on worker threads via `Viper.Threads.Thread.Start()` and `Viper.Threads.Channel` to keep the UI responsive. |
| **Subprocess integration** | The compiler (`zia`, `vbasic`) and git are invoked via `Viper.Exec.Capture()` / `Viper.Exec.CaptureArgs()`. Parse their stderr/stdout for diagnostics. |
| **JSON configuration** | All settings stored as JSON via `Viper.Text.Json`. Human-readable, easy to edit outside the IDE. |
| **File budget: ~65 files** | Zia supports up to 100 files per `bind` compilation unit (50 depth). We target ~65 to leave headroom. |
| **No try/catch** | Zia has no exception handling. Errors are communicated through return values, status flags, and `Viper.Diagnostics.Trap()` for fatal conditions. |

### 1.3 Target Users

- **Beginners** learning Zia or BASIC
- **Hobbyists** building games and apps on the Viper platform
- **Educators** teaching programming
- **Viper contributors** developing the platform itself

### 1.4 Non-Goals (v1.0)

- Remote development / SSH
- Plugin/extension system (future)
- Visual drag-and-drop UI designer
- Mobile platform support
- Collaborative editing

---

## 2. Architecture Overview

### 2.1 System Architecture

```
+-----------------------------------------------------------------------+
|                            ViperIDE                                    |
|-----------------------------------------------------------------------|
|                                                                       |
|  main.zia (entry point + event loop)                                  |
|    |                                                                  |
|    +-- AppShell (UI container, layout, theme)                         |
|    |     |                                                            |
|    |     +-- MenuBar, Toolbar, StatusBar                              |
|    |     +-- SplitPane (sidebar | editor area | panel)                |
|    |     +-- CommandPalette, FindBar                                  |
|    |                                                                  |
|    +-- DocumentManager (open files, tabs, dirty tracking)             |
|    +-- EditorEngine (CodeEditor widget, highlighting, folding)        |
|    +-- ProjectManager (file tree, workspace, watcher)                 |
|    +-- BuildSystem (compile, run, error parsing)                      |
|    +-- Debugger (breakpoints, stepping, variable inspection)          |
|    +-- GitIntegration (status, diff, commit, branch)                  |
|    +-- AutocompleteEngine (symbol index, popup)                       |
|    +-- FindReplace (in-file + project-wide search)                    |
|    +-- CommandSystem (command registry, shortcuts, palette)            |
|    +-- SettingsManager (load/save JSON config)                        |
|    +-- TerminalPanel (build output, program output)                   |
|                                                                       |
|  ---- Background Threads ----                                         |
|    +-- FileWatcherThread (Viper.IO.Watcher)                           |
|    +-- BuildThread (Viper.Exec.Capture)                               |
|    +-- GitThread (Viper.Exec.Capture)                                 |
|    +-- SearchThread (file content search)                             |
|                                                                       |
+-----------------------------------------------------------------------+
|                     Viper Runtime Layer                                |
|  Viper.GUI.*  |  Viper.IO.*  |  Viper.Exec.*  |  Viper.Threads.*    |
|  Viper.Text.* |  Viper.Collections.* |  Viper.String.*               |
+-----------------------------------------------------------------------+
```

### 2.2 Event Loop Pattern

The entire application is driven by a single polling loop in `main.zia`, following
the pattern established by VEdit:

```zia
func main() {
    var app = Viper.GUI.App.New("ViperIDE", 1280, 800);
    Viper.GUI.Theme.SetDark();

    // Initialize all components
    var shell    = AppShell.New(app);
    var docs     = DocumentManager.New();
    var editor   = EditorEngine.New(shell.editorArea);
    var project  = ProjectManager.New(shell.sidebarArea);
    var build    = BuildSystem.New();
    var commands  = CommandSystem.New(shell.palette);
    var settings = SettingsManager.New();
    // ... more components ...

    // Register commands & shortcuts
    commands.RegisterAll();

    // Main loop
    while app.ShouldClose == 0 {
        app.Poll();

        // 1. Check background thread results (non-blocking)
        build.CheckResults();
        project.CheckWatcher();

        // 2. Handle menu/toolbar/shortcut actions
        shell.HandleMenus(commands);
        shell.HandleToolbar(commands);
        commands.HandleShortcuts();
        commands.HandlePalette();

        // 3. Handle document/editor state
        docs.HandleTabChanges(shell.tabBar, editor);
        editor.HandleGutterClicks();
        editor.UpdateHighlighting();

        // 4. Handle panels (file tree, find, terminal)
        project.HandleTreeClicks(docs, editor);
        // ... more panel handling ...

        // 5. Update status bar
        shell.UpdateStatus(editor, docs, project);

        app.Render();
    }

    // Cleanup
    settings.Save();
    app.Destroy();
}
```

### 2.3 Threading Model

```
Main Thread (UI)                    Worker Threads
===================                 ===================
Poll() / Render()                   BuildThread
Handle user input                     - Viper.Exec.Capture("zia ...")
Check Channel (non-blocking)          - Send result via Channel
Update widgets
                                    GitThread
                                      - Viper.Exec.Capture("git ...")
                                      - Send result via Channel

                                    FileWatcherThread
                                      - Viper.IO.Watcher.Poll()
                                      - Send events via Channel

                                    SearchThread
                                      - Read files, match patterns
                                      - Send results via Channel
```

**Thread communication:** `Viper.Threads.Channel.New(capacity)` for bounded channels.
The main thread calls `Channel.TryRecv()` each frame (non-blocking) to check for
results from worker threads.

### 2.4 Component Lifecycle

Every major component follows this pattern:

```zia
entity ComponentName {
    // State fields
    expose ...

    // Constructor
    func New(...) -> ComponentName { ... }

    // Per-frame update (called from main loop)
    func Update(...) { ... }

    // Cleanup
    func Destroy() { ... }
}
```

---

## 3. Module / File Structure

### 3.1 Directory Layout

All source files live in `demos/zia/viperide/`. The main entry point binds all
other files using Zia's `bind` directive.

```
demos/zia/viperide/
  main.zia                          # Entry point, event loop
  app_shell.zia                     # Top-level UI layout

  # --- Core ---
  core/
    document.zia                    # Document model (buffer, path, dirty flag)
    document_manager.zia            # Open document collection, tab sync
    project.zia                     # Project model (root path, files, config)
    project_manager.zia             # File tree, workspace operations
    settings.zia                    # Settings model + persistence
    settings_manager.zia            # Settings UI, load/save
    recent_files.zia                # MRU file list
    recent_projects.zia             # MRU project list

  # --- Editor ---
  editor/
    editor_engine.zia               # CodeEditor widget wrapper, main editing
    editor_tabs.zia                 # Tab bar management, tab ↔ document sync
    syntax_zia.zia                  # Zia syntax highlighting rules
    syntax_basic.zia                # BASIC syntax highlighting rules
    syntax_theme.zia                # Token color definitions
    folding.zia                     # Code folding region detection
    autocomplete.zia                # Completion engine + popup
    symbol_index.zia                # Symbol table for go-to-definition
    bracket_match.zia               # Bracket/paren matching
    snippets.zia                    # Code snippet definitions + expansion
    hover_info.zia                  # Hover tooltip content generation

  # --- Search ---
  search/
    find_replace.zia                # In-editor find/replace (FindBar wrapper)
    find_in_files.zia               # Project-wide search
    go_to_line.zia                  # Ctrl+G dialog
    go_to_symbol.zia                # Ctrl+Shift+O symbol navigation
    go_to_file.zia                  # Ctrl+P quick file open

  # --- Build ---
  build/
    build_system.zia                # Compile/run orchestration
    build_config.zia                # Build configuration (compiler path, flags)
    error_parser.zia                # Parse compiler stderr into diagnostics
    output_panel.zia                # Build output display
    run_config.zia                  # Run configurations (args, env vars)

  # --- Debug ---
  debug/
    debugger.zia                    # Debug session management
    breakpoints.zia                 # Breakpoint model + gutter markers
    watch_panel.zia                 # Variable watch expressions
    call_stack.zia                  # Call stack display
    debug_toolbar.zia               # Step/continue/stop controls

  # --- Git ---
  git/
    git_integration.zia             # Git command execution
    git_status.zia                  # File status (modified, untracked, etc.)
    git_diff.zia                    # Diff display
    git_branch.zia                  # Branch management
    git_commit.zia                  # Commit dialog
    git_log.zia                     # Commit history

  # --- UI ---
  ui/
    command_system.zia              # Command registry + palette binding
    command_defs.zia                # All command definitions
    keybindings.zia                 # Shortcut → command mapping
    menu_builder.zia                # Menu bar construction
    toolbar_builder.zia             # Toolbar construction
    statusbar_manager.zia           # Status bar sections + updates
    sidebar.zia                     # Sidebar panel switching
    breadcrumb_manager.zia          # Path breadcrumb for current file
    minimap_manager.zia             # Minimap binding + markers
    notification.zia                # Toast notification helpers
    welcome_tab.zia                 # Welcome/start page content
    about_dialog.zia                # About dialog

  # --- Services ---
  services/
    file_watcher.zia                # Background file system watcher
    file_utils.zia                  # File I/O helpers (read, write, detect encoding)
    string_utils.zia                # String manipulation helpers
    path_utils.zia                  # Path manipulation helpers
    json_config.zia                 # JSON config read/write helpers
    thread_helpers.zia              # Thread/channel utility functions
    clipboard_manager.zia           # Clipboard integration

  # --- Language ---
  lang/
    language_registry.zia           # Language ↔ file extension mapping
    token_types.zia                 # Token type constants
    indent_rules.zia                # Auto-indent rules per language
    comment_rules.zia               # Toggle-comment rules per language
```

### 3.2 File Count

| Subsystem | Files | Description |
|-----------|-------|-------------|
| Root | 2 | `main.zia`, `app_shell.zia` |
| Core | 8 | Document, project, settings, recents |
| Editor | 11 | Editor engine, syntax, folding, autocomplete, symbols |
| Search | 5 | Find/replace, find-in-files, go-to |
| Build | 5 | Build system, error parsing, output, run config |
| Debug | 5 | Debugger, breakpoints, watch, call stack |
| Git | 6 | Status, diff, branch, commit, log |
| UI | 12 | Commands, menus, toolbar, statusbar, sidebar, minimap |
| Services | 7 | File watcher, utilities, clipboard |
| Lang | 4 | Language registry, token types, indent/comment rules |
| **Total** | **65** | Within 100-file bind limit |

### 3.3 Bind Structure

`main.zia` is the compilation entry point:

```zia
module viperide;

bind "./app_shell";
bind "./core/document";
bind "./core/document_manager";
bind "./core/project";
bind "./core/project_manager";
bind "./core/settings";
bind "./core/settings_manager";
bind "./core/recent_files";
bind "./core/recent_projects";
bind "./editor/editor_engine";
bind "./editor/editor_tabs";
bind "./editor/syntax_zia";
bind "./editor/syntax_basic";
bind "./editor/syntax_theme";
bind "./editor/folding";
bind "./editor/autocomplete";
bind "./editor/symbol_index";
bind "./editor/bracket_match";
bind "./editor/snippets";
bind "./editor/hover_info";
bind "./search/find_replace";
bind "./search/find_in_files";
bind "./search/go_to_line";
bind "./search/go_to_symbol";
bind "./search/go_to_file";
bind "./build/build_system";
bind "./build/build_config";
bind "./build/error_parser";
bind "./build/output_panel";
bind "./build/run_config";
bind "./debug/debugger";
bind "./debug/breakpoints";
bind "./debug/watch_panel";
bind "./debug/call_stack";
bind "./debug/debug_toolbar";
bind "./git/git_integration";
bind "./git/git_status";
bind "./git/git_diff";
bind "./git/git_branch";
bind "./git/git_commit";
bind "./git/git_log";
bind "./ui/command_system";
bind "./ui/command_defs";
bind "./ui/keybindings";
bind "./ui/menu_builder";
bind "./ui/toolbar_builder";
bind "./ui/statusbar_manager";
bind "./ui/sidebar";
bind "./ui/breadcrumb_manager";
bind "./ui/minimap_manager";
bind "./ui/notification";
bind "./ui/welcome_tab";
bind "./ui/about_dialog";
bind "./services/file_watcher";
bind "./services/file_utils";
bind "./services/string_utils";
bind "./services/path_utils";
bind "./services/json_config";
bind "./services/thread_helpers";
bind "./services/clipboard_manager";
bind "./lang/language_registry";
bind "./lang/token_types";
bind "./lang/indent_rules";
bind "./lang/comment_rules";

func main() {
    // ... event loop (see Section 2.2)
}
```

---

## 4. UI Layout

### 4.1 Widget Hierarchy

```
App ("ViperIDE", 1280, 800)
 |
 Root
 └── mainVBox (VBox, spacing=0, padding=0)
      |
      ├── menuBar (MenuBar)
      |    ├── fileMenu: New, Open, Open Folder, Save, Save As, Save All, Close, Exit
      |    ├── editMenu: Undo, Redo, Cut, Copy, Paste, Find, Replace, Find in Files
      |    ├── viewMenu: Command Palette, Explorer, Minimap, Status Bar, Word Wrap
      |    ├── goMenu: Go to File, Go to Symbol, Go to Line, Go to Definition, Back, Forward
      |    ├── buildMenu: Build, Build & Run, Run, Stop, Clean, Build Config
      |    ├── debugMenu: Start Debug, Stop, Step Over, Step Into, Step Out, Toggle Breakpoint
      |    ├── gitMenu: Status, Commit, Push, Pull, Branch, Log, Diff
      |    └── helpMenu: Welcome, Keyboard Shortcuts, About
      |
      ├── toolbar (Toolbar)
      |    ├── btnNew, btnOpen, btnSave, btnSaveAll
      |    ├── separator
      |    ├── btnUndo, btnRedo
      |    ├── separator
      |    ├── btnBuild, btnRun, btnStop
      |    ├── separator
      |    ├── btnDebug, btnStepOver, btnStepInto
      |    └── separator, btnGitCommit
      |
      ├── breadcrumb (Breadcrumb)
      |    └── project > folder > file > symbol
      |
      ├── mainSplit (SplitPane, horizontal, position=0.2)
      |    |
      |    ├── First: sidebarVBox (VBox)
      |    |    ├── sidebarTabs (TabBar — Explorer, Search, Git, Debug)
      |    |    └── sidebarContent (VBox)
      |    |         └── [activePanel: TreeView | SearchPanel | GitPanel | DebugPanel]
      |    |
      |    └── Second: centerSplit (SplitPane, vertical, position=0.75)
      |         |
      |         ├── First: editorArea (VBox)
      |         |    ├── editorTabBar (TabBar — open files)
      |         |    ├── findBar (FindBar — hidden by default)
      |         |    └── editorSplit (HBox)
      |         |         ├── codeEditor (CodeEditor)
      |         |         └── minimap (Minimap, bound to codeEditor)
      |         |
      |         └── Second: panelArea (VBox)
      |              ├── panelTabs (TabBar — Output, Terminal, Problems, Debug Console)
      |              └── panelContent (VBox)
      |                   └── [activePanel: OutputText | TerminalView | ProblemsList | DebugConsole]
      |
      └── statusBar (StatusBar)
           ├── left: branch icon + branch name, errors/warnings count
           ├── center: file path
           └── right: line:col, language, encoding, indentation
```

### 4.2 Layout Construction (Zia Pseudocode)

```zia
entity AppShell {
    expose app: obj;          // Viper.GUI.App handle
    expose mainVBox: obj;
    expose menuBar: obj;
    expose toolbar: obj;
    expose breadcrumb: obj;
    expose mainSplit: obj;
    expose sidebarVBox: obj;
    expose sidebarTree: obj;
    expose centerSplit: obj;
    expose editorArea: obj;
    expose editorTabBar: obj;
    expose findBar: obj;
    expose codeEditor: obj;
    expose minimap: obj;
    expose panelArea: obj;
    expose panelTabBar: obj;
    expose statusBar: obj;
    expose palette: obj;      // CommandPalette

    // Menu items (for WasClicked polling)
    expose miFileNew: obj;
    expose miFileOpen: obj;
    expose miFileSave: obj;
    // ... all menu items ...

    // Toolbar items
    expose tbNew: obj;
    expose tbOpen: obj;
    expose tbSave: obj;
    // ... all toolbar items ...

    func New(title: String, width: Int, height: Int) -> AppShell {
        var self = AppShell {};

        self.app = Viper.GUI.App.New(title, width, height);
        Viper.GUI.Theme.SetDark();

        // Main container
        self.mainVBox = Viper.GUI.VBox.New();
        self.mainVBox.SetSpacing(0.0);
        self.mainVBox.SetPadding(0.0);
        var root = self.app.Root;
        root.AddChild(self.mainVBox);

        // Menu bar
        self.menuBar = Viper.GUI.MenuBar.New(self.mainVBox);
        self.buildFileMenu();
        self.buildEditMenu();
        self.buildViewMenu();
        self.buildGoMenu();
        self.buildBuildMenu();
        self.buildDebugMenu();
        self.buildGitMenu();
        self.buildHelpMenu();

        // Toolbar
        self.toolbar = Viper.GUI.Toolbar.New(self.mainVBox);
        self.buildToolbar();

        // Breadcrumb
        self.breadcrumb = Viper.GUI.Breadcrumb.New(self.mainVBox);
        self.breadcrumb.SetSeparator(">");

        // Main horizontal split: sidebar | editor+panel
        self.mainSplit = Viper.GUI.SplitPane.New(self.mainVBox, 0);  // 0 = horizontal
        self.mainSplit.SetPosition(0.2);

        // Sidebar
        self.sidebarVBox = Viper.GUI.VBox.New();
        var sideFirst = self.mainSplit.First;
        sideFirst.AddChild(self.sidebarVBox);
        self.sidebarTree = Viper.GUI.TreeView.New(self.sidebarVBox);

        // Center: vertical split for editor | bottom panel
        self.centerSplit = Viper.GUI.SplitPane.New(self.mainSplit.Second, 1);  // 1 = vertical
        self.centerSplit.SetPosition(0.75);

        // Editor area
        self.editorArea = Viper.GUI.VBox.New();
        var centerFirst = self.centerSplit.First;
        centerFirst.AddChild(self.editorArea);

        self.editorTabBar = Viper.GUI.TabBar.New(self.editorArea);

        self.findBar = Viper.GUI.FindBar.New(self.editorArea);
        self.findBar.SetVisible(0);

        self.codeEditor = Viper.GUI.CodeEditor.New(self.editorArea);
        self.codeEditor.SetShowLineNumbers(1);

        // Minimap
        self.minimap = Viper.GUI.Minimap.New(self.editorArea);
        self.minimap.BindEditor(self.codeEditor);
        self.minimap.SetWidth(80);

        // Panel area (output/terminal)
        self.panelArea = Viper.GUI.VBox.New();
        var centerSecond = self.centerSplit.Second;
        centerSecond.AddChild(self.panelArea);
        self.panelTabBar = Viper.GUI.TabBar.New(self.panelArea);

        // Status bar
        self.statusBar = Viper.GUI.StatusBar.New(self.mainVBox);

        // Command palette (overlay, hidden by default)
        self.palette = Viper.GUI.CommandPalette.New(self.mainVBox);
        self.palette.SetPlaceholder("Type a command...");

        return self;
    }
}
```

### 4.3 Theme

Default theme is dark (`Viper.GUI.Theme.SetDark()`). Token colors are applied via
`CodeEditor.SetTokenColor(tokenType, color)`.

Default dark token colors:

| Token | Color | Hex |
|-------|-------|-----|
| Keyword | Soft blue | `0xFF569CD6` |
| String | Orange | `0xFFCE9178` |
| Number | Light green | `0xFFB5CEA8` |
| Comment | Green-gray | `0xFF6A9955` |
| Type | Teal | `0xFF4EC9B0` |
| Function | Light yellow | `0xFFDCDCAA` |
| Operator | Light gray | `0xFFD4D4D4` |
| Variable | Light blue | `0xFF9CDCFE` |
| Punctuation | Gray | `0xFFCCCCCC` |
| Error underline | Red | `0xFFFF0000` |
| Warning underline | Yellow | `0xFFFFCC00` |

---

## 5. Data Models

### 5.1 Document

Represents a single open file in the editor.

```zia
entity Document {
    expose filePath: String;      // Absolute path ("" for untitled)
    expose fileName: String;      // Display name
    expose content: String;       // Full text content
    expose language: String;      // "zia", "basic", "text", etc.
    expose isModified: Bool;      // Has unsaved changes
    expose isNew: Bool;           // Never been saved to disk
    expose tabHandle: obj;        // Viper.GUI.Tab widget reference
    expose cursorLine: Int;       // Last known cursor line
    expose cursorCol: Int;        // Last known cursor column
    expose scrollLine: Int;       // Last known scroll position
    expose encoding: String;      // "utf-8" (default)
    expose lineEnding: String;    // "\n" (unix) or "\r\n" (windows)
    expose lastModifiedOnDisk: Int; // Viper.IO.File.Modified() timestamp
}
```

### 5.2 Project

Represents a workspace/folder.

```zia
entity Project {
    expose rootPath: String;       // Absolute path to project root
    expose name: String;           // Folder name
    expose files: obj;             // Seq of file paths (relative)
    expose buildConfig: BuildConfig;
    expose isOpen: Bool;
}
```

### 5.3 BuildConfig

```zia
entity BuildConfig {
    expose compilerPath: String;   // "zia" or "vbasic" (or absolute path)
    expose mainFile: String;       // Entry point file
    expose outputName: String;     // Output binary name
    expose flags: String;          // Additional compiler flags
    expose runArgs: String;        // Arguments passed to program when running
    expose language: String;       // "zia" or "basic"
}
```

### 5.4 Diagnostic (compiler error/warning)

```zia
entity Diagnostic {
    expose filePath: String;
    expose line: Int;
    expose column: Int;
    expose severity: Int;      // 0=error, 1=warning, 2=info
    expose message: String;
    expose code: String;       // Error code (if any)
}
```

### 5.5 Breakpoint

```zia
entity Breakpoint {
    expose filePath: String;
    expose line: Int;
    expose isEnabled: Bool;
    expose condition: String;   // Conditional breakpoint expression ("" = always)
    expose hitCount: Int;       // Number of times hit
}
```

### 5.6 SymbolEntry (for autocomplete / go-to-definition)

```zia
entity SymbolEntry {
    expose name: String;
    expose kind: Int;          // 0=func, 1=entity, 2=var, 3=field, 4=method, 5=value, 6=interface
    expose filePath: String;
    expose line: Int;
    expose detail: String;     // Signature or type info for display
    expose scope: String;      // Parent scope name ("" for module-level)
}
```

### 5.7 GitFileStatus

```zia
entity GitFileStatus {
    expose filePath: String;   // Relative to project root
    expose status: String;     // "M", "A", "D", "?", "R", "C", "U"
    expose staged: Bool;
}
```

### 5.8 SearchResult

```zia
entity SearchResult {
    expose filePath: String;
    expose line: Int;
    expose column: Int;
    expose matchLength: Int;
    expose lineText: String;   // Full line content for preview
}
```

### 5.9 Command

```zia
entity Command {
    expose id: String;         // "file.save", "edit.undo", etc.
    expose label: String;      // "Save File"
    expose category: String;   // "File", "Edit", "View", etc.
    expose shortcut: String;   // "Ctrl+S" (display only; actual binding is separate)
}
```

### 5.10 Settings

```zia
entity Settings {
    // Editor
    expose tabSize: Int;              // Default: 4
    expose insertSpaces: Bool;        // Default: true (spaces, not tabs)
    expose wordWrap: Bool;            // Default: false
    expose showLineNumbers: Bool;     // Default: true
    expose showMinimap: Bool;         // Default: true
    expose minimapWidth: Int;         // Default: 80
    expose fontSize: Float;           // Default: 14.0
    expose fontPath: String;          // Default: "" (use system font)

    // Theme
    expose theme: String;             // "dark" or "light"

    // Build
    expose ziaCompilerPath: String;   // Default: "zia"
    expose basicCompilerPath: String; // Default: "vbasic"
    expose autoBuildOnSave: Bool;     // Default: false
    expose buildBeforeRun: Bool;      // Default: true

    // Git
    expose gitPath: String;           // Default: "git"
    expose autoFetchInterval: Int;    // Default: 300 (seconds), 0 = disabled

    // UI
    expose sidebarVisible: Bool;      // Default: true
    expose sidebarWidth: Float;       // Default: 0.2 (fraction)
    expose panelVisible: Bool;        // Default: true
    expose panelHeight: Float;        // Default: 0.25 (fraction)
    expose toolbarVisible: Bool;      // Default: true
    expose statusBarVisible: Bool;    // Default: true
    expose breadcrumbVisible: Bool;   // Default: true

    // Files
    expose autoSaveInterval: Int;     // Default: 0 (disabled), in seconds
    expose trimTrailingWhitespace: Bool;  // Default: false
    expose insertFinalNewline: Bool;  // Default: true
    expose excludePatterns: String;   // Default: ".git,build,*.o"

    // Recent
    expose maxRecentFiles: Int;       // Default: 20
    expose maxRecentProjects: Int;    // Default: 10
    expose lastProjectPath: String;   // Last opened project
}
```

---

## 6. Core Components

### 6.1 Editor Engine

**File:** `editor/editor_engine.zia`
**Responsibility:** Wraps the `Viper.GUI.CodeEditor` widget with IDE-level editing
features: syntax highlighting, code folding, bracket matching, and gutter icon
management.

```zia
entity EditorEngine {
    expose editor: obj;        // Viper.GUI.CodeEditor widget handle
    expose currentDoc: Document;

    func New(parent: obj) -> EditorEngine { ... }

    // Load document content into the editor widget
    func LoadDocument(doc: Document) { ... }

    // Save editor content back to document
    func SaveToDocument(doc: Document) { ... }

    // Apply syntax highlighting for the current language
    func ApplyHighlighting(language: String) { ... }

    // Detect and apply fold regions
    func UpdateFolding() { ... }

    // Set gutter icons for diagnostics
    func SetDiagnosticMarkers(diagnostics: obj) { ... }

    // Set gutter icons for breakpoints
    func SetBreakpointMarkers(breakpoints: obj) { ... }

    // Handle gutter click (toggle breakpoint)
    func HandleGutterClicks() -> Int { ... }

    // Get current cursor position
    func GetCursorLine() -> Int { ... }
    func GetCursorCol() -> Int { ... }

    // Navigate to a specific line
    func GoToLine(line: Int) { ... }

    // Check if modified
    func IsModified() -> Bool { ... }
}
```

**Key API usage:**
- `Viper.GUI.CodeEditor.New(parent)` — Create editor widget
- `CodeEditor.SetText(str)` / `CodeEditor.Text` — Get/set content
- `CodeEditor.SetLanguage("zia")` — Set syntax language
- `CodeEditor.SetTokenColor(tokenType, color)` — Set token colors
- `CodeEditor.SetCursor(line, col)` — Position cursor
- `CodeEditor.ScrollToLine(line)` — Scroll to line
- `CodeEditor.SetGutterIcon(line, pixelsObj, iconType)` — Set gutter marker
- `CodeEditor.ClearGutterIcons(iconType)` — Clear markers
- `CodeEditor.WasGutterClicked()` / `GetGutterClickLine()` — Detect gutter clicks
- `CodeEditor.AddFoldRegion(startLine, endLine)` — Define fold regions
- `CodeEditor.Fold(line)` / `Unfold(line)` / `IsFolded(line)` — Manage folding
- `CodeEditor.AddHighlight(line, col, endLine, endCol)` — Highlight range
- `CodeEditor.IsModified()` / `ClearModified()` — Track modifications
- `CodeEditor.SetShowLineNumbers(1)` — Show line numbers
- `CodeEditor.LineCount` — Get line count

### 6.2 Document Manager

**File:** `core/document_manager.zia`
**Responsibility:** Manages the collection of open documents, synchronizes with the
tab bar, handles file open/close/save operations.

```zia
entity DocumentManager {
    expose documents: obj;     // Seq of Document
    expose activeIndex: Int;   // Index of active document (-1 if none)
    expose tabBar: obj;        // Reference to editor TabBar widget

    func New() -> DocumentManager { ... }

    // Open a file (or activate if already open)
    func OpenFile(path: String, tabBar: obj) -> Document { ... }

    // Create a new untitled document
    func NewDocument(tabBar: obj) -> Document { ... }

    // Save the active document
    func SaveActive() -> Bool { ... }

    // Save a document to a specific path
    func SaveAs(doc: Document, path: String) -> Bool { ... }

    // Save all modified documents
    func SaveAll() -> Int { ... }

    // Close a document (prompt if modified)
    func CloseDocument(index: Int) -> Bool { ... }

    // Get the active document
    func GetActive() -> Document { ... }

    // Switch to a document by index
    func SetActive(index: Int, editor: EditorEngine) { ... }

    // Handle tab bar changes (user clicked a different tab)
    func HandleTabChanges(tabBar: obj, editor: EditorEngine) { ... }

    // Check for external modifications
    func CheckExternalChanges() { ... }

    // Find document by path
    func FindByPath(path: String) -> Int { ... }
}
```

**File operations use:**
- `Viper.IO.File.ReadAllText(path)` — Read file content
- `Viper.IO.File.WriteAllText(path, content)` — Write file content
- `Viper.IO.File.Exists(path)` — Check if file exists
- `Viper.IO.File.Modified(path)` — Get modification timestamp
- `Viper.IO.Path.Name(path)` — Extract filename
- `Viper.IO.Path.Ext(path)` — Extract extension
- `Viper.IO.Path.Stem(path)` — Extract name without extension

### 6.3 Syntax Highlighting

**Files:** `editor/syntax_zia.zia`, `editor/syntax_basic.zia`, `editor/syntax_theme.zia`
**Responsibility:** Define syntax rules for Zia and BASIC. The CodeEditor widget has
built-in tokenization via `SetLanguage()` — our job is to set the language and
configure token colors.

```zia
// syntax_theme.zia — token color application

// Token type constants (match the CodeEditor's built-in token IDs)
var TOKEN_KEYWORD   = 0;
var TOKEN_STRING    = 1;
var TOKEN_NUMBER    = 2;
var TOKEN_COMMENT   = 3;
var TOKEN_TYPE      = 4;
var TOKEN_FUNCTION  = 5;
var TOKEN_OPERATOR  = 6;
var TOKEN_VARIABLE  = 7;
var TOKEN_PUNCT     = 8;

func ApplyDarkTheme(editor: obj) {
    editor.SetTokenColor(TOKEN_KEYWORD,  0xFF569CD6);  // Soft blue
    editor.SetTokenColor(TOKEN_STRING,   0xFFCE9178);  // Orange
    editor.SetTokenColor(TOKEN_NUMBER,   0xFFB5CEA8);  // Light green
    editor.SetTokenColor(TOKEN_COMMENT,  0xFF6A9955);  // Green-gray
    editor.SetTokenColor(TOKEN_TYPE,     0xFF4EC9B0);  // Teal
    editor.SetTokenColor(TOKEN_FUNCTION, 0xFFDCDCAA);  // Light yellow
    editor.SetTokenColor(TOKEN_OPERATOR, 0xFFD4D4D4);  // Light gray
    editor.SetTokenColor(TOKEN_VARIABLE, 0xFF9CDCFE);  // Light blue
    editor.SetTokenColor(TOKEN_PUNCT,    0xFFCCCCCC);  // Gray
}

func ApplyLightTheme(editor: obj) {
    editor.SetTokenColor(TOKEN_KEYWORD,  0xFF0000FF);  // Blue
    editor.SetTokenColor(TOKEN_STRING,   0xFFA31515);  // Dark red
    editor.SetTokenColor(TOKEN_NUMBER,   0xFF098658);  // Green
    editor.SetTokenColor(TOKEN_COMMENT,  0xFF008000);  // Green
    editor.SetTokenColor(TOKEN_TYPE,     0xFF267F99);  // Teal
    editor.SetTokenColor(TOKEN_FUNCTION, 0xFF795E26);  // Brown
    editor.SetTokenColor(TOKEN_OPERATOR, 0xFF000000);  // Black
    editor.SetTokenColor(TOKEN_VARIABLE, 0xFF001080);  // Dark blue
    editor.SetTokenColor(TOKEN_PUNCT,    0xFF000000);  // Black
}
```

```zia
// syntax_zia.zia — Zia language configuration

func SetupZiaLanguage(editor: obj) {
    editor.SetLanguage("zia");
    // The CodeEditor widget handles Zia tokenization natively.
    // We just need to set the language identifier.
}
```

```zia
// syntax_basic.zia — BASIC language configuration

func SetupBasicLanguage(editor: obj) {
    editor.SetLanguage("basic");
}
```

### 6.4 Autocomplete Engine

**Files:** `editor/autocomplete.zia`, `editor/symbol_index.zia`
**Responsibility:** Build and maintain a symbol index from source files, provide
completion suggestions based on cursor context.

**Design approach:** Since Zia has no reflection or eval, we build the symbol index
by parsing source files textually. We scan for `func`, `entity`, `value`,
`interface`, `var`, `expose`, and `hide` declarations using pattern matching.

```zia
entity SymbolIndex {
    expose symbols: obj;       // Seq of SymbolEntry
    expose fileTimestamps: obj; // Map<String, Int> — path → last indexed timestamp

    func New() -> SymbolIndex { ... }

    // Index all .zia/.bas files in the project
    func IndexProject(rootPath: String) { ... }

    // Index a single file (called when file is saved)
    func IndexFile(filePath: String) { ... }

    // Remove symbols from a deleted file
    func RemoveFile(filePath: String) { ... }

    // Query symbols matching a prefix
    func FindByPrefix(prefix: String, limit: Int) -> obj { ... }

    // Find symbol by exact name (for go-to-definition)
    func FindByName(name: String) -> obj { ... }

    // Find all symbols in a file (for go-to-symbol-in-file)
    func FindInFile(filePath: String) -> obj { ... }
}

entity AutocompleteEngine {
    expose index: SymbolIndex;
    expose popup: obj;         // ListBox widget for completion popup
    expose isVisible: Bool;
    expose currentWord: String;

    func New(parent: obj) -> AutocompleteEngine { ... }

    // Trigger completion at cursor position
    func Trigger(editor: obj, cursorLine: Int, cursorCol: Int) { ... }

    // Update completion list as user types
    func Update(editor: obj) { ... }

    // Accept the selected completion
    func Accept(editor: obj) { ... }

    // Dismiss the popup
    func Dismiss() { ... }
}
```

**Symbol extraction algorithm (see Section 7.1).**

**Viper.* API completions:** In addition to user-defined symbols, the autocomplete
engine provides completions for the 800+ `Viper.*` runtime functions. These are
loaded from a static data table compiled into the symbol index at startup.

### 6.5 Project Manager

**Files:** `core/project.zia`, `core/project_manager.zia`
**Responsibility:** Manages the project file tree, handles folder open/close,
creates/deletes/renames files and folders, monitors for external changes.

```zia
entity ProjectManager {
    expose project: Project;
    expose treeView: obj;          // Viper.GUI.TreeView widget
    expose watcher: obj;           // Viper.IO.Watcher handle
    expose watcherChannel: obj;    // Channel for file events
    expose treeNodeMap: obj;       // Map<String, obj> — path → tree node

    func New(sidebarArea: obj) -> ProjectManager { ... }

    // Open a project folder
    func OpenFolder(path: String) { ... }

    // Close the current project
    func CloseProject() { ... }

    // Rebuild the tree from disk
    func RefreshTree() { ... }

    // Handle tree node clicks (open file)
    func HandleTreeClicks(docs: DocumentManager, editor: EditorEngine) { ... }

    // Start background file watcher
    func StartWatcher() { ... }

    // Check watcher channel for events (non-blocking, call each frame)
    func CheckWatcher() { ... }

    // File operations
    func CreateFile(parentPath: String, name: String) { ... }
    func CreateFolder(parentPath: String, name: String) { ... }
    func RenameItem(oldPath: String, newName: String) { ... }
    func DeleteItem(path: String) { ... }

    // Build the tree recursively
    func BuildTree(dirPath: String, parentNode: obj) { ... }
}
```

**Tree building uses:**
- `Viper.IO.Dir.DirsSeq(path)` — List subdirectories
- `Viper.IO.Dir.FilesSeq(path)` — List files
- `Viper.IO.Path.Join(parent, child)` — Construct paths
- `Viper.GUI.TreeView.AddNode(tree, parent, label)` — Add tree nodes

**File watching uses:**
- `Viper.IO.Watcher.new(path)` — Create watcher
- `Watcher.Start()` / `Watcher.Stop()` — Start/stop monitoring
- `Watcher.Poll()` — Check for events (0 = no event)
- `Watcher.EventType()` — CREATED/MODIFIED/DELETED/RENAMED
- `Watcher.EventPath()` — Affected file path

### 6.6 Build System

**Files:** `build/build_system.zia`, `build/error_parser.zia`, `build/output_panel.zia`
**Responsibility:** Compile and run Zia/BASIC programs, parse compiler output for
diagnostics, display build output.

```zia
entity BuildSystem {
    expose isBuilding: Bool;
    expose isRunning: Bool;
    expose buildChannel: obj;       // Channel for build results
    expose diagnostics: obj;        // Seq of Diagnostic
    expose lastBuildSuccess: Bool;
    expose outputText: String;      // Build output text

    func New() -> BuildSystem { ... }

    // Start a build in background thread
    func Build(config: BuildConfig, projectRoot: String) { ... }

    // Build and then run
    func BuildAndRun(config: BuildConfig, projectRoot: String) { ... }

    // Run without building
    func Run(config: BuildConfig, projectRoot: String) { ... }

    // Stop current build or run
    func Stop() { ... }

    // Check for build completion (non-blocking, call each frame)
    func CheckResults() -> Bool { ... }

    // Get diagnostics for a specific file
    func GetDiagnosticsForFile(path: String) -> obj { ... }
}
```

**Build execution (on worker thread):**

```zia
// Simplified build worker
func buildWorker(config: BuildConfig, projectRoot: String, channel: obj) {
    var args = Viper.Collections.Seq.New();
    args.Push(Viper.Box.Str(config.mainFile));
    if config.flags.Length > 0 {
        args.Push(Viper.Box.Str(config.flags));
    }

    var output = Viper.Exec.CaptureArgs(config.compilerPath, args);
    // Send output back to main thread
    channel.Send(Viper.Box.Str(output));
}
```

**Error parsing (see Section 7.4).**

### 6.7 Debugger

**Files:** `debug/debugger.zia`, `debug/breakpoints.zia`, `debug/watch_panel.zia`,
`debug/call_stack.zia`, `debug/debug_toolbar.zia`
**Responsibility:** Manage debug sessions, breakpoints, stepping, and variable
inspection.

**Implementation note:** Full interactive debugging requires debugger protocol
support in the Viper VM. The initial implementation provides:
- Breakpoint management (visual markers in the gutter)
- Build with debug symbols
- Console output capture during debugging
- Variable watch expressions (populated from debug output)

Future versions will add step-by-step execution when the VM exposes a debug protocol.

```zia
entity Debugger {
    expose isDebugging: Bool;
    expose breakpoints: obj;        // Seq of Breakpoint
    expose currentLine: Int;        // Current execution line (-1 if not debugging)
    expose currentFile: String;

    func New() -> Debugger { ... }

    // Breakpoint management
    func ToggleBreakpoint(filePath: String, line: Int) { ... }
    func ClearBreakpoints(filePath: String) { ... }
    func ClearAllBreakpoints() { ... }
    func GetBreakpoints(filePath: String) -> obj { ... }

    // Debug session
    func StartSession(config: BuildConfig, projectRoot: String) { ... }
    func StopSession() { ... }
    func StepOver() { ... }
    func StepInto() { ... }
    func StepOut() { ... }
    func Continue() { ... }

    // State
    func IsAtBreakpoint() -> Bool { ... }
}
```

### 6.8 Git Integration

**Files:** `git/git_integration.zia`, `git/git_status.zia`, `git/git_diff.zia`,
`git/git_branch.zia`, `git/git_commit.zia`, `git/git_log.zia`
**Responsibility:** Execute git commands via subprocess, parse output, provide
status/diff/branch/commit/log functionality.

```zia
entity GitIntegration {
    expose isGitRepo: Bool;
    expose currentBranch: String;
    expose fileStatuses: obj;      // Seq of GitFileStatus
    expose gitChannel: obj;        // Channel for async git results
    expose gitPath: String;        // Path to git executable

    func New(gitPath: String) -> GitIntegration { ... }

    // Check if project root is a git repo
    func Detect(projectRoot: String) -> Bool { ... }

    // Get current branch name
    func FetchBranch(projectRoot: String) { ... }

    // Get file status (modified, untracked, etc.)
    func FetchStatus(projectRoot: String) { ... }

    // Get diff for a file
    func FetchDiff(filePath: String, projectRoot: String) -> String { ... }

    // Stage/unstage a file
    func Stage(filePath: String, projectRoot: String) { ... }
    func Unstage(filePath: String, projectRoot: String) { ... }

    // Commit staged changes
    func Commit(message: String, projectRoot: String) { ... }

    // Branch operations
    func ListBranches(projectRoot: String) -> obj { ... }
    func SwitchBranch(branchName: String, projectRoot: String) { ... }
    func CreateBranch(branchName: String, projectRoot: String) { ... }

    // Push/pull
    func Push(projectRoot: String) { ... }
    func Pull(projectRoot: String) { ... }

    // Log
    func FetchLog(projectRoot: String, limit: Int) -> obj { ... }

    // Check results (non-blocking, call each frame)
    func CheckResults() { ... }

    // Helper: run a git command and return output
    func RunGit(args: String, projectRoot: String) -> String { ... }
}
```

**Git command execution:**

```zia
func RunGit(args: String, projectRoot: String) -> String {
    var cmd = self.gitPath + " -C " + projectRoot + " " + args;
    return Viper.Exec.ShellCapture(cmd);
}
```

**Status parsing:**

```
git status --porcelain
```

Output format: `XY path` where X=index status, Y=worktree status.
Parse each line: first two chars → status, rest → file path.

### 6.9 Terminal/Output Panel

**File:** `build/output_panel.zia`
**Responsibility:** Display build output, program stdout/stderr, and diagnostic
messages in the bottom panel.

```zia
entity OutputPanel {
    expose outputEditor: obj;   // CodeEditor widget used as read-only output
    expose content: String;     // Accumulated output text

    func New(parent: obj) -> OutputPanel { ... }

    // Append text to output
    func Append(text: String) { ... }

    // Clear all output
    func Clear() { ... }

    // Set entire content
    func SetContent(text: String) { ... }

    // Scroll to bottom
    func ScrollToEnd() { ... }
}
```

Uses a `Viper.GUI.CodeEditor` in read-only mode (set text but don't allow editing)
to display output with syntax coloring for error messages.

### 6.10 Find & Replace

**Files:** `search/find_replace.zia`, `search/find_in_files.zia`
**Responsibility:** In-editor find/replace using the FindBar widget, and
project-wide file search.

```zia
entity FindReplace {
    expose findBar: obj;       // Viper.GUI.FindBar widget

    func New(findBarWidget: obj) -> FindReplace { ... }

    // Show/hide the find bar
    func Show() { ... }
    func ShowReplace() { ... }
    func Hide() { ... }

    // Navigate matches
    func FindNext() -> Int { ... }
    func FindPrev() -> Int { ... }

    // Replace
    func Replace() -> Int { ... }
    func ReplaceAll() -> Int { ... }

    // Get match info
    func GetMatchCount() -> Int { ... }
    func GetCurrentMatch() -> Int { ... }
}
```

**FindBar API:**
- `FindBar.SetVisible(1/0)` — Show/hide
- `FindBar.SetReplaceMode(1/0)` — Toggle replace mode
- `FindBar.SetFindText(str)` — Set search text
- `FindBar.GetFindText()` — Get search text
- `FindBar.SetCaseSensitive(1/0)` — Toggle case sensitivity
- `FindBar.SetWholeWord(1/0)` — Toggle whole word
- `FindBar.SetRegex(1/0)` — Toggle regex mode
- `FindBar.FindNext()` / `FindPrev()` — Navigate matches
- `FindBar.Replace()` / `ReplaceAll()` — Replace operations
- `FindBar.GetMatchCount()` — Total matches
- `FindBar.Focus()` — Focus the search input

**Find in files (background thread):**

```zia
entity FindInFiles {
    expose results: obj;          // Seq of SearchResult
    expose searchChannel: obj;    // Channel for results from worker
    expose isSearching: Bool;
    expose query: String;
    expose resultCount: Int;

    func New() -> FindInFiles { ... }

    // Start a project-wide search
    func Search(query: String, projectRoot: String, caseSensitive: Bool,
                regex: Bool, includePattern: String, excludePattern: String) { ... }

    // Check for results (non-blocking)
    func CheckResults() -> Bool { ... }

    // Cancel active search
    func Cancel() { ... }
}
```

The search worker thread reads each file with `Viper.IO.File.ReadAllText()` and
matches lines using `Viper.String.IndexOf()` or `Viper.Text.Pattern.Find()` for
regex mode.

### 6.11 Command System

**Files:** `ui/command_system.zia`, `ui/command_defs.zia`, `ui/keybindings.zia`
**Responsibility:** Central command registry. Every IDE action is a command with an
ID, label, category, and optional keyboard shortcut. The command palette and
keyboard shortcuts both resolve to command IDs.

```zia
entity CommandSystem {
    expose commands: obj;      // Map<String, Command> — id → Command
    expose palette: obj;       // Viper.GUI.CommandPalette widget

    func New(paletteWidget: obj) -> CommandSystem { ... }

    // Register a command
    func Register(id: String, label: String, category: String, shortcut: String) { ... }

    // Register all built-in commands (called once at startup)
    func RegisterAll() { ... }

    // Show the command palette
    func ShowPalette() { ... }

    // Handle palette selection (call each frame)
    func HandlePalette() -> String { ... }

    // Handle keyboard shortcuts (call each frame)
    func HandleShortcuts() -> String { ... }

    // Execute a command by ID
    func Execute(id: String, ...) { ... }
}
```

**Shortcut registration:**
```zia
Viper.GUI.Shortcuts.Register("file.save", "Ctrl+S", "Save File");
Viper.GUI.Shortcuts.Register("file.new", "Ctrl+N", "New File");
// ...
```

**Shortcut polling:**
```zia
if Viper.GUI.Shortcuts.WasTriggered("file.save") != 0 {
    // Handle save
}
```

**Command palette registration:**
```zia
palette.AddCommandWithShortcut("file.save", "Save File", "File", "Ctrl+S");
palette.AddCommand("file.saveAll", "Save All Files", "File");
```

**Palette polling:**
```zia
if palette.WasSelected() != 0 {
    var cmdId = palette.GetSelected();
    commands.Execute(cmdId);
}
```

### 6.12 Settings Manager

**Files:** `core/settings.zia`, `core/settings_manager.zia`
**Responsibility:** Load/save IDE settings from a JSON configuration file.

```zia
entity SettingsManager {
    expose settings: Settings;
    expose configPath: String;     // Path to settings.json

    func New() -> SettingsManager { ... }

    // Load settings from disk (or create defaults)
    func Load() { ... }

    // Save settings to disk
    func Save() { ... }

    // Apply settings to IDE components
    func Apply(shell: AppShell, editor: EditorEngine) { ... }

    // Get the config directory path
    func GetConfigDir() -> String { ... }
}
```

**Config file location:** `~/.viperide/settings.json` (using `Viper.Machine.Home`).

**JSON operations:**
- `Viper.Text.Json.Parse(str)` — Parse JSON string to object
- `Viper.Text.Json.FormatPretty(obj, indent)` — Format object to JSON string
- `Viper.IO.File.ReadAllText(path)` — Read config file
- `Viper.IO.File.WriteAllText(path, str)` — Write config file

### 6.13 UI Shell (AppShell)

**File:** `app_shell.zia`
**Responsibility:** Top-level UI layout and coordination. Creates all widgets,
builds menus and toolbars, manages sidebar/panel visibility.

See Section 4.2 for the full entity definition.

Additional responsibilities:

```zia
// Handle menu item clicks (call each frame)
func HandleMenus(commands: CommandSystem) {
    if self.miFileNew.WasClicked() != 0 { commands.Execute("file.new"); }
    if self.miFileOpen.WasClicked() != 0 { commands.Execute("file.open"); }
    if self.miFileSave.WasClicked() != 0 { commands.Execute("file.save"); }
    // ... all menu items ...
}

// Handle toolbar button clicks (call each frame)
func HandleToolbar(commands: CommandSystem) {
    if self.tbNew.WasClicked() != 0 { commands.Execute("file.new"); }
    if self.tbOpen.WasClicked() != 0 { commands.Execute("file.open"); }
    if self.tbSave.WasClicked() != 0 { commands.Execute("file.save"); }
    // ... all toolbar items ...
}

// Update status bar with current state
func UpdateStatus(editor: EditorEngine, docs: DocumentManager, project: ProjectManager) {
    var doc = docs.GetActive();
    if doc != null {
        var lineInfo = "Ln " + editor.GetCursorLine() + ", Col " + editor.GetCursorCol();
        self.statusBar.SetRightText(lineInfo + "  |  " + doc.language);
        self.statusBar.SetCenterText(doc.filePath);
    }
    if project.project.isOpen {
        self.statusBar.SetLeftText(project.project.name);
    }
}

// Toggle sidebar visibility
func ToggleSidebar() {
    if self.sidebarVisible {
        self.mainSplit.SetPosition(0.0);
        self.sidebarVisible = false;
    } else {
        self.mainSplit.SetPosition(0.2);
        self.sidebarVisible = true;
    }
}

// Toggle bottom panel visibility
func TogglePanel() {
    if self.panelVisible {
        self.centerSplit.SetPosition(1.0);
        self.panelVisible = false;
    } else {
        self.centerSplit.SetPosition(0.75);
        self.panelVisible = true;
    }
}
```

---

## 7. Key Algorithms

### 7.1 Symbol Extraction (for Autocomplete / Go-to-Definition)

Scan source files line-by-line for declaration patterns using string matching.
No full parser needed — we extract enough structure for useful completions.

**Patterns to match:**

| Declaration | Pattern | Example |
|-------------|---------|---------|
| Function | line starts with `func ` (after whitespace) | `func Foo(x: Int) -> String` |
| Entity | line starts with `entity ` | `entity Document {` |
| Value | line starts with `value ` | `value Color { ... }` |
| Interface | line starts with `interface ` | `interface Drawable { ... }` |
| Variable | line starts with `var ` (module-level) | `var MAX_SIZE = 100;` |
| Exposed field | line contains `expose ` | `expose name: String;` |
| Hidden field | line contains `hide ` | `hide cache: obj;` |

**Algorithm:**

```
for each line in file:
    trimmed = line.Trim()
    if trimmed.StartsWith("func "):
        name = extract_word_after("func ")
        sig = extract_until("{") or extract_until(";")
        add SymbolEntry(name, kind=FUNC, file, lineNum, sig)
    elif trimmed.StartsWith("entity "):
        name = extract_word_after("entity ")
        currentScope = name
        add SymbolEntry(name, kind=ENTITY, file, lineNum, "")
    elif trimmed.StartsWith("value "):
        name = extract_word_after("value ")
        add SymbolEntry(name, kind=VALUE, file, lineNum, "")
    elif trimmed.StartsWith("interface "):
        name = extract_word_after("interface ")
        add SymbolEntry(name, kind=INTERFACE, file, lineNum, "")
    elif trimmed.StartsWith("var ") and currentScope == "":
        name = extract_word_after("var ")
        add SymbolEntry(name, kind=VAR, file, lineNum, "")
    elif trimmed.StartsWith("expose "):
        name = extract_word_after("expose ")
        typeInfo = extract_after(":")
        add SymbolEntry(name, kind=FIELD, file, lineNum, typeInfo, scope=currentScope)
```

**String operations used:**
- `Viper.String.Trim()` — Remove whitespace
- `Viper.String.StartsWith(prefix)` — Check prefix
- `Viper.String.IndexOf(sub)` — Find substring
- `Viper.String.Substring(start, len)` — Extract portion
- `Viper.String.Split(delim)` — Split string

### 7.2 Completion Ranking

When the user types a prefix, completion candidates are ranked:

1. **Exact prefix match** (case-sensitive) — highest score
2. **Case-insensitive prefix match** — medium score
3. **Substring match** — lower score
4. **Same-file symbols** — bonus points
5. **Same-scope symbols** — bonus points
6. **Recency** (recently used completions) — bonus points

Sort by score descending, return top N results.

### 7.3 Code Folding Region Detection

Scan for block boundaries using brace counting and indentation:

```
foldRegions = []
stack = []

for each line in file:
    if line contains "{":
        stack.push(lineNum)
    if line contains "}":
        if stack is not empty:
            startLine = stack.pop()
            if lineNum - startLine > 1:  // At least 2 lines
                foldRegions.add(startLine, lineNum)

// Also detect consecutive comment blocks
commentStart = -1
for each line in file:
    if line.TrimStart().StartsWith("//"):
        if commentStart < 0:
            commentStart = lineNum
    else:
        if commentStart >= 0 and lineNum - commentStart > 2:
            foldRegions.add(commentStart, lineNum - 1)
        commentStart = -1
```

Apply fold regions with `CodeEditor.AddFoldRegion(startLine, endLine)`.

### 7.4 Compiler Error Parsing

Parse compiler stderr output into structured `Diagnostic` entries.

**Zia compiler error format:**

```
file.zia:12:5: error: undeclared identifier 'foo'
file.zia:20:10: warning: unused variable 'x'
```

**Parser:**

```
for each line in compilerOutput:
    parts = line.Split(":")
    if parts.Count >= 4:
        filePath = parts[0]
        lineNum  = parseInt(parts[1])
        colNum   = parseInt(parts[2])
        rest     = parts[3..].Join(":")
        if rest.Has("error:"):
            message  = rest after "error:"
            severity = 0  // error
        elif rest.Has("warning:"):
            message  = rest after "warning:"
            severity = 1  // warning
        else:
            message  = rest
            severity = 2  // info
        diagnostics.add(Diagnostic(filePath, lineNum, colNum, severity, message.Trim()))
```

### 7.5 Project-Wide File Search

Background thread scans all project files for a query string.

```
func searchWorker(query, rootPath, caseSensitive, regex, channel):
    files = collectFiles(rootPath)  // recursive Dir.FilesSeq
    for each file in files:
        if matchesExcludePattern(file): continue
        content = File.ReadAllText(file)
        lines = content.Split("\n")
        for lineNum, line in lines:
            if regex:
                if Pattern.IsMatch(line, query):
                    pos = Pattern.FindPos(line, query)
                    match = Pattern.Find(line, query)
                    channel.TrySend(SearchResult(file, lineNum, pos, match.Length, line))
            else:
                searchLine = if caseSensitive then line else line.ToLower()
                searchQuery = if caseSensitive then query else query.ToLower()
                pos = searchLine.IndexOf(searchQuery)
                while pos >= 0:
                    channel.TrySend(SearchResult(file, lineNum, pos, query.Length, line))
                    pos = searchLine.IndexOfFrom(pos + 1, searchQuery)
```

### 7.6 Git Status Parsing

Parse `git status --porcelain` output:

```
for each line in statusOutput:
    if line.Length < 3: continue
    indexStatus = line.Substring(0, 1)
    workStatus  = line.Substring(1, 1)
    filePath    = line.Mid(3)

    // Determine display status
    if indexStatus == "?" and workStatus == "?":
        status = "?"   // Untracked
    elif indexStatus == "A":
        status = "A"   // Added
    elif indexStatus == "D" or workStatus == "D":
        status = "D"   // Deleted
    elif indexStatus == "M" or workStatus == "M":
        status = "M"   // Modified
    elif indexStatus == "R":
        status = "R"   // Renamed
    // ... etc

    staged = indexStatus != " " and indexStatus != "?"
    fileStatuses.add(GitFileStatus(filePath, status, staged))
```

---

## 8. Configuration Formats

### 8.1 Settings File (`~/.viperide/settings.json`)

```json
{
    "editor": {
        "tabSize": 4,
        "insertSpaces": true,
        "wordWrap": false,
        "showLineNumbers": true,
        "showMinimap": true,
        "minimapWidth": 80,
        "fontSize": 14.0,
        "fontPath": ""
    },
    "theme": "dark",
    "build": {
        "ziaCompilerPath": "zia",
        "basicCompilerPath": "vbasic",
        "autoBuildOnSave": false,
        "buildBeforeRun": true
    },
    "git": {
        "path": "git",
        "autoFetchInterval": 300
    },
    "ui": {
        "sidebarVisible": true,
        "sidebarWidth": 0.2,
        "panelVisible": true,
        "panelHeight": 0.25,
        "toolbarVisible": true,
        "statusBarVisible": true,
        "breadcrumbVisible": true
    },
    "files": {
        "autoSaveInterval": 0,
        "trimTrailingWhitespace": false,
        "insertFinalNewline": true,
        "excludePatterns": ".git,build,*.o"
    },
    "recent": {
        "maxFiles": 20,
        "maxProjects": 10,
        "lastProject": "/path/to/last/project"
    }
}
```

### 8.2 Project Config (`.viperide/project.json`)

Stored in the project root under a `.viperide/` directory.

```json
{
    "name": "MyProject",
    "language": "zia",
    "build": {
        "mainFile": "main.zia",
        "outputName": "myproject",
        "flags": "",
        "runArgs": ""
    },
    "exclude": [
        "build/",
        "*.o",
        ".git/"
    ]
}
```

### 8.3 Keybindings File (`~/.viperide/keybindings.json`)

User-customizable keybindings (overrides defaults):

```json
{
    "keybindings": [
        { "command": "file.save", "key": "Ctrl+S" },
        { "command": "file.new", "key": "Ctrl+N" },
        { "command": "edit.find", "key": "Ctrl+F" },
        { "command": "edit.replace", "key": "Ctrl+H" },
        { "command": "view.commandPalette", "key": "Ctrl+Shift+P" }
    ]
}
```

### 8.4 Recent Files (`~/.viperide/recent.json`)

```json
{
    "files": [
        "/path/to/file1.zia",
        "/path/to/file2.bas"
    ],
    "projects": [
        "/path/to/project1",
        "/path/to/project2"
    ]
}
```

---

## 9. Default Keymap

### 9.1 File Operations

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+N` | `file.new` | New file |
| `Ctrl+O` | `file.open` | Open file |
| `Ctrl+S` | `file.save` | Save file |
| `Ctrl+Shift+S` | `file.saveAs` | Save as |
| `Ctrl+Alt+S` | `file.saveAll` | Save all files |
| `Ctrl+W` | `file.close` | Close file |
| `Ctrl+Shift+T` | `file.reopenClosed` | Reopen last closed file |

### 9.2 Editing

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+Z` | `edit.undo` | Undo |
| `Ctrl+Y` | `edit.redo` | Redo |
| `Ctrl+X` | `edit.cut` | Cut line/selection |
| `Ctrl+C` | `edit.copy` | Copy line/selection |
| `Ctrl+V` | `edit.paste` | Paste |
| `Ctrl+D` | `edit.duplicateLine` | Duplicate line |
| `Ctrl+Shift+K` | `edit.deleteLine` | Delete line |
| `Alt+Up` | `edit.moveLineUp` | Move line up |
| `Alt+Down` | `edit.moveLineDown` | Move line down |
| `Ctrl+/` | `edit.toggleComment` | Toggle line comment |
| `Tab` | `edit.indent` | Indent |
| `Shift+Tab` | `edit.outdent` | Outdent |
| `Ctrl+Shift+F` | `edit.formatDocument` | Format document |

### 9.3 Selection

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+A` | `edit.selectAll` | Select all |
| `Ctrl+L` | `edit.selectLine` | Select current line |
| `Ctrl+Shift+L` | `edit.selectAllOccurrences` | Select all occurrences of selection |
| `Alt+Click` | `edit.addCursor` | Add cursor at click position |

### 9.4 Find & Replace

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+F` | `edit.find` | Find in file |
| `Ctrl+H` | `edit.replace` | Find and replace in file |
| `Ctrl+Shift+F` | `search.findInFiles` | Find in files (project-wide) |
| `F3` | `edit.findNext` | Find next match |
| `Shift+F3` | `edit.findPrev` | Find previous match |
| `Escape` | `edit.dismissFind` | Dismiss find bar |

### 9.5 Navigation

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+G` | `go.line` | Go to line |
| `Ctrl+P` | `go.file` | Quick open file |
| `Ctrl+Shift+O` | `go.symbol` | Go to symbol in file |
| `F12` | `go.definition` | Go to definition |
| `Ctrl+Shift+P` | `view.commandPalette` | Command palette |
| `Ctrl+Tab` | `view.nextTab` | Next editor tab |
| `Ctrl+Shift+Tab` | `view.prevTab` | Previous editor tab |
| `Alt+Left` | `go.back` | Navigate back |
| `Alt+Right` | `go.forward` | Navigate forward |

### 9.6 View

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+B` | `view.toggleSidebar` | Toggle sidebar |
| `Ctrl+J` | `view.togglePanel` | Toggle bottom panel |
| `Ctrl+Shift+M` | `view.toggleMinimap` | Toggle minimap |
| `Ctrl+,` | `view.settings` | Open settings |
| `F11` | `view.fullscreen` | Toggle fullscreen |

### 9.7 Build & Run

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+Shift+B` | `build.build` | Build |
| `F5` | `build.run` | Build and run |
| `Ctrl+F5` | `build.runWithoutDebug` | Run without debugging |
| `Shift+F5` | `build.stop` | Stop build/run |

### 9.8 Debug

| Shortcut | Command | Description |
|----------|---------|-------------|
| `F9` | `debug.toggleBreakpoint` | Toggle breakpoint |
| `F5` | `debug.start` | Start/continue debugging |
| `Shift+F5` | `debug.stop` | Stop debugging |
| `F10` | `debug.stepOver` | Step over |
| `F11` | `debug.stepInto` | Step into |
| `Shift+F11` | `debug.stepOut` | Step out |

### 9.9 Git

| Shortcut | Command | Description |
|----------|---------|-------------|
| `Ctrl+Shift+G` | `git.panel` | Show git panel |
| `Ctrl+Enter` | `git.commit` | Commit (when in git panel) |

---

## 10. Implementation Phases

### Phase 1: Skeleton & Core Editor

**Goal:** Basic window with a code editor that can open and save files.

**Files to create:**
- `main.zia` — Entry point, event loop
- `app_shell.zia` — Basic layout (VBox, editor area, status bar)
- `core/document.zia` — Document entity
- `core/document_manager.zia` — Open/save/close
- `editor/editor_engine.zia` — CodeEditor wrapper
- `editor/editor_tabs.zia` — Tab management
- `services/file_utils.zia` — File I/O helpers

**Features:**
- [x] Window with title, dark theme
- [x] Code editor widget with line numbers
- [x] Open file dialog (`Viper.GUI.FileDialog.Open`)
- [x] Save file dialog (`Viper.GUI.FileDialog.Save`)
- [x] Tab bar for open files
- [x] Modified indicator on tabs
- [x] Status bar with line:col and filename
- [x] Keyboard shortcuts: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+W

**Deliverable:** A working multi-file text editor.

---

### Phase 2: Menu Bar & Toolbar

**Goal:** Full menu bar and toolbar with all command stubs.

**Files to create:**
- `ui/command_system.zia` — Command registry
- `ui/command_defs.zia` — All command definitions
- `ui/keybindings.zia` — Shortcut bindings
- `ui/menu_builder.zia` — Menu bar construction
- `ui/toolbar_builder.zia` — Toolbar construction

**Features:**
- [ ] Complete menu bar (File, Edit, View, Go, Build, Debug, Git, Help)
- [ ] Toolbar with icon buttons
- [ ] Command palette (Ctrl+Shift+P)
- [ ] All keyboard shortcuts registered

**Deliverable:** Full chrome with stubbed commands showing toast notifications.

---

### Phase 3: Project Manager & File Tree

**Goal:** Open a folder, display file tree, click files to open.

**Files to create:**
- `core/project.zia` — Project entity
- `core/project_manager.zia` — File tree management
- `ui/sidebar.zia` — Sidebar panel switching
- `services/path_utils.zia` — Path helpers

**Features:**
- [ ] Open Folder command
- [ ] TreeView file explorer in sidebar
- [ ] Click file → open in editor
- [ ] Expand/collapse folders
- [ ] File/folder icons (using label text prefixes)
- [ ] Context menu for new file/folder/rename/delete
- [ ] Exclude patterns (.git, build, etc.)

**Deliverable:** Project-based file management.

---

### Phase 4: Syntax Highlighting & Folding

**Goal:** Language-aware syntax coloring and code folding.

**Files to create:**
- `editor/syntax_zia.zia` — Zia syntax setup
- `editor/syntax_basic.zia` — BASIC syntax setup
- `editor/syntax_theme.zia` — Token color themes
- `editor/folding.zia` — Fold region detection
- `lang/language_registry.zia` — Extension → language mapping
- `lang/token_types.zia` — Token type constants

**Features:**
- [ ] Syntax highlighting for Zia
- [ ] Syntax highlighting for BASIC
- [ ] Auto-detect language from file extension
- [ ] Dark and light token color themes
- [ ] Code folding for functions, entities, blocks
- [ ] Fold/unfold via gutter click or keyboard

**Deliverable:** Color-coded, foldable source code.

---

### Phase 5: Build System

**Goal:** Compile and run programs from the IDE.

**Files to create:**
- `build/build_system.zia` — Build orchestration
- `build/build_config.zia` — Build configuration
- `build/error_parser.zia` — Diagnostic extraction
- `build/output_panel.zia` — Output display
- `build/run_config.zia` — Run configuration

**Features:**
- [ ] Build command (Ctrl+Shift+B)
- [ ] Build & Run (F5)
- [ ] Background compilation (worker thread)
- [ ] Parse compiler errors → Diagnostic entries
- [ ] Show errors in output panel with file:line references
- [ ] Gutter error markers (red icons for errors, yellow for warnings)
- [ ] Click error → jump to file:line
- [ ] Status bar: error/warning counts

**Deliverable:** Integrated build with inline error display.

---

### Phase 6: Find & Replace

**Goal:** In-file and project-wide search.

**Files to create:**
- `search/find_replace.zia` — In-editor find/replace
- `search/find_in_files.zia` — Project-wide search
- `search/go_to_line.zia` — Go to line dialog

**Features:**
- [ ] Find bar (Ctrl+F) with match count, current match indicator
- [ ] Replace bar (Ctrl+H) with replace/replace-all
- [ ] Case sensitive, whole word, regex toggles
- [ ] Find in files (Ctrl+Shift+F) — background search
- [ ] Search results panel with file:line previews
- [ ] Click result → open file at line
- [ ] Go to line (Ctrl+G)

**Deliverable:** Full search capabilities.

---

### Phase 7: Autocomplete & Symbol Navigation

**Goal:** Context-aware code completion and symbol navigation.

**Files to create:**
- `editor/autocomplete.zia` — Completion engine
- `editor/symbol_index.zia` — Symbol table
- `editor/bracket_match.zia` — Bracket matching
- `editor/hover_info.zia` — Hover tooltips
- `search/go_to_symbol.zia` — Symbol navigation
- `search/go_to_file.zia` — Quick file open

**Features:**
- [ ] Symbol index built from project files
- [ ] Auto-trigger completion after `.` or `Ctrl+Space`
- [ ] Completion popup with symbol kind, type info
- [ ] Go to definition (F12)
- [ ] Go to symbol (Ctrl+Shift+O)
- [ ] Quick open file (Ctrl+P) — fuzzy file name search
- [ ] Viper.* runtime API completions
- [ ] Bracket matching highlight
- [ ] Hover tooltips for symbols

**Deliverable:** Intelligent code assistance.

---

### Phase 8: Git Integration

**Goal:** Git status, diff, commit, branch management.

**Files to create:**
- `git/git_integration.zia` — Git command execution
- `git/git_status.zia` — File status parsing
- `git/git_diff.zia` — Diff display
- `git/git_branch.zia` — Branch management
- `git/git_commit.zia` — Commit dialog
- `git/git_log.zia` — Commit history

**Features:**
- [ ] Detect git repository
- [ ] Branch name in status bar
- [ ] File status indicators in file tree (M, A, D, ?)
- [ ] Git panel in sidebar (staged/unstaged files)
- [ ] Stage/unstage files
- [ ] Commit dialog with message input
- [ ] View diff for modified files
- [ ] Branch list, switch, create
- [ ] Push/pull
- [ ] Commit log viewer

**Deliverable:** Git workflow without leaving the IDE.

---

### Phase 9: Debug Support

**Goal:** Breakpoint management, debug session control.

**Files to create:**
- `debug/debugger.zia` — Debug session
- `debug/breakpoints.zia` — Breakpoint model
- `debug/watch_panel.zia` — Watch expressions
- `debug/call_stack.zia` — Call stack display
- `debug/debug_toolbar.zia` — Step controls

**Features:**
- [ ] Set/clear breakpoints by clicking gutter
- [ ] Breakpoint markers (red circles in gutter)
- [ ] Start debug session
- [ ] Debug toolbar (Continue, Step Over, Step Into, Step Out, Stop)
- [ ] Debug output panel
- [ ] Watch expression panel
- [ ] Call stack panel
- [ ] Conditional breakpoints

**Deliverable:** Basic debugging workflow.

---

### Phase 10: Settings & Configuration

**Goal:** Persistent settings, user customization.

**Files to create:**
- `core/settings.zia` — Settings entity
- `core/settings_manager.zia` — Load/save/apply
- `core/recent_files.zia` — MRU files
- `core/recent_projects.zia` — MRU projects
- `services/json_config.zia` — JSON config helpers

**Features:**
- [ ] Settings file (`~/.viperide/settings.json`)
- [ ] Settings UI (Ctrl+,)
- [ ] Project-specific config (`.viperide/project.json`)
- [ ] Custom keybindings
- [ ] Recent files/projects list
- [ ] Auto-restore last project on launch
- [ ] Theme switching (dark/light)
- [ ] Font size/family configuration

**Deliverable:** Customizable, persistent IDE settings.

---

### Phase 11: File Watching & Advanced Features

**Goal:** External change detection, snippets, advanced editing.

**Files to create:**
- `services/file_watcher.zia` — Background file watcher
- `editor/snippets.zia` — Code snippets
- `lang/indent_rules.zia` — Auto-indent rules
- `lang/comment_rules.zia` — Comment toggle rules
- `ui/breadcrumb_manager.zia` — Breadcrumb navigation
- `ui/minimap_manager.zia` — Minimap configuration
- `ui/notification.zia` — Toast notifications

**Features:**
- [ ] File system watcher (background thread)
- [ ] Prompt when file changed externally
- [ ] Auto-refresh file tree on external changes
- [ ] Code snippets (entity template, func template, etc.)
- [ ] Smart auto-indent (language-aware)
- [ ] Toggle comment (Ctrl+/)
- [ ] Breadcrumb navigation bar
- [ ] Minimap with error/warning markers
- [ ] Toast notifications for operations

**Deliverable:** Polished editing experience.

---

### Phase 12: Welcome Screen, About, Polish

**Goal:** First-run experience, about dialog, final polish.

**Files to create:**
- `ui/welcome_tab.zia` — Welcome page
- `ui/about_dialog.zia` — About dialog
- `ui/statusbar_manager.zia` — Enhanced status bar
- `services/string_utils.zia` — String helpers
- `services/thread_helpers.zia` — Thread utilities
- `services/clipboard_manager.zia` — Clipboard helpers

**Features:**
- [ ] Welcome tab with recent projects, getting started guide
- [ ] About dialog (version, credits)
- [ ] Drag-and-drop file open (App.WasFileDropped)
- [ ] Unsaved changes prompt on close
- [ ] Tab context menu (close, close others, close all, copy path)
- [ ] Status bar click actions (change language, encoding, etc.)
- [ ] Error recovery (handle missing files, invalid config gracefully)
- [ ] Performance optimization (lazy tree loading, throttled file search)

**Deliverable:** Production-ready IDE.

---

## 11. Risk Analysis

### 11.1 Platform Risks

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| **100-file bind limit** | Cannot compile if files exceed limit | Low (targeting 65) | Monitor count; merge small utility files if needed |
| **No try/catch** | Cannot recover from runtime errors | Medium | Use return codes, null checks, `File.Exists()` guards everywhere |
| **No async/await** | Cannot do non-blocking I/O natively | Medium | Use worker threads + Channel for all blocking operations |
| **No reflection** | Cannot inspect types at runtime | Low | Use string-based symbol index; no dynamic dispatch needed |
| **Map keys are strings only** | Cannot use composite keys | Low | Serialize composite keys as strings (e.g., "file:line") |
| **CodeEditor widget limitations** | May lack some advanced editing features | Medium | Work within widget capabilities; defer missing features |
| **Thread safety** | Shared state corruption | Medium | Channel-only communication; no shared mutable state between threads |

### 11.2 Design Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Main loop polling performance** | UI lag if too many checks per frame | Throttle expensive operations; only check channels/watchers every N frames |
| **Symbol index scale** | Slow for very large projects | Incremental indexing; only re-index changed files |
| **File search performance** | Slow for many/large files | Background thread; stream results via channel; cancel support |
| **Git subprocess overhead** | Visible delay on git operations | Background thread; cache results; throttle auto-fetch |
| **Memory usage** | Large projects with many open files | Lazy loading; unload content of background tabs |

### 11.3 Unknowns

| Item | Question | Resolution Path |
|------|----------|-----------------|
| **CodeEditor selection events** | Can we detect cursor position changes per frame? | Test: poll `GetCursorLine()`/`GetCursorCol()` and compare to previous |
| **TreeView selection events** | How to detect which node was clicked? | Test: check if TreeView has a `GetSelected()` or similar API. May need to poll node state. |
| **TabBar tab switching** | How to detect active tab changed? | Test: poll `TabBar.GetActive()` or similar. Compare to previous. |
| **CodeEditor undo/redo** | Does the widget have built-in undo? | Test: likely built-in. If not, implement undo stack in document manager. |
| **Large file handling** | Performance with files >10K lines? | Test: profile CodeEditor with large files. May need virtual scrolling. |

### 11.4 Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| Viper Runtime | Current (2026-01-27) | GUI, IO, Exec, Threads, Collections, Text |
| `zia` compiler | Current | Compilation of IDE itself and user programs |
| `vbasic` compiler | Current | BASIC compilation support |
| `git` | Any recent | Git integration |

---

## Appendix A: Viper.GUI Widget Reference (Used)

| Widget | Constructor | Key Methods |
|--------|-------------|-------------|
| `App` | `App.New(title, w, h)` | `Poll()`, `Render()`, `Destroy()`, `ShouldClose`, `Root`, `SetFont()`, `WasFileDropped()`, `GetDroppedFile()` |
| `VBox` | `VBox.New()` | `SetSpacing()`, `SetPadding()`, `AddChild()`, `SetSize()` |
| `HBox` | `HBox.New()` | `SetSpacing()`, `SetPadding()`, `AddChild()`, `SetSize()` |
| `Label` | `Label.New(parent, text)` | `SetText()`, `SetColor()`, `SetFont()` |
| `Button` | `Button.New(parent, text)` | `WasClicked()`, `SetText()`, `SetStyle()` |
| `TextInput` | `TextInput.New(parent)` | `SetText()`, `Text`, `SetPlaceholder()` |
| `CodeEditor` | `CodeEditor.New(parent)` | `SetText()`, `Text`, `SetLanguage()`, `SetTokenColor()`, `SetCursor()`, `ScrollToLine()`, `LineCount`, `IsModified()`, `ClearModified()`, `SetShowLineNumbers()`, `SetGutterIcon()`, `ClearGutterIcons()`, `WasGutterClicked()`, `GetGutterClickLine()`, `AddFoldRegion()`, `Fold()`, `Unfold()`, `AddHighlight()`, `ClearHighlights()`, `AddCursor()`, `ClearCursors()`, `SetFont()` |
| `TabBar` | `TabBar.New(parent)` | `AddTab(label, closeable)`, `RemoveTab()`, `SetActive()` |
| `Tab` | via `TabBar.AddTab()` | `SetTitle()`, `SetModified()` |
| `SplitPane` | `SplitPane.New(parent, orient)` | `SetPosition()`, `First`, `Second` |
| `TreeView` | `TreeView.New(parent)` | `AddNode(tree, parent, label)`, `RemoveNode()`, `Clear()`, `Expand()`, `Collapse()`, `Select()` |
| `FindBar` | `FindBar.New(parent)` | `SetVisible()`, `SetFindText()`, `GetFindText()`, `SetReplaceText()`, `GetReplaceText()`, `SetCaseSensitive()`, `SetWholeWord()`, `SetRegex()`, `SetReplaceMode()`, `FindNext()`, `FindPrev()`, `Replace()`, `ReplaceAll()`, `GetMatchCount()`, `GetCurrentMatch()`, `Focus()` |
| `CommandPalette` | `CommandPalette.New(parent)` | `AddCommand()`, `AddCommandWithShortcut()`, `RemoveCommand()`, `Show()`, `Hide()`, `IsVisible()`, `SetPlaceholder()`, `GetSelected()`, `WasSelected()` |
| `MenuBar` | `MenuBar.New(parent)` | `AddMenu(label)` |
| `Menu` | via `MenuBar.AddMenu()` | `AddItem(label)`, `AddSeparator()`, `AddSubmenu(label)` |
| `MenuItem` | via `Menu.AddItem()` | `WasClicked()`, `SetEnabled()`, `SetChecked()`, `SetShortcut()`, `SetText()` |
| `ContextMenu` | `ContextMenu.New()` | `AddItem()`, `AddItemWithShortcut()`, `AddSeparator()`, `Show(x,y)`, `Hide()`, `IsVisible()` |
| `StatusBar` | `StatusBar.New(parent)` | `SetLeftText()`, `SetCenterText()`, `SetRightText()`, `AddText()` |
| `StatusBarItem` | via `StatusBar.AddText()` | `SetText()`, `SetTooltip()`, `WasClicked()` |
| `Toolbar` | `Toolbar.New(parent)` | `AddButton(icon, label)`, `AddSeparator()`, `SetIconSize()`, `SetStyle()` |
| `ToolbarItem` | via `Toolbar.AddButton()` | `WasClicked()`, `SetEnabled()`, `SetTooltip()`, `SetIcon()` |
| `Minimap` | `Minimap.New(parent)` | `BindEditor()`, `UnbindEditor()`, `SetWidth()`, `SetScale()`, `AddMarker()`, `RemoveMarkers()`, `ClearMarkers()` |
| `Breadcrumb` | `Breadcrumb.New(parent)` | `SetPath()`, `SetItems()`, `AddItem()`, `Clear()`, `WasItemClicked()`, `GetClickedIndex()`, `GetClickedData()`, `SetSeparator()` |
| `Tooltip` | `Tooltip.Show(text, x, y)` | `ShowRich()`, `Hide()`, `SetDelay()` |
| `Toast` | `Toast.Info(msg)` | `Success()`, `Warning()`, `Error()`, `New()`, `SetAction()`, `WasActionClicked()`, `Dismiss()` |
| `MessageBox` | `MessageBox.Info(title, msg)` | `Warning()`, `Error()`, `Question()`, `Confirm()` |
| `FileDialog` | `FileDialog.Open(title, filter, dir)` | `OpenMultiple()`, `Save()`, `SelectFolder()` |
| `Clipboard` | `Clipboard.SetText(str)` | `GetText()`, `HasText()`, `Clear()` |
| `Shortcuts` | `Shortcuts.Register(id, key, desc)` | `Unregister()`, `WasTriggered()`, `GetTriggered()`, `IsEnabled()`, `SetEnabled()` |
| `Theme` | `Theme.SetDark()` | `SetLight()` |
| `Font` | `Font.Load(path)` | `Destroy()` |
| `Widget` | (base) | `Destroy()`, `SetVisible()`, `SetEnabled()`, `SetSize()`, `AddChild()`, `IsHovered()`, `IsPressed()`, `IsFocused()`, `WasClicked()`, `SetPosition()`, `SetTooltip()`, `SetDraggable()`, `SetDropTarget()`, `WasDropped()` |
| `Checkbox` | `Checkbox.New(parent, label)` | `IsChecked()`, `SetChecked()` |
| `Dropdown` | `Dropdown.New(parent)` | `AddItem()`, `SetSelected()`, `Selected`, `SelectedText` |
| `ListBox` | `ListBox.New(parent)` | `AddItem()`, `RemoveItem()`, `Clear()`, `Select()`, `Selected` |
| `ScrollView` | `ScrollView.New(parent)` | `SetScroll()`, `SetContentSize()` |
| `Image` | `Image.New(parent)` | `SetPixels()`, `Clear()` |
| `ProgressBar` | `ProgressBar.New(parent)` | `SetValue()`, `Value` |
| `Slider` | `Slider.New(parent, orient)` | `SetValue()`, `Value`, `SetRange()`, `SetStep()` |
| `Spinner` | `Spinner.New(parent)` | `SetValue()`, `Value`, `SetRange()`, `SetStep()` |

## Appendix B: Viper.* Service Reference (Used)

| Service | Key Functions |
|---------|---------------|
| `Viper.IO.File` | `ReadAllText`, `WriteAllText`, `Exists`, `Delete`, `Move`, `Copy`, `Size`, `Modified`, `ReadAllLines`, `Append` |
| `Viper.IO.Dir` | `Current`, `Exists`, `FilesSeq`, `DirsSeq`, `ListSeq`, `Entries`, `Make`, `MakeAll`, `Remove`, `RemoveAll`, `SetCurrent` |
| `Viper.IO.Path` | `Join`, `Name`, `Stem`, `Ext`, `Dir`, `Abs`, `IsAbs`, `Norm`, `Sep`, `WithExt` |
| `Viper.IO.Watcher` | `new`, `Start`, `Stop`, `Poll`, `PollFor`, `EventPath`, `EventType`, `EVENT_CREATED/MODIFIED/DELETED/RENAMED` |
| `Viper.Exec` | `Capture`, `CaptureArgs`, `Run`, `RunArgs`, `Shell`, `ShellCapture` |
| `Viper.Threads.Thread` | `Start`, `Join`, `TryJoin`, `JoinFor`, `Sleep`, `Yield`, `Id`, `IsAlive` |
| `Viper.Threads.Channel` | `New`, `Send`, `TrySend`, `SendFor`, `Recv`, `TryRecv`, `RecvFor`, `Close`, `Len`, `Cap`, `IsClosed`, `IsEmpty`, `IsFull` |
| `Viper.Threads.Monitor` | `Enter`, `TryEnter`, `Exit`, `Wait`, `WaitFor`, `Pause`, `PauseAll` |
| `Viper.Text.Json` | `Parse`, `ParseObject`, `ParseArray`, `Format`, `FormatPretty`, `IsValid`, `TypeOf` |
| `Viper.Text.Pattern` | `IsMatch`, `Find`, `FindFrom`, `FindPos`, `FindAll`, `Replace`, `ReplaceFirst`, `Split`, `Escape` |
| `Viper.Text.StringBuilder` | `New`, `Append`, `AppendLine`, `ToString`, `Clear`, `Length` |
| `Viper.String` | `Trim`, `StartsWith`, `EndsWith`, `Has`, `IndexOf`, `IndexOfFrom`, `Replace`, `Split`, `Substring`, `Length`, `ToLower`, `ToUpper`, `Left`, `Right`, `Mid`, `Count`, `PadLeft`, `PadRight` |
| `Viper.Collections.Seq` | `New`, `Push`, `Pop`, `Get`, `Set`, `Len`, `Find`, `Has`, `Slice`, `Sort`, `Clear`, `Clone`, `Insert`, `Remove` |
| `Viper.Collections.Map` | `New`, `Set`, `Get`, `GetOr`, `Has`, `Remove`, `Keys`, `Values`, `Len`, `Clear` |
| `Viper.Collections.List` | `New`, `Add`, `Get`, `Set`, `Count`, `Find`, `Has`, `RemoveAt`, `Clear` |
| `Viper.Box` | `I64`, `F64`, `Str`, `I1`, `ToI64`, `ToF64`, `ToStr`, `ToI1`, `Type`, `EqI64`, `EqStr` |
| `Viper.Machine` | `Home`, `OS`, `Cores`, `Temp`, `User` |
| `Viper.DateTime` | `Now`, `Format` |
| `Viper.Environment` | `GetArgumentCount`, `GetArgument`, `GetVariable`, `HasVariable`, `EndProgram` |
| `Viper.Diagnostics` | `Trap(msg)`, `Assert(cond, msg)`, `Stopwatch.*` |
| `Viper.Log` | `Debug`, `Info`, `Warn`, `Error`, `SetLevel` |
