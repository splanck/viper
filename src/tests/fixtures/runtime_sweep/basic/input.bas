' EXPECT_OUT: RESULT: ok
' COVER: Viper.Input.Key.Unknown
' COVER: Viper.Input.Key.A
' COVER: Viper.Input.Key.B
' COVER: Viper.Input.Key.C
' COVER: Viper.Input.Key.D
' COVER: Viper.Input.Key.E
' COVER: Viper.Input.Key.F
' COVER: Viper.Input.Key.G
' COVER: Viper.Input.Key.H
' COVER: Viper.Input.Key.I
' COVER: Viper.Input.Key.J
' COVER: Viper.Input.Key.K
' COVER: Viper.Input.Key.L
' COVER: Viper.Input.Key.M
' COVER: Viper.Input.Key.N
' COVER: Viper.Input.Key.O
' COVER: Viper.Input.Key.P
' COVER: Viper.Input.Key.Q
' COVER: Viper.Input.Key.R
' COVER: Viper.Input.Key.S
' COVER: Viper.Input.Key.T
' COVER: Viper.Input.Key.U
' COVER: Viper.Input.Key.V
' COVER: Viper.Input.Key.W
' COVER: Viper.Input.Key.X
' COVER: Viper.Input.Key.Y
' COVER: Viper.Input.Key.Z
' COVER: Viper.Input.Key.Digit0
' COVER: Viper.Input.Key.Digit1
' COVER: Viper.Input.Key.Digit2
' COVER: Viper.Input.Key.Digit3
' COVER: Viper.Input.Key.Digit4
' COVER: Viper.Input.Key.Digit5
' COVER: Viper.Input.Key.Digit6
' COVER: Viper.Input.Key.Digit7
' COVER: Viper.Input.Key.Digit8
' COVER: Viper.Input.Key.Digit9
' COVER: Viper.Input.Key.F1
' COVER: Viper.Input.Key.F2
' COVER: Viper.Input.Key.F3
' COVER: Viper.Input.Key.F4
' COVER: Viper.Input.Key.F5
' COVER: Viper.Input.Key.F6
' COVER: Viper.Input.Key.F7
' COVER: Viper.Input.Key.F8
' COVER: Viper.Input.Key.F9
' COVER: Viper.Input.Key.F10
' COVER: Viper.Input.Key.F11
' COVER: Viper.Input.Key.F12
' COVER: Viper.Input.Key.Up
' COVER: Viper.Input.Key.Down
' COVER: Viper.Input.Key.Left
' COVER: Viper.Input.Key.Right
' COVER: Viper.Input.Key.Home
' COVER: Viper.Input.Key.End
' COVER: Viper.Input.Key.PageUp
' COVER: Viper.Input.Key.PageDown
' COVER: Viper.Input.Key.Insert
' COVER: Viper.Input.Key.Delete

FUNCTION IsBool(v AS INTEGER) AS BOOLEAN
    IsBool = (v = 0) OR (v = 1) OR (v = -1)
