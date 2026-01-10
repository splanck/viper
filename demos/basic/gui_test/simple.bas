' Simple GUI Test for BASIC
' Tests Viper.GUI.* namespace bindings

DIM app AS OBJECT
DIM root AS OBJECT
DIM layout AS OBJECT
DIM label AS OBJECT
DIM button AS OBJECT
DIM status AS OBJECT

' Create application
app = Viper.GUI.App.New("BASIC GUI Test", 400, 300)
Viper.GUI.Theme.SetDark()

' Get root and create layout
root = app.Root
layout = Viper.GUI.VBox.New()
layout.SetSpacing(8.0)
layout.SetPadding(16.0)
root.AddChild(layout)

' Add title label
label = Viper.GUI.Label.New(layout, "Hello from BASIC!")
label.SetColor(4294967295)  ' White (0xFFFFFFFF)

' Add a button
button = Viper.GUI.Button.New(layout, "Click Me")
button.SetSize(120, 32)
button.SetStyle(1)

' Add status label
status = Viper.GUI.Label.New(layout, "Ready")
status.SetColor(4286611584)  ' Gray (0xFF888888)

DIM clickCount AS INTEGER
clickCount = 0

' Main loop
DO WHILE app.ShouldClose = 0
    app.Poll()

    IF button.WasClicked() <> 0 THEN
        clickCount = clickCount + 1
        status.SetText("Clicked " & STR$(clickCount) & " times")
    END IF

    app.Render()
LOOP

app.Destroy()
