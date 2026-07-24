' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Input.Key.Unknown
' COVER: Zanna.Input.Key.A
' COVER: Zanna.Input.Key.B
' COVER: Zanna.Input.Key.C
' COVER: Zanna.Input.Key.D
' COVER: Zanna.Input.Key.E
' COVER: Zanna.Input.Key.F
' COVER: Zanna.Input.Key.G
' COVER: Zanna.Input.Key.H
' COVER: Zanna.Input.Key.I
' COVER: Zanna.Input.Key.J
' COVER: Zanna.Input.Key.K
' COVER: Zanna.Input.Key.L
' COVER: Zanna.Input.Key.M
' COVER: Zanna.Input.Key.N
' COVER: Zanna.Input.Key.O
' COVER: Zanna.Input.Key.P
' COVER: Zanna.Input.Key.Q
' COVER: Zanna.Input.Key.R
' COVER: Zanna.Input.Key.S
' COVER: Zanna.Input.Key.T
' COVER: Zanna.Input.Key.U
' COVER: Zanna.Input.Key.V
' COVER: Zanna.Input.Key.W
' COVER: Zanna.Input.Key.X
' COVER: Zanna.Input.Key.Y
' COVER: Zanna.Input.Key.Z
' COVER: Zanna.Input.Key.Digit0
' COVER: Zanna.Input.Key.Digit1
' COVER: Zanna.Input.Key.Digit2
' COVER: Zanna.Input.Key.Digit3
' COVER: Zanna.Input.Key.Digit4
' COVER: Zanna.Input.Key.Digit5
' COVER: Zanna.Input.Key.Digit6
' COVER: Zanna.Input.Key.Digit7
' COVER: Zanna.Input.Key.Digit8
' COVER: Zanna.Input.Key.Digit9
' COVER: Zanna.Input.Key.F1
' COVER: Zanna.Input.Key.F2
' COVER: Zanna.Input.Key.F3
' COVER: Zanna.Input.Key.F4
' COVER: Zanna.Input.Key.F5
' COVER: Zanna.Input.Key.F6
' COVER: Zanna.Input.Key.F7
' COVER: Zanna.Input.Key.F8
' COVER: Zanna.Input.Key.F9
' COVER: Zanna.Input.Key.F10
' COVER: Zanna.Input.Key.F11
' COVER: Zanna.Input.Key.F12
' COVER: Zanna.Input.Key.Up
' COVER: Zanna.Input.Key.Down
' COVER: Zanna.Input.Key.Left
' COVER: Zanna.Input.Key.Right
' COVER: Zanna.Input.Key.Home
' COVER: Zanna.Input.Key.End
' COVER: Zanna.Input.Key.PageUp
' COVER: Zanna.Input.Key.PageDown
' COVER: Zanna.Input.Key.Insert
' COVER: Zanna.Input.Key.Delete

FUNCTION IsBool(v AS INTEGER) AS BOOLEAN
    IsBool = (v = 0) OR (v = 1) OR (v = -1)
