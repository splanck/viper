# ViperIDE

A full-featured IDE for Zia and Viper BASIC, built entirely in Zia using the `Viper.GUI.*` runtime classes.

## Features

- **Code Editor** with syntax highlighting, line numbers, undo/redo, and minimap
- **IntelliSense** code completion via the Zia CompletionEngine, using the active file path for relative `bind` resolution
- **Live Diagnostics** — errors and warnings appear as you type (debounced 500ms)
- **Hover Tooltips** — type and signature info on mouse dwell over identifiers
- **File Explorer** with recursive tree view and context menus
- **Tabbed Editing** with modified indicators and close buttons
- **Find/Replace** bar with match count and navigation
- **Build Integration** — compile and run Zia/BASIC files with error diagnostics
- **Command Palette** (Ctrl+Shift+P) for keyboard-driven workflow
- **Persistent Settings** — font size, optional font path, theme, minimap visibility, diagnostics, folding, and tab width saved to `~/.viperide/settings.ini`
- **File Watcher** — detects external changes and prompts for reload
- **Themes** — dark and light theme switching

## Project Structure

```
viperide/
    main.zia                  Entry point, event loop, trigger helpers
    viper.project             Project manifest

    ui/
        app_shell.zia         Window layout, menus, toolbar, panels

    commands/
        file_commands.zia     New, Open, Save, SaveAs, SaveAll, Close, Reload, Exit
        edit_commands.zia     Undo, Redo, Cut, Copy, Paste, SelectAll, Find, GoTo
        view_commands.zia     Sidebar, Zoom, Theme, Fullscreen, Minimap, Settings
        build_commands.zia    Build, BuildAndRun, Run, diagnostics panel

    core/
        document.zia          Document entity (path, content, cursor state)
        document_manager.zia  Open/save/close document operations
        project.zia           Project entity (root path, name)
        project_manager.zia   File tree population and selection
        settings.zia          Persistent settings via Viper.Text.Ini

    editor/
        editor_engine.zia     CodeEditor widget wrapper
        editor_tabs.zia       TabBar synchronization
        completion.zia        IntelliSense popup controller
        diagnostics.zia       Live error checking (errors-as-you-type)
        hover.zia             Hover tooltips (type info on mouse dwell)

    build/
        build_system.zia      Compile, run, parse diagnostics
        diagnostic.zia        Diagnostic data class (error/warning)

    services/
        file_utils.zia        Language detection from file extension
```

## Running

```bash
cd examples/apps/viperide
viper run .
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N | New File |
| Ctrl+O | Open File |
| Ctrl+S | Save |
| Ctrl+W | Close File |
| Ctrl+F | Find/Replace |
| Ctrl+G | Go To Line |
| Ctrl+Shift+B | Build |
| F5 | Build and Run |
| Ctrl+Shift+P | Command Palette |
| Ctrl+B | Toggle Sidebar |
| Ctrl+=/-/0 | Zoom In/Out/Reset |
| Ctrl+Shift+T | Toggle Theme |
| F11 | Fullscreen |
| Ctrl+Space | Trigger Completion |
| Ctrl+, | Settings / font preferences |

## Architecture

ViperIDE follows a layered architecture:

1. **Core Layer** (`core/`) — Domain entities (Document, Project, Settings) with no UI dependencies
2. **Editor Layer** (`editor/`) — Wraps GUI.CodeEditor with IDE-level features (tabs, completion)
3. **Build Layer** (`build/`) — Compiler invocation and diagnostic parsing
4. **UI Layer** (`ui/`) — Window layout, menus, toolbar, panels
5. **Main Loop** (`main.zia`) — Event dispatch connecting UI events to core operations

All GUI widgets come from the `Viper.GUI.*` runtime classes. No external dependencies.