END FUNCTION
' COVER: Viper.Input.Key.Backspace
' COVER: Viper.Input.Key.Tab
' COVER: Viper.Input.Key.Enter
' COVER: Viper.Input.Key.Space
' COVER: Viper.Input.Key.Escape
' COVER: Viper.Input.Key.LeftShift
' COVER: Viper.Input.Key.RightShift
' COVER: Viper.Input.Key.LeftControl
' COVER: Viper.Input.Key.RightControl
' COVER: Viper.Input.Key.LeftAlt
' COVER: Viper.Input.Key.RightAlt
' COVER: Viper.Input.Key.Minus
' COVER: Viper.Input.Key.Equals
' COVER: Viper.Input.Key.LeftBracket
' COVER: Viper.Input.Key.RightBracket
' COVER: Viper.Input.Key.Backslash
' COVER: Viper.Input.Key.Semicolon
' COVER: Viper.Input.Key.Quote
' COVER: Viper.Input.Key.Grave
' COVER: Viper.Input.Key.Comma
' COVER: Viper.Input.Key.Period
' COVER: Viper.Input.Key.Slash
' COVER: Viper.Input.Key.Numpad0
' COVER: Viper.Input.Key.Numpad1
' COVER: Viper.Input.Key.Numpad2
' COVER: Viper.Input.Key.Numpad3
' COVER: Viper.Input.Key.Numpad4
' COVER: Viper.Input.Key.Numpad5
' COVER: Viper.Input.Key.Numpad6
' COVER: Viper.Input.Key.Numpad7
' COVER: Viper.Input.Key.Numpad8
' COVER: Viper.Input.Key.Numpad9
' COVER: Viper.Input.Key.NumpadAdd
' COVER: Viper.Input.Key.NumpadSubtract
' COVER: Viper.Input.Key.NumpadMultiply
' COVER: Viper.Input.Key.NumpadDivide
' COVER: Viper.Input.Key.NumpadEnter
' COVER: Viper.Input.Key.NumpadDecimal
' COVER: Viper.Input.Keyboard.IsDown
' COVER: Viper.Input.Keyboard.IsUp
' COVER: Viper.Input.Keyboard.AnyDown
' COVER: Viper.Input.Keyboard.GetDown
' COVER: Viper.Input.Keyboard.WasPressed
' COVER: Viper.Input.Keyboard.WasReleased
' COVER: Viper.Input.Keyboard.GetPressed
' COVER: Viper.Input.Keyboard.GetReleased
' COVER: Viper.Input.Keyboard.GetText
' COVER: Viper.Input.Keyboard.EnableTextInput
' COVER: Viper.Input.Keyboard.DisableTextInput
' COVER: Viper.Input.Keyboard.KeyName
' COVER: Viper.Input.Mouse.ButtonLeft
' COVER: Viper.Input.Mouse.ButtonRight
' COVER: Viper.Input.Mouse.ButtonMiddle
' COVER: Viper.Input.Mouse.ButtonX1
' COVER: Viper.Input.Mouse.ButtonX2
' COVER: Viper.Input.Mouse.X
' COVER: Viper.Input.Mouse.Y
' COVER: Viper.Input.Mouse.DeltaX
' COVER: Viper.Input.Mouse.DeltaY
' COVER: Viper.Input.Mouse.IsDown
' COVER: Viper.Input.Mouse.IsUp
' COVER: Viper.Input.Mouse.ButtonLeft
' COVER: Viper.Input.Mouse.ButtonRight
' COVER: Viper.Input.Mouse.ButtonMiddle
' COVER: Viper.Input.Mouse.WasPressed
' COVER: Viper.Input.Mouse.WasReleased
' COVER: Viper.Input.Mouse.WasClicked
' COVER: Viper.Input.Mouse.WasDoubleClicked
' COVER: Viper.Input.Mouse.WheelX
' COVER: Viper.Input.Mouse.WheelY
' COVER: Viper.Input.Mouse.Show
' COVER: Viper.Input.Mouse.Hide
' COVER: Viper.Input.Mouse.IsHidden
' COVER: Viper.Input.Mouse.Capture
' COVER: Viper.Input.Mouse.Release
' COVER: Viper.Input.Mouse.IsCaptured
' COVER: Viper.Input.Mouse.SetPosition
' COVER: Viper.Input.Pad.ButtonA
' COVER: Viper.Input.Pad.ButtonB
' COVER: Viper.Input.Pad.ButtonX
' COVER: Viper.Input.Pad.ButtonY
' COVER: Viper.Input.Pad.ButtonLeftBumper
' COVER: Viper.Input.Pad.ButtonRightBumper
' COVER: Viper.Input.Pad.ButtonBack
' COVER: Viper.Input.Pad.ButtonStart
' COVER: Viper.Input.Pad.ButtonLeftStick
' COVER: Viper.Input.Pad.ButtonRightStick
' COVER: Viper.Input.Pad.ButtonUp
' COVER: Viper.Input.Pad.ButtonDown
' COVER: Viper.Input.Pad.ButtonLeft
' COVER: Viper.Input.Pad.ButtonRight
' COVER: Viper.Input.Pad.ButtonGuide
' COVER: Viper.Input.Pad.Count
' COVER: Viper.Input.Pad.IsConnected
' COVER: Viper.Input.Pad.Name
' COVER: Viper.Input.Pad.IsDown
' COVER: Viper.Input.Pad.IsUp
' COVER: Viper.Input.Pad.WasPressed
' COVER: Viper.Input.Pad.WasReleased
' COVER: Viper.Input.Pad.LeftX
' COVER: Viper.Input.Pad.LeftY
' COVER: Viper.Input.Pad.RightX
' COVER: Viper.Input.Pad.RightY
' COVER: Viper.Input.Pad.LeftTrigger
' COVER: Viper.Input.Pad.RightTrigger
' COVER: Viper.Input.Pad.SetDeadzone
' COVER: Viper.Input.Pad.GetDeadzone
' COVER: Viper.Input.Pad.Vibrate
' COVER: Viper.Input.Pad.StopVibration

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Input Test", 16, 16)
canvas.Poll()

