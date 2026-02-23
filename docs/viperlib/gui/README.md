# GUI Widgets
> Window, layout, and widget types for building graphical user interfaces.

**Part of [Viper Runtime Library](../README.md)**

---

## Contents

| File | Contents |
|------|----------|
| [Core & Application](core.md) | App, Font, and common widget functions |
| [Layout Widgets](layout.md) | VBox, HBox, Grid, and other layout containers |
| [Basic Widgets](widgets.md) | Label, Button, TextInput, Checkbox, Slider, RadioButton, and more |
| [Containers & Advanced](containers.md) | ScrollView, SplitPane, TabBar, TreeView, CodeEditor, Minimap |
| [Application Components](application.md) | MenuBar, Toolbar, StatusBar, Dialogs, Notifications, Utilities, Themes |

## Complete Example (Zia)

```rust
module NoteEditor;

bind Viper.GUI.App as App;
bind Viper.GUI.VBox as VBox;
bind Viper.GUI.HBox as HBox;
bind Viper.GUI.Label as Label;
bind Viper.GUI.Button as Button;
bind Viper.GUI.CodeEditor as CodeEditor;
bind Viper.Fmt as Fmt;

func start() {
    var app = App.New("Note Editor", 800, 600);
    var root = app.get_Root();

    // Layout
    var vbox = VBox.New();
    vbox.SetSpacing(5.0);
    vbox.SetPadding(10.0);

    // Toolbar
    var toolbar = HBox.New();
    toolbar.SetSpacing(5.0);
    var newBtn = Button.New(toolbar, "New");
    newBtn.SetSize(60, 28);
    var saveBtn = Button.New(toolbar, "Save");
    saveBtn.SetStyle(1);
    saveBtn.SetSize(60, 28);

    // Editor
    var editor = CodeEditor.New(root);
    editor.SetSize(780, 500);

    // Status bar
    var status = Label.New(root, "Ready");

    while app.get_ShouldClose() == 0 {
        app.Poll();

        if newBtn.WasClicked() == 1 {
            editor.SetText("");
            status.SetText("New file");
        }
        if saveBtn.WasClicked() == 1 {
            editor.ClearModified();
            status.SetText("Saved!");
        }
        if editor.IsModified() == 1 {
            status.SetText("Modified - " + Fmt.Int(editor.get_LineCount()) + " lines");
        }

        app.Render();
    }
}
```

## Complete Example

```basic
' Complete GUI Application Example
DIM app AS Viper.GUI.App
app = NEW Viper.GUI.App("Note Editor", 800, 600)

' Load font
DIM font AS Viper.GUI.Font
font = Viper.GUI.Font.Load("consola.ttf")
app.SetFont(font, 12)

' Create layout
DIM vbox AS Viper.GUI.VBox
vbox = NEW Viper.GUI.VBox()
vbox.SetSpacing(5)
vbox.SetPadding(10)
app.Root.AddChild(vbox)

' Toolbar
DIM toolbar AS Viper.GUI.HBox
toolbar = NEW Viper.GUI.HBox()
toolbar.SetSpacing(5)
vbox.AddChild(toolbar)

DIM newBtn AS Viper.GUI.Button
newBtn = NEW Viper.GUI.Button(toolbar, "New")
newBtn.SetSize(60, 28)

DIM openBtn AS Viper.GUI.Button
openBtn = NEW Viper.GUI.Button(toolbar, "Open")
openBtn.SetSize(60, 28)

DIM saveBtn AS Viper.GUI.Button
saveBtn = NEW Viper.GUI.Button(toolbar, "Save")
saveBtn.SetStyle(1)  ' Primary
saveBtn.SetSize(60, 28)

' Editor
DIM editor AS Viper.GUI.CodeEditor
editor = NEW Viper.GUI.CodeEditor(vbox)
editor.SetSize(780, 500)

' Status bar
DIM status AS Viper.GUI.Label
status = NEW Viper.GUI.Label(vbox, "Ready")

' Main loop
DO WHILE app.ShouldClose = 0
    app.Poll()

    ' Handle buttons
    IF newBtn.WasClicked = 1 THEN
        editor.SetText("")
        status.SetText("New file")
    END IF

    IF saveBtn.WasClicked = 1 THEN
        ' Save logic here
        editor.ClearModified()
        status.SetText("Saved!")
    END IF

    ' Update status
    IF editor.IsModified() = 1 THEN
        status.SetText("Modified - " + STR$(editor.GetLineCount()) + " lines")
    END IF

    app.Render()
LOOP
```

---


## See Also

- [Graphics](../graphics/README.md) - Low-level graphics with Canvas
- [Input](../input.md) - Keyboard and mouse input
- [Audio](../audio.md) - Sound effects and music