END FUNCTION
' COVER: Zanna.Input.Key.Backspace
' COVER: Zanna.Input.Key.Tab
' COVER: Zanna.Input.Key.Enter
' COVER: Zanna.Input.Key.Space
' COVER: Zanna.Input.Key.Escape
' COVER: Zanna.Input.Key.LeftShift
' COVER: Zanna.Input.Key.RightShift
' COVER: Zanna.Input.Key.LeftControl
' COVER: Zanna.Input.Key.RightControl
' COVER: Zanna.Input.Key.LeftAlt
' COVER: Zanna.Input.Key.RightAlt
' COVER: Zanna.Input.Key.LeftSuper
' COVER: Zanna.Input.Key.RightSuper
' COVER: Zanna.Input.Key.Minus
' COVER: Zanna.Input.Key.Equals
' COVER: Zanna.Input.Key.LeftBracket
' COVER: Zanna.Input.Key.RightBracket
' COVER: Zanna.Input.Key.Backslash
' COVER: Zanna.Input.Key.Semicolon
' COVER: Zanna.Input.Key.Quote
' COVER: Zanna.Input.Key.Grave
' COVER: Zanna.Input.Key.Comma
' COVER: Zanna.Input.Key.Period
' COVER: Zanna.Input.Key.Slash
' COVER: Zanna.Input.Key.Numpad0
' COVER: Zanna.Input.Key.Numpad1
' COVER: Zanna.Input.Key.Numpad2
' COVER: Zanna.Input.Key.Numpad3
' COVER: Zanna.Input.Key.Numpad4
' COVER: Zanna.Input.Key.Numpad5
' COVER: Zanna.Input.Key.Numpad6
' COVER: Zanna.Input.Key.Numpad7
' COVER: Zanna.Input.Key.Numpad8
' COVER: Zanna.Input.Key.Numpad9
' COVER: Zanna.Input.Key.NumpadAdd
' COVER: Zanna.Input.Key.NumpadSubtract
' COVER: Zanna.Input.Key.NumpadMultiply
' COVER: Zanna.Input.Key.NumpadDivide
' COVER: Zanna.Input.Key.NumpadEnter
' COVER: Zanna.Input.Key.NumpadDecimal
' COVER: Zanna.Input.Keyboard.IsDown
' COVER: Zanna.Input.Keyboard.IsUp
' COVER: Zanna.Input.Keyboard.AnyDown
' COVER: Zanna.Input.Keyboard.GetDown
' COVER: Zanna.Input.Keyboard.WasPressed
' COVER: Zanna.Input.Keyboard.WasReleased
' COVER: Zanna.Input.Keyboard.GetPressed
' COVER: Zanna.Input.Keyboard.GetReleased
' COVER: Zanna.Input.Keyboard.GetText
' COVER: Zanna.Input.Keyboard.EnableTextInput
' COVER: Zanna.Input.Keyboard.DisableTextInput
' COVER: Zanna.Input.Keyboard.KeyName
' COVER: Zanna.Input.Mouse.ButtonLeft
' COVER: Zanna.Input.Mouse.ButtonRight
' COVER: Zanna.Input.Mouse.ButtonMiddle
' COVER: Zanna.Input.Mouse.ButtonX1
' COVER: Zanna.Input.Mouse.ButtonX2
' COVER: Zanna.Input.Mouse.X
' COVER: Zanna.Input.Mouse.Y
' COVER: Zanna.Input.Mouse.DeltaX
' COVER: Zanna.Input.Mouse.DeltaY
' COVER: Zanna.Input.Mouse.IsDown
' COVER: Zanna.Input.Mouse.IsUp
' COVER: Zanna.Input.Mouse.ButtonLeft
' COVER: Zanna.Input.Mouse.ButtonRight
' COVER: Zanna.Input.Mouse.ButtonMiddle
' COVER: Zanna.Input.Mouse.WasPressed
' COVER: Zanna.Input.Mouse.WasReleased
' COVER: Zanna.Input.Mouse.WasClicked
' COVER: Zanna.Input.Mouse.WasDoubleClicked
' COVER: Zanna.Input.Mouse.WheelX
' COVER: Zanna.Input.Mouse.WheelY
' COVER: Zanna.Input.Mouse.Show
' COVER: Zanna.Input.Mouse.Hide
' COVER: Zanna.Input.Mouse.IsHidden
' COVER: Zanna.Input.Mouse.Capture
' COVER: Zanna.Input.Mouse.Release
' COVER: Zanna.Input.Mouse.IsCaptured
' COVER: Zanna.Input.Mouse.SetPosition
' COVER: Zanna.Input.Pad.ButtonA
' COVER: Zanna.Input.Pad.ButtonB
' COVER: Zanna.Input.Pad.ButtonX
' COVER: Zanna.Input.Pad.ButtonY
' COVER: Zanna.Input.Pad.ButtonLeftBumper
' COVER: Zanna.Input.Pad.ButtonRightBumper
' COVER: Zanna.Input.Pad.ButtonBack
' COVER: Zanna.Input.Pad.ButtonStart
' COVER: Zanna.Input.Pad.ButtonLeftStick
' COVER: Zanna.Input.Pad.ButtonRightStick
' COVER: Zanna.Input.Pad.ButtonUp
' COVER: Zanna.Input.Pad.ButtonDown
' COVER: Zanna.Input.Pad.ButtonLeft
' COVER: Zanna.Input.Pad.ButtonRight
' COVER: Zanna.Input.Pad.ButtonGuide
' COVER: Zanna.Input.Pad.Count
' COVER: Zanna.Input.Pad.IsConnected
' COVER: Zanna.Input.Pad.Name
' COVER: Zanna.Input.Pad.IsDown
' COVER: Zanna.Input.Pad.IsUp
' COVER: Zanna.Input.Pad.WasPressed
' COVER: Zanna.Input.Pad.WasReleased
' COVER: Zanna.Input.Pad.LeftX
' COVER: Zanna.Input.Pad.LeftY
' COVER: Zanna.Input.Pad.RightX
' COVER: Zanna.Input.Pad.RightY
' COVER: Zanna.Input.Pad.LeftTrigger
' COVER: Zanna.Input.Pad.RightTrigger
' COVER: Zanna.Input.Pad.SetDeadzone
' COVER: Zanna.Input.Pad.GetDeadzone
' COVER: Zanna.Input.Pad.Vibrate
' COVER: Zanna.Input.Pad.StopVibration

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Zanna.Math.Abs(actual - expected) > eps THEN
        Zanna.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM canvas AS Zanna.Graphics.Canvas