DIM keySum AS INTEGER
keySum = 0
keySum = keySum + Viper.Input.Key.Unknown
keySum = keySum + Viper.Input.Key.A
keySum = keySum + Viper.Input.Key.B
keySum = keySum + Viper.Input.Key.C
keySum = keySum + Viper.Input.Key.D
keySum = keySum + Viper.Input.Key.E
keySum = keySum + Viper.Input.Key.F
keySum = keySum + Viper.Input.Key.G
keySum = keySum + Viper.Input.Key.H
keySum = keySum + Viper.Input.Key.I
keySum = keySum + Viper.Input.Key.J
keySum = keySum + Viper.Input.Key.K
keySum = keySum + Viper.Input.Key.L
keySum = keySum + Viper.Input.Key.M
keySum = keySum + Viper.Input.Key.N
keySum = keySum + Viper.Input.Key.O
keySum = keySum + Viper.Input.Key.P
keySum = keySum + Viper.Input.Key.Q
keySum = keySum + Viper.Input.Key.R
keySum = keySum + Viper.Input.Key.S
keySum = keySum + Viper.Input.Key.T
keySum = keySum + Viper.Input.Key.U
keySum = keySum + Viper.Input.Key.V
keySum = keySum + Viper.Input.Key.W
keySum = keySum + Viper.Input.Key.X
keySum = keySum + Viper.Input.Key.Y
keySum = keySum + Viper.Input.Key.Z
keySum = keySum + Viper.Input.Key.Digit0
keySum = keySum + Viper.Input.Key.Digit1
keySum = keySum + Viper.Input.Key.Digit2
keySum = keySum + Viper.Input.Key.Digit3
keySum = keySum + Viper.Input.Key.Digit4
keySum = keySum + Viper.Input.Key.Digit5
keySum = keySum + Viper.Input.Key.Digit6
keySum = keySum + Viper.Input.Key.Digit7
keySum = keySum + Viper.Input.Key.Digit8
keySum = keySum + Viper.Input.Key.Digit9
keySum = keySum + Viper.Input.Key.F1
keySum = keySum + Viper.Input.Key.F2
keySum = keySum + Viper.Input.Key.F3
keySum = keySum + Viper.Input.Key.F4
keySum = keySum + Viper.Input.Key.F5
keySum = keySum + Viper.Input.Key.F6
keySum = keySum + Viper.Input.Key.F7
keySum = keySum + Viper.Input.Key.F8
keySum = keySum + Viper.Input.Key.F9
keySum = keySum + Viper.Input.Key.F10
keySum = keySum + Viper.Input.Key.F11
keySum = keySum + Viper.Input.Key.F12
keySum = keySum + Viper.Input.Key.Up
keySum = keySum + Viper.Input.Key.Down
keySum = keySum + Viper.Input.Key.Left
keySum = keySum + Viper.Input.Key.Right
keySum = keySum + Viper.Input.Key.Home
keySum = keySum + Viper.Input.Key.End
keySum = keySum + Viper.Input.Key.PageUp
keySum = keySum + Viper.Input.Key.PageDown
keySum = keySum + Viper.Input.Key.Insert
keySum = keySum + Viper.Input.Key.Delete
keySum = keySum + Viper.Input.Key.Backspace
keySum = keySum + Viper.Input.Key.Tab
keySum = keySum + Viper.Input.Key.Enter
keySum = keySum + Viper.Input.Key.Space
keySum = keySum + Viper.Input.Key.Escape
keySum = keySum + Viper.Input.Key.LeftShift
keySum = keySum + Viper.Input.Key.RightShift
keySum = keySum + Viper.Input.Key.LeftControl
keySum = keySum + Viper.Input.Key.RightControl
keySum = keySum + Viper.Input.Key.LeftAlt
keySum = keySum + Viper.Input.Key.RightAlt
keySum = keySum + Viper.Input.Key.Minus
keySum = keySum + Viper.Input.Key.Equals
keySum = keySum + Viper.Input.Key.LeftBracket
keySum = keySum + Viper.Input.Key.RightBracket
keySum = keySum + Viper.Input.Key.Backslash
keySum = keySum + Viper.Input.Key.Semicolon
keySum = keySum + Viper.Input.Key.Quote
keySum = keySum + Viper.Input.Key.Grave
keySum = keySum + Viper.Input.Key.Comma
keySum = keySum + Viper.Input.Key.Period
keySum = keySum + Viper.Input.Key.Slash
keySum = keySum + Viper.Input.Key.Numpad0
keySum = keySum + Viper.Input.Key.Numpad1
keySum = keySum + Viper.Input.Key.Numpad2
keySum = keySum + Viper.Input.Key.Numpad3
keySum = keySum + Viper.Input.Key.Numpad4
keySum = keySum + Viper.Input.Key.Numpad5
keySum = keySum + Viper.Input.Key.Numpad6
keySum = keySum + Viper.Input.Key.Numpad7
keySum = keySum + Viper.Input.Key.Numpad8
keySum = keySum + Viper.Input.Key.Numpad9
keySum = keySum + Viper.Input.Key.NumpadAdd
keySum = keySum + Viper.Input.Key.NumpadSubtract
keySum = keySum + Viper.Input.Key.NumpadMultiply
keySum = keySum + Viper.Input.Key.NumpadDivide
keySum = keySum + Viper.Input.Key.NumpadEnter
keySum = keySum + Viper.Input.Key.NumpadDecimal
Viper.Core.Diagnostics.Assert(keySum > 0, "key.sum")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Key.A, 65, "key.a")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Key.Digit0, 48, "key.0")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Key.Space, 32, "key.space")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Key.F1, 290, "key.f1")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Key.Unknown, 0, "key.unknown")

