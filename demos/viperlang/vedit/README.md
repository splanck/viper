# VEdit - Viper Text Editor Demo

A demonstration of the `Viper.GUI.*` widget library by building a simple text editor.

## Files

- `main.viper` - Full-featured editor with toolbar, tabs, and status bar
- `simple.viper` - Minimal example showing basic GUI usage
- `widgets_demo.viper` - Comprehensive widget showcase (form, list, progress, etc.)

## Features Demonstrated

### main.viper
- `Viper.GUI.App` - Application window management
- `Viper.GUI.VBox` / `HBox` - Layout containers
- `Viper.GUI.Button` - Toolbar buttons with click handling
- `Viper.GUI.TabBar` - Multi-document tabs
- `Viper.GUI.CodeEditor` - Multi-line code editing
- `Viper.GUI.Label` - Text display
- `Viper.GUI.Theme` - Dark theme

### simple.viper
- Basic application setup
- `Viper.GUI.TextInput` - Single-line input
- `Viper.GUI.CodeEditor` - Code editing
- Button click handling with `WasClicked()`
- Status updates

### widgets_demo.viper
- `Viper.GUI.Dropdown` - Selection dropdown
- `Viper.GUI.Checkbox` - Toggle options
- `Viper.GUI.RadioButton` / `RadioGroup` - Exclusive selection
- `Viper.GUI.Slider` - Value slider
- `Viper.GUI.ListBox` - Scrollable item list
- `Viper.GUI.ProgressBar` - Progress indicator
- `Viper.GUI.Spinner` - Numeric input
- Button styles (default, primary, danger)

## Running

```bash
# From the viper directory
./build/tools/ilc/ilc demos/viperlang/vedit/simple.viper --run

# Or the full editor
./build/tools/ilc/ilc demos/viperlang/vedit/main.viper --run
```

## GUI Widgets Used

| Widget | Purpose |
|--------|---------|
| `App` | Main window and event loop |
| `VBox` | Vertical layout container |
| `HBox` | Horizontal layout container |
| `Label` | Text display |
| `Button` | Clickable button |
| `TextInput` | Single-line text input |
| `CodeEditor` | Multi-line code editor |
| `TabBar` | Tabbed document interface |
| `Theme` | Dark/light theme switching |

## Example: Basic GUI Application

```viper
func main() {
    // Create window
    var app = Viper.GUI.App.New("My App", 800, 600);
    Viper.GUI.Theme.SetDark();

    // Add a button
    var btn = Viper.GUI.Button.New(app.Root, "Click Me");
    btn.SetSize(120, 32);

    // Event loop
    while app.ShouldClose == 0 {
        app.Poll();

        if btn.WasClicked() != 0 {
            Viper.Terminal.Say("Button clicked!");
        }

        app.Render();
    }

    app.Destroy();
}
```