canvas = NEW Zanna.Graphics.Canvas("Input Test", 16, 16)
canvas.Poll()

DIM keySum AS INTEGER
keySum = 0
keySum = keySum + Zanna.Input.Key.Unknown
keySum = keySum + Zanna.Input.Key.A
keySum = keySum + Zanna.Input.Key.B
keySum = keySum + Zanna.Input.Key.C
keySum = keySum + Zanna.Input.Key.D
keySum = keySum + Zanna.Input.Key.E
keySum = keySum + Zanna.Input.Key.F
keySum = keySum + Zanna.Input.Key.G
keySum = keySum + Zanna.Input.Key.H
keySum = keySum + Zanna.Input.Key.I
keySum = keySum + Zanna.Input.Key.J
keySum = keySum + Zanna.Input.Key.K
keySum = keySum + Zanna.Input.Key.L
keySum = keySum + Zanna.Input.Key.M
keySum = keySum + Zanna.Input.Key.N
keySum = keySum + Zanna.Input.Key.O
keySum = keySum + Zanna.Input.Key.P
keySum = keySum + Zanna.Input.Key.Q
keySum = keySum + Zanna.Input.Key.R
keySum = keySum + Zanna.Input.Key.S
keySum = keySum + Zanna.Input.Key.T
keySum = keySum + Zanna.Input.Key.U
keySum = keySum + Zanna.Input.Key.V
keySum = keySum + Zanna.Input.Key.W
keySum = keySum + Zanna.Input.Key.X
keySum = keySum + Zanna.Input.Key.Y
keySum = keySum + Zanna.Input.Key.Z
keySum = keySum + Zanna.Input.Key.Digit0
keySum = keySum + Zanna.Input.Key.Digit1
keySum = keySum + Zanna.Input.Key.Digit2
keySum = keySum + Zanna.Input.Key.Digit3
keySum = keySum + Zanna.Input.Key.Digit4
keySum = keySum + Zanna.Input.Key.Digit5
keySum = keySum + Zanna.Input.Key.Digit6
keySum = keySum + Zanna.Input.Key.Digit7
keySum = keySum + Zanna.Input.Key.Digit8
keySum = keySum + Zanna.Input.Key.Digit9
keySum = keySum + Zanna.Input.Key.F1
keySum = keySum + Zanna.Input.Key.F2
keySum = keySum + Zanna.Input.Key.F3
keySum = keySum + Zanna.Input.Key.F4
keySum = keySum + Zanna.Input.Key.F5
keySum = keySum + Zanna.Input.Key.F6
keySum = keySum + Zanna.Input.Key.F7
keySum = keySum + Zanna.Input.Key.F8
keySum = keySum + Zanna.Input.Key.F9
keySum = keySum + Zanna.Input.Key.F10
keySum = keySum + Zanna.Input.Key.F11
keySum = keySum + Zanna.Input.Key.F12
keySum = keySum + Zanna.Input.Key.Up
keySum = keySum + Zanna.Input.Key.Down
keySum = keySum + Zanna.Input.Key.Left
keySum = keySum + Zanna.Input.Key.Right
keySum = keySum + Zanna.Input.Key.Home
keySum = keySum + Zanna.Input.Key.End
keySum = keySum + Zanna.Input.Key.PageUp
keySum = keySum + Zanna.Input.Key.PageDown
keySum = keySum + Zanna.Input.Key.Insert
keySum = keySum + Zanna.Input.Key.Delete
keySum = keySum + Zanna.Input.Key.Backspace
keySum = keySum + Zanna.Input.Key.Tab
keySum = keySum + Zanna.Input.Key.Enter
keySum = keySum + Zanna.Input.Key.Space
keySum = keySum + Zanna.Input.Key.Escape
keySum = keySum + Zanna.Input.Key.LeftShift
keySum = keySum + Zanna.Input.Key.RightShift
keySum = keySum + Zanna.Input.Key.LeftControl
keySum = keySum + Zanna.Input.Key.RightControl
keySum = keySum + Zanna.Input.Key.LeftAlt
keySum = keySum + Zanna.Input.Key.RightAlt
keySum = keySum + Zanna.Input.Key.LeftSuper
keySum = keySum + Zanna.Input.Key.RightSuper
keySum = keySum + Zanna.Input.Key.Minus
keySum = keySum + Zanna.Input.Key.Equals
keySum = keySum + Zanna.Input.Key.LeftBracket
keySum = keySum + Zanna.Input.Key.RightBracket
keySum = keySum + Zanna.Input.Key.Backslash
keySum = keySum + Zanna.Input.Key.Semicolon
keySum = keySum + Zanna.Input.Key.Quote
keySum = keySum + Zanna.Input.Key.Grave
keySum = keySum + Zanna.Input.Key.Comma
keySum = keySum + Zanna.Input.Key.Period
keySum = keySum + Zanna.Input.Key.Slash
keySum = keySum + Zanna.Input.Key.Numpad0
keySum = keySum + Zanna.Input.Key.Numpad1
keySum = keySum + Zanna.Input.Key.Numpad2
keySum = keySum + Zanna.Input.Key.Numpad3
keySum = keySum + Zanna.Input.Key.Numpad4
keySum = keySum + Zanna.Input.Key.Numpad5
keySum = keySum + Zanna.Input.Key.Numpad6
keySum = keySum + Zanna.Input.Key.Numpad7
keySum = keySum + Zanna.Input.Key.Numpad8
keySum = keySum + Zanna.Input.Key.Numpad9
keySum = keySum + Zanna.Input.Key.NumpadAdd
keySum = keySum + Zanna.Input.Key.NumpadSubtract
keySum = keySum + Zanna.Input.Key.NumpadMultiply
keySum = keySum + Zanna.Input.Key.NumpadDivide
keySum = keySum + Zanna.Input.Key.NumpadEnter
keySum = keySum + Zanna.Input.Key.NumpadDecimal
Zanna.Core.Diagnostics.Assert(keySum > 0, "key.sum")
Zanna.Core.Diagnostics.AssertEq(Zanna.Input.Key.A, 65, "key.a")
Zanna.Core.Diagnostics.AssertEq(Zanna.Input.Key.Digit0, 48, "key.0")
Zanna.Core.Diagnostics.AssertEq(Zanna.Input.Key.Space, 32, "key.space")
Zanna.Core.Diagnostics.AssertEq(Zanna.Input.Key.F1, 290, "key.f1")
Zanna.Core.Diagnostics.AssertEq(Zanna.Input.Key.Unknown, 0, "key.unknown")

