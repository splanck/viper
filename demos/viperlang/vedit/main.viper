module vedit;

// ============================================================================
// VEDIT - A Simple Text Editor Demo
// ============================================================================
// Demonstrates the Viper.GUI widget library by building a functional
// text editor with multiple document support.
// ============================================================================

// Colors
var COLOR_WHITE = 0xFFFFFFFF;
var COLOR_GRAY = 0xFF888888;
var COLOR_DARK = 0xFF2D2D2D;

// Document management
var documentCount = 0;
var isModified = false;

func main() {
    // Initialize GUI application
    var app = Viper.GUI.App.New("VEdit - Viper Text Editor", 800, 600);

    // Set theme
    Viper.GUI.Theme.SetDark();

    // Main vertical container
    var mainContainer = Viper.GUI.VBox.New();
    mainContainer.SetSpacing(0.0);
    mainContainer.SetPadding(0.0);
    var root = app.Root;
    root.AddChild(mainContainer);

    // ========================================================================
    // Toolbar
    // ========================================================================
    var toolbar = Viper.GUI.HBox.New();
    toolbar.SetSpacing(8.0);
    toolbar.SetPadding(8.0);
    toolbar.SetSize(800, 40);
    mainContainer.AddChild(toolbar);

    // New button
    var btnNew = Viper.GUI.Button.New(toolbar, "New");
    btnNew.SetSize(60, 28);
    btnNew.SetStyle(0);

    // Open button
    var btnOpen = Viper.GUI.Button.New(toolbar, "Open");
    btnOpen.SetSize(60, 28);
    btnOpen.SetStyle(0);

    // Save button
    var btnSave = Viper.GUI.Button.New(toolbar, "Save");
    btnSave.SetSize(60, 28);
    btnSave.SetStyle(1);  // Primary style

    // Spacer label
    var spacer = Viper.GUI.Label.New(toolbar, "");
    spacer.SetSize(400, 28);

    // Title
    var title = Viper.GUI.Label.New(toolbar, "VEdit - Viper Text Editor");
    title.SetColor(COLOR_GRAY);

    // ========================================================================
    // Tab Bar
    // ========================================================================
    var tabBar = Viper.GUI.TabBar.New(mainContainer);
    tabBar.SetSize(800, 32);

    // ========================================================================
    // Editor
    // ========================================================================
    var editorArea = Viper.GUI.VBox.New();
    editorArea.SetSize(800, 490);
    mainContainer.AddChild(editorArea);

    var editor = Viper.GUI.CodeEditor.New(editorArea);
    editor.SetSize(800, 490);

    // Set initial content
    var welcomeText = "// Welcome to VEdit!\n";
    welcomeText = welcomeText + "// A simple text editor built with Viper.GUI\n";
    welcomeText = welcomeText + "\n";
    welcomeText = welcomeText + "func main() {\n";
    welcomeText = welcomeText + "    Viper.Terminal.Say(\"Hello, World!\");\n";
    welcomeText = welcomeText + "}\n";
    editor.SetText(welcomeText);
    editor.ClearModified();

    // ========================================================================
    // Status Bar
    // ========================================================================
    var statusBar = Viper.GUI.HBox.New();
    statusBar.SetSpacing(16.0);
    statusBar.SetPadding(4.0);
    statusBar.SetSize(800, 24);
    mainContainer.AddChild(statusBar);

    var statusLeft = Viper.GUI.Label.New(statusBar, "Lines: 0");
    statusLeft.SetColor(COLOR_GRAY);
    statusLeft.SetSize(200, 20);

    // Status spacer
    var statusSpacer = Viper.GUI.Label.New(statusBar, "");
    statusSpacer.SetSize(400, 20);

    var statusRight = Viper.GUI.Label.New(statusBar, "VEdit Demo");
    statusRight.SetColor(COLOR_GRAY);
    statusRight.SetSize(180, 20);

    // Create first document tab
    documentCount = 1;
    var tab = tabBar.AddTab("Untitled 1", true);
    tabBar.SetActive(tab);

    // ========================================================================
    // Main Event Loop
    // ========================================================================
    while app.ShouldClose == 0 {
        app.Poll();

        // Handle New button
        if btnNew.WasClicked() != 0 {
            documentCount = documentCount + 1;
            var newTab = tabBar.AddTab("Untitled " + documentCount, true);
            tabBar.SetActive(newTab);
            editor.SetText("");
            editor.ClearModified();
        }

        // Handle Open button (just creates a new doc for demo)
        if btnOpen.WasClicked() != 0 {
            documentCount = documentCount + 1;
            var openTab = tabBar.AddTab("Untitled " + documentCount, true);
            tabBar.SetActive(openTab);
            editor.SetText("// File opened (demo)\n// In a real app, this would show a file dialog.\n");
        }

        // Handle Save button
        if btnSave.WasClicked() != 0 {
            editor.ClearModified();
            statusLeft.SetText("Saved!");
        }

        // Update status bar
        var lineCount = editor.LineCount;
        if editor.IsModified() != 0 {
            statusLeft.SetText("Lines: " + lineCount + " [Modified]");
        } else {
            statusLeft.SetText("Lines: " + lineCount);
        }

        app.Render();
    }

    // Cleanup
    app.Destroy();
}