DIM down AS INTEGER
down = Viper.Input.Keyboard.IsDown(Viper.Input.Key.A)
Viper.Core.Diagnostics.Assert(IsBool(down), "key.isdown")
DIM up AS INTEGER
up = Viper.Input.Keyboard.IsUp(Viper.Input.Key.A)
Viper.Core.Diagnostics.Assert(IsBool(up), "key.isup")
DIM anyDown AS INTEGER
anyDown = Viper.Input.Keyboard.AnyDown()
Viper.Core.Diagnostics.Assert(IsBool(anyDown), "key.anydown")
DIM downKey AS INTEGER
downKey = Viper.Input.Keyboard.GetDown()
Viper.Core.Diagnostics.Assert(downKey >= 0, "key.getdown")
DIM wasPressed AS INTEGER
wasPressed = Viper.Input.Keyboard.WasPressed(Viper.Input.Key.A)
Viper.Core.Diagnostics.Assert(IsBool(wasPressed), "key.waspressed")
DIM wasReleased AS INTEGER
wasReleased = Viper.Input.Keyboard.WasReleased(Viper.Input.Key.A)
Viper.Core.Diagnostics.Assert(IsBool(wasReleased), "key.wasreleased")
DIM pressed AS Viper.Collections.Seq
pressed = Viper.Input.Keyboard.GetPressed()
Viper.Core.Diagnostics.Assert(pressed.Count >= 0, "key.getpressed")
DIM released AS Viper.Collections.Seq
released = Viper.Input.Keyboard.GetReleased()
Viper.Core.Diagnostics.Assert(released.Count >= 0, "key.getreleased")
Viper.Input.Keyboard.EnableTextInput()
canvas.Poll()
DIM text AS STRING
text = Viper.Input.Keyboard.GetText()
Viper.Core.Diagnostics.Assert(text.Length >= 0, "key.gettext")
Viper.Input.Keyboard.DisableTextInput()
DIM shiftDown AS INTEGER
shiftDown = Viper.Input.Keyboard.IsDown(Viper.Input.Key.LeftShift)
Viper.Core.Diagnostics.Assert(IsBool(shiftDown), "key.shift")
DIM ctrlDown AS INTEGER
ctrlDown = Viper.Input.Keyboard.IsDown(Viper.Input.Key.LeftControl)
Viper.Core.Diagnostics.Assert(IsBool(ctrlDown), "key.ctrl")
DIM altDown AS INTEGER
altDown = Viper.Input.Keyboard.IsDown(Viper.Input.Key.LeftAlt)
Viper.Core.Diagnostics.Assert(IsBool(altDown), "key.alt")
DIM keyName AS STRING
keyName = Viper.Input.Keyboard.KeyName(Viper.Input.Key.A)
Viper.Core.Diagnostics.AssertEqStr(keyName, "A", "key.name")