DIM down AS INTEGER
down = Zanna.Input.Keyboard.IsDown(Zanna.Input.Key.A)
Zanna.Core.Diagnostics.Assert(IsBool(down), "key.isdown")
DIM up AS INTEGER
up = Zanna.Input.Keyboard.IsUp(Zanna.Input.Key.A)
Zanna.Core.Diagnostics.Assert(IsBool(up), "key.isup")
DIM anyDown AS INTEGER
anyDown = Zanna.Input.Keyboard.AnyDown()
Zanna.Core.Diagnostics.Assert(IsBool(anyDown), "key.anydown")
DIM downKey AS INTEGER
downKey = Zanna.Input.Keyboard.GetDown()
Zanna.Core.Diagnostics.Assert(downKey >= 0, "key.getdown")
DIM wasPressed AS INTEGER
wasPressed = Zanna.Input.Keyboard.WasPressed(Zanna.Input.Key.A)
Zanna.Core.Diagnostics.Assert(IsBool(wasPressed), "key.waspressed")
DIM wasReleased AS INTEGER
wasReleased = Zanna.Input.Keyboard.WasReleased(Zanna.Input.Key.A)
Zanna.Core.Diagnostics.Assert(IsBool(wasReleased), "key.wasreleased")
DIM pressed AS Zanna.Collections.Seq
pressed = Zanna.Input.Keyboard.GetPressed()
Zanna.Core.Diagnostics.Assert(pressed.Count >= 0, "key.getpressed")
DIM released AS Zanna.Collections.Seq
released = Zanna.Input.Keyboard.GetReleased()
Zanna.Core.Diagnostics.Assert(released.Count >= 0, "key.getreleased")
Zanna.Input.Keyboard.EnableTextInput()
canvas.Poll()
DIM text AS STRING
text = Zanna.Input.Keyboard.GetText()
Zanna.Core.Diagnostics.Assert(text.Length >= 0, "key.gettext")
Zanna.Input.Keyboard.DisableTextInput()
DIM shiftDown AS INTEGER
shiftDown = Zanna.Input.Keyboard.IsDown(Zanna.Input.Key.LeftShift)
Zanna.Core.Diagnostics.Assert(IsBool(shiftDown), "key.shift")
DIM ctrlDown AS INTEGER
ctrlDown = Zanna.Input.Keyboard.IsDown(Zanna.Input.Key.LeftControl)
Zanna.Core.Diagnostics.Assert(IsBool(ctrlDown), "key.ctrl")
DIM altDown AS INTEGER
altDown = Zanna.Input.Keyboard.IsDown(Zanna.Input.Key.LeftAlt)
Zanna.Core.Diagnostics.Assert(IsBool(altDown), "key.alt")
DIM keyName AS STRING
keyName = Zanna.Input.Keyboard.KeyName(Zanna.Input.Key.A)
Zanna.Core.Diagnostics.AssertEqStr(keyName, "A", "key.name")