DIM btnSum AS INTEGER
btnSum = 0
btnSum = btnSum + Viper.Input.Mouse.ButtonLeft
btnSum = btnSum + Viper.Input.Mouse.ButtonRight
btnSum = btnSum + Viper.Input.Mouse.ButtonMiddle
btnSum = btnSum + Viper.Input.Mouse.ButtonX1
btnSum = btnSum + Viper.Input.Mouse.ButtonX2
Viper.Core.Diagnostics.Assert(btnSum > 0, "mouse.btnsum")
Viper.Input.Mouse.Hide()
Viper.Input.Mouse.Show()
DIM hidden AS INTEGER
hidden = Viper.Input.Mouse.IsHidden()
Viper.Core.Diagnostics.Assert(IsBool(hidden), "mouse.hidden")
Viper.Input.Mouse.Capture()
DIM captured AS INTEGER
captured = Viper.Input.Mouse.IsCaptured()
Viper.Core.Diagnostics.Assert(IsBool(captured), "mouse.captured")
Viper.Input.Mouse.Release()
Viper.Input.Mouse.SetPosition(1, 1)
DIM mx AS INTEGER
mx = Viper.Input.Mouse.X()
DIM my AS INTEGER
my = Viper.Input.Mouse.Y()
DIM dx AS INTEGER
dx = Viper.Input.Mouse.DeltaX()
DIM dy AS INTEGER
dy = Viper.Input.Mouse.DeltaY()
DIM mdown AS INTEGER
mdown = Viper.Input.Mouse.IsDown(Viper.Input.Mouse.ButtonLeft)
Viper.Core.Diagnostics.Assert(IsBool(mdown), "mouse.isdown")
DIM mup AS INTEGER
mup = Viper.Input.Mouse.IsUp(Viper.Input.Mouse.ButtonLeft)
Viper.Core.Diagnostics.Assert(IsBool(mup), "mouse.isup")
DIM leftDown AS INTEGER
leftDown = Viper.Input.Mouse.IsDown(Viper.Input.Mouse.ButtonLeft)
DIM rightDown AS INTEGER
rightDown = Viper.Input.Mouse.IsDown(Viper.Input.Mouse.ButtonRight)
DIM middleDown AS INTEGER
middleDown = Viper.Input.Mouse.IsDown(Viper.Input.Mouse.ButtonMiddle)
DIM wasPressM AS INTEGER
wasPressM = Viper.Input.Mouse.WasPressed(Viper.Input.Mouse.ButtonLeft)
DIM wasRelM AS INTEGER
wasRelM = Viper.Input.Mouse.WasReleased(Viper.Input.Mouse.ButtonLeft)
DIM wasClick AS INTEGER
wasClick = Viper.Input.Mouse.WasClicked(Viper.Input.Mouse.ButtonLeft)
DIM wasDbl AS INTEGER
wasDbl = Viper.Input.Mouse.WasDoubleClicked(Viper.Input.Mouse.ButtonLeft)
DIM wheelX AS INTEGER
wheelX = Viper.Input.Mouse.WheelX()
DIM wheelY AS INTEGER
wheelY = Viper.Input.Mouse.WheelY()
Viper.Core.Diagnostics.Assert(leftDown = leftDown, "mouse.left")
Viper.Core.Diagnostics.Assert(rightDown = rightDown, "mouse.right")
Viper.Core.Diagnostics.Assert(middleDown = middleDown, "mouse.middle")
Viper.Core.Diagnostics.Assert(wasPressM = wasPressM, "mouse.waspressed")
Viper.Core.Diagnostics.Assert(wasRelM = wasRelM, "mouse.wasreleased")
Viper.Core.Diagnostics.Assert(wasClick = wasClick, "mouse.wasclicked")
Viper.Core.Diagnostics.Assert(wasDbl = wasDbl, "mouse.wasdouble")
Viper.Core.Diagnostics.Assert(wheelX = wheelX, "mouse.wheelx")
Viper.Core.Diagnostics.Assert(wheelY = wheelY, "mouse.wheely")
Viper.Core.Diagnostics.Assert(mx = mx, "mouse.x")
Viper.Core.Diagnostics.Assert(my = my, "mouse.y")
Viper.Core.Diagnostics.Assert(dx = dx, "mouse.dx")
Viper.Core.Diagnostics.Assert(dy = dy, "mouse.dy")

DIM padSum AS INTEGER
padSum = 0
padSum = padSum + Viper.Input.Pad.ButtonA
padSum = padSum + Viper.Input.Pad.ButtonB
padSum = padSum + Viper.Input.Pad.ButtonX
padSum = padSum + Viper.Input.Pad.ButtonY
padSum = padSum + Viper.Input.Pad.ButtonLeftBumper
padSum = padSum + Viper.Input.Pad.ButtonRightBumper
padSum = padSum + Viper.Input.Pad.ButtonBack
padSum = padSum + Viper.Input.Pad.ButtonStart
padSum = padSum + Viper.Input.Pad.ButtonLeftStick
padSum = padSum + Viper.Input.Pad.ButtonRightStick
padSum = padSum + Viper.Input.Pad.ButtonUp
padSum = padSum + Viper.Input.Pad.ButtonDown
padSum = padSum + Viper.Input.Pad.ButtonLeft
padSum = padSum + Viper.Input.Pad.ButtonRight
padSum = padSum + Viper.Input.Pad.ButtonGuide
Viper.Core.Diagnostics.Assert(padSum > 0, "pad.sum")
DIM padCount AS INTEGER
padCount = Viper.Input.Pad.Count()
Viper.Core.Diagnostics.Assert(padCount >= 0, "pad.count")
DIM padConnected AS INTEGER
padConnected = Viper.Input.Pad.IsConnected(0)
Viper.Core.Diagnostics.Assert(padConnected = 0 OR padConnected = 1, "pad.connected")
DIM padName AS STRING
padName = Viper.Input.Pad.Name(0)
Viper.Core.Diagnostics.Assert(padName.Length >= 0, "pad.name")
DIM padDown AS INTEGER
padDown = Viper.Input.Pad.IsDown(0, Viper.Input.Pad.ButtonA)
DIM padUp AS INTEGER
padUp = Viper.Input.Pad.IsUp(0, Viper.Input.Pad.ButtonA)
DIM padPressed AS INTEGER
padPressed = Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.ButtonA)
DIM padReleased AS INTEGER
padReleased = Viper.Input.Pad.WasReleased(0, Viper.Input.Pad.ButtonA)
Viper.Core.Diagnostics.Assert(padDown = padDown, "pad.isdown")
Viper.Core.Diagnostics.Assert(padUp = padUp, "pad.isup")
Viper.Core.Diagnostics.Assert(padPressed = padPressed, "pad.waspressed")
Viper.Core.Diagnostics.Assert(padReleased = padReleased, "pad.wasreleased")
DIM lx AS DOUBLE
lx = Viper.Input.Pad.LeftX(0)
DIM ly AS DOUBLE
ly = Viper.Input.Pad.LeftY(0)
DIM rx AS DOUBLE
rx = Viper.Input.Pad.RightX(0)
DIM ry AS DOUBLE
ry = Viper.Input.Pad.RightY(0)
DIM lt AS DOUBLE
lt = Viper.Input.Pad.LeftTrigger(0)
DIM rt AS DOUBLE
rt = Viper.Input.Pad.RightTrigger(0)
Viper.Core.Diagnostics.Assert(lx >= -1 AND lx <= 1, "pad.leftx")
Viper.Core.Diagnostics.Assert(ly >= -1 AND ly <= 1, "pad.lefty")
Viper.Core.Diagnostics.Assert(rx >= -1 AND rx <= 1, "pad.rightx")
Viper.Core.Diagnostics.Assert(ry >= -1 AND ry <= 1, "pad.righty")
Viper.Core.Diagnostics.Assert(lt >= 0 AND lt <= 1, "pad.lefttrigger")
Viper.Core.Diagnostics.Assert(rt >= 0 AND rt <= 1, "pad.righttrigger")
Viper.Input.Pad.SetDeadzone(0.2)
DIM dz AS DOUBLE
dz = Viper.Input.Pad.GetDeadzone()
AssertApprox(dz, 0.2, 0.0001, "pad.deadzone")
Viper.Input.Pad.Vibrate(0, 0.1, 0.1)
Viper.Input.Pad.StopVibration(0)

PRINT "RESULT: ok"
END