DIM btnSum AS INTEGER
btnSum = 0
btnSum = btnSum + Zanna.Input.Mouse.ButtonLeft
btnSum = btnSum + Zanna.Input.Mouse.ButtonRight
btnSum = btnSum + Zanna.Input.Mouse.ButtonMiddle
btnSum = btnSum + Zanna.Input.Mouse.ButtonX1
btnSum = btnSum + Zanna.Input.Mouse.ButtonX2
Zanna.Core.Diagnostics.Assert(btnSum > 0, "mouse.btnsum")
Zanna.Input.Mouse.Hide()
Zanna.Input.Mouse.Show()
DIM hidden AS INTEGER
hidden = Zanna.Input.Mouse.IsHidden()
Zanna.Core.Diagnostics.Assert(IsBool(hidden), "mouse.hidden")
Zanna.Input.Mouse.Capture()
DIM captured AS INTEGER
captured = Zanna.Input.Mouse.IsCaptured()
Zanna.Core.Diagnostics.Assert(IsBool(captured), "mouse.captured")
Zanna.Input.Mouse.Release()
Zanna.Input.Mouse.SetPosition(1, 1)
DIM mx AS INTEGER
mx = Zanna.Input.Mouse.X()
DIM my AS INTEGER
my = Zanna.Input.Mouse.Y()
DIM dx AS INTEGER
dx = Zanna.Input.Mouse.DeltaX()
DIM dy AS INTEGER
dy = Zanna.Input.Mouse.DeltaY()
DIM mdown AS INTEGER
mdown = Zanna.Input.Mouse.IsDown(Zanna.Input.Mouse.ButtonLeft)
Zanna.Core.Diagnostics.Assert(IsBool(mdown), "mouse.isdown")
DIM mup AS INTEGER
mup = Zanna.Input.Mouse.IsUp(Zanna.Input.Mouse.ButtonLeft)
Zanna.Core.Diagnostics.Assert(IsBool(mup), "mouse.isup")
DIM leftDown AS INTEGER
leftDown = Zanna.Input.Mouse.IsDown(Zanna.Input.Mouse.ButtonLeft)
DIM rightDown AS INTEGER
rightDown = Zanna.Input.Mouse.IsDown(Zanna.Input.Mouse.ButtonRight)
DIM middleDown AS INTEGER
middleDown = Zanna.Input.Mouse.IsDown(Zanna.Input.Mouse.ButtonMiddle)
DIM wasPressM AS INTEGER
wasPressM = Zanna.Input.Mouse.WasPressed(Zanna.Input.Mouse.ButtonLeft)
DIM wasRelM AS INTEGER
wasRelM = Zanna.Input.Mouse.WasReleased(Zanna.Input.Mouse.ButtonLeft)
DIM wasClick AS INTEGER
wasClick = Zanna.Input.Mouse.WasClicked(Zanna.Input.Mouse.ButtonLeft)
DIM wasDbl AS INTEGER
wasDbl = Zanna.Input.Mouse.WasDoubleClicked(Zanna.Input.Mouse.ButtonLeft)
DIM wheelX AS INTEGER
wheelX = Zanna.Input.Mouse.WheelX()
DIM wheelY AS INTEGER
wheelY = Zanna.Input.Mouse.WheelY()
Zanna.Core.Diagnostics.Assert(leftDown = leftDown, "mouse.left")
Zanna.Core.Diagnostics.Assert(rightDown = rightDown, "mouse.right")
Zanna.Core.Diagnostics.Assert(middleDown = middleDown, "mouse.middle")
Zanna.Core.Diagnostics.Assert(wasPressM = wasPressM, "mouse.waspressed")
Zanna.Core.Diagnostics.Assert(wasRelM = wasRelM, "mouse.wasreleased")
Zanna.Core.Diagnostics.Assert(wasClick = wasClick, "mouse.wasclicked")
Zanna.Core.Diagnostics.Assert(wasDbl = wasDbl, "mouse.wasdouble")
Zanna.Core.Diagnostics.Assert(wheelX = wheelX, "mouse.wheelx")
Zanna.Core.Diagnostics.Assert(wheelY = wheelY, "mouse.wheely")
Zanna.Core.Diagnostics.Assert(mx = mx, "mouse.x")
Zanna.Core.Diagnostics.Assert(my = my, "mouse.y")
Zanna.Core.Diagnostics.Assert(dx = dx, "mouse.dx")
Zanna.Core.Diagnostics.Assert(dy = dy, "mouse.dy")

DIM padSum AS INTEGER
padSum = 0
padSum = padSum + Zanna.Input.Pad.ButtonA
padSum = padSum + Zanna.Input.Pad.ButtonB
padSum = padSum + Zanna.Input.Pad.ButtonX
padSum = padSum + Zanna.Input.Pad.ButtonY
padSum = padSum + Zanna.Input.Pad.ButtonLeftBumper
padSum = padSum + Zanna.Input.Pad.ButtonRightBumper
padSum = padSum + Zanna.Input.Pad.ButtonBack
padSum = padSum + Zanna.Input.Pad.ButtonStart
padSum = padSum + Zanna.Input.Pad.ButtonLeftStick
padSum = padSum + Zanna.Input.Pad.ButtonRightStick
padSum = padSum + Zanna.Input.Pad.ButtonUp
padSum = padSum + Zanna.Input.Pad.ButtonDown
padSum = padSum + Zanna.Input.Pad.ButtonLeft
padSum = padSum + Zanna.Input.Pad.ButtonRight
padSum = padSum + Zanna.Input.Pad.ButtonGuide
Zanna.Core.Diagnostics.Assert(padSum > 0, "pad.sum")
DIM padCount AS INTEGER
padCount = Zanna.Input.Pad.Count()
Zanna.Core.Diagnostics.Assert(padCount >= 0, "pad.count")
DIM padConnected AS INTEGER
padConnected = Zanna.Input.Pad.IsConnected(0)
Zanna.Core.Diagnostics.Assert(padConnected = 0 OR padConnected = 1, "pad.connected")
DIM padName AS STRING
padName = Zanna.Input.Pad.Name(0)
Zanna.Core.Diagnostics.Assert(padName.Length >= 0, "pad.name")
DIM padDown AS INTEGER
padDown = Zanna.Input.Pad.IsDown(0, Zanna.Input.Pad.ButtonA)
DIM padUp AS INTEGER
padUp = Zanna.Input.Pad.IsUp(0, Zanna.Input.Pad.ButtonA)
DIM padPressed AS INTEGER
padPressed = Zanna.Input.Pad.WasPressed(0, Zanna.Input.Pad.ButtonA)
DIM padReleased AS INTEGER
padReleased = Zanna.Input.Pad.WasReleased(0, Zanna.Input.Pad.ButtonA)
Zanna.Core.Diagnostics.Assert(padDown = padDown, "pad.isdown")
Zanna.Core.Diagnostics.Assert(padUp = padUp, "pad.isup")
Zanna.Core.Diagnostics.Assert(padPressed = padPressed, "pad.waspressed")
Zanna.Core.Diagnostics.Assert(padReleased = padReleased, "pad.wasreleased")
DIM lx AS DOUBLE
lx = Zanna.Input.Pad.LeftX(0)
DIM ly AS DOUBLE
ly = Zanna.Input.Pad.LeftY(0)
DIM rx AS DOUBLE
rx = Zanna.Input.Pad.RightX(0)
DIM ry AS DOUBLE
ry = Zanna.Input.Pad.RightY(0)
DIM lt AS DOUBLE
lt = Zanna.Input.Pad.LeftTrigger(0)
DIM rt AS DOUBLE
rt = Zanna.Input.Pad.RightTrigger(0)
Zanna.Core.Diagnostics.Assert(lx >= -1 AND lx <= 1, "pad.leftx")
Zanna.Core.Diagnostics.Assert(ly >= -1 AND ly <= 1, "pad.lefty")
Zanna.Core.Diagnostics.Assert(rx >= -1 AND rx <= 1, "pad.rightx")
Zanna.Core.Diagnostics.Assert(ry >= -1 AND ry <= 1, "pad.righty")
Zanna.Core.Diagnostics.Assert(lt >= 0 AND lt <= 1, "pad.lefttrigger")
Zanna.Core.Diagnostics.Assert(rt >= 0 AND rt <= 1, "pad.righttrigger")
Zanna.Input.Pad.SetDeadzone(0.2)
DIM dz AS DOUBLE
dz = Zanna.Input.Pad.GetDeadzone()
AssertApprox(dz, 0.2, 0.0001, "pad.deadzone")
Zanna.Input.Pad.Vibrate(0, 0.1, 0.1)
Zanna.Input.Pad.StopVibration(0)

PRINT "RESULT: ok"
END
