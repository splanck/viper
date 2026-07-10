' EXPECT_OUT: RESULT: ok
' COVER: Viper.Input.Keyboard.KeyUnknown
' COVER: Viper.Input.Keyboard.KeyA
' COVER: Viper.Input.Keyboard.KeyB
' COVER: Viper.Input.Keyboard.KeyC
' COVER: Viper.Input.Keyboard.KeyD
' COVER: Viper.Input.Keyboard.KeyE
' COVER: Viper.Input.Keyboard.KeyF
' COVER: Viper.Input.Keyboard.KeyG
' COVER: Viper.Input.Keyboard.KeyH
' COVER: Viper.Input.Keyboard.KeyI
' COVER: Viper.Input.Keyboard.KeyJ
' COVER: Viper.Input.Keyboard.KeyK
' COVER: Viper.Input.Keyboard.KeyL
' COVER: Viper.Input.Keyboard.KeyM
' COVER: Viper.Input.Keyboard.KeyN
' COVER: Viper.Input.Keyboard.KeyO
' COVER: Viper.Input.Keyboard.KeyP
' COVER: Viper.Input.Keyboard.KeyQ
' COVER: Viper.Input.Keyboard.KeyR
' COVER: Viper.Input.Keyboard.KeyS
' COVER: Viper.Input.Keyboard.KeyT
' COVER: Viper.Input.Keyboard.KeyU
' COVER: Viper.Input.Keyboard.KeyV
' COVER: Viper.Input.Keyboard.KeyW
' COVER: Viper.Input.Keyboard.KeyX
' COVER: Viper.Input.Keyboard.KeyY
' COVER: Viper.Input.Keyboard.KeyZ
' COVER: Viper.Input.Keyboard.Key0
' COVER: Viper.Input.Keyboard.Key1
' COVER: Viper.Input.Keyboard.Key2
' COVER: Viper.Input.Keyboard.Key3
' COVER: Viper.Input.Keyboard.Key4
' COVER: Viper.Input.Keyboard.Key5
' COVER: Viper.Input.Keyboard.Key6
' COVER: Viper.Input.Keyboard.Key7
' COVER: Viper.Input.Keyboard.Key8
' COVER: Viper.Input.Keyboard.Key9
' COVER: Viper.Input.Keyboard.KeyF1
' COVER: Viper.Input.Keyboard.KeyF2
' COVER: Viper.Input.Keyboard.KeyF3
' COVER: Viper.Input.Keyboard.KeyF4
' COVER: Viper.Input.Keyboard.KeyF5
' COVER: Viper.Input.Keyboard.KeyF6
' COVER: Viper.Input.Keyboard.KeyF7
' COVER: Viper.Input.Keyboard.KeyF8
' COVER: Viper.Input.Keyboard.KeyF9
' COVER: Viper.Input.Keyboard.KeyF10
' COVER: Viper.Input.Keyboard.KeyF11
' COVER: Viper.Input.Keyboard.KeyF12
' COVER: Viper.Input.Keyboard.KeyUp
' COVER: Viper.Input.Keyboard.KeyDown
' COVER: Viper.Input.Keyboard.KeyLeft
' COVER: Viper.Input.Keyboard.KeyRight
' COVER: Viper.Input.Keyboard.KeyHome
' COVER: Viper.Input.Keyboard.KeyEnd
' COVER: Viper.Input.Keyboard.KeyPageUp
' COVER: Viper.Input.Keyboard.KeyPageDown
' COVER: Viper.Input.Keyboard.KeyInsert
' COVER: Viper.Input.Keyboard.KeyDelete

FUNCTION IsBool(v AS INTEGER) AS BOOLEAN
    IsBool = (v = 0) OR (v = 1) OR (v = -1)
END FUNCTION
' COVER: Viper.Input.Keyboard.KeyBackspace
' COVER: Viper.Input.Keyboard.KeyTab
' COVER: Viper.Input.Keyboard.KeyEnter
' COVER: Viper.Input.Keyboard.KeySpace
' COVER: Viper.Input.Keyboard.KeyEscape
' COVER: Viper.Input.Keyboard.KeyLeftShift
' COVER: Viper.Input.Keyboard.KeyRightShift
' COVER: Viper.Input.Keyboard.KeyLeftControl
' COVER: Viper.Input.Keyboard.KeyRightControl
' COVER: Viper.Input.Keyboard.KeyLeftAlt
' COVER: Viper.Input.Keyboard.KeyRightAlt
' COVER: Viper.Input.Keyboard.KeyMinus
' COVER: Viper.Input.Keyboard.KeyEquals
' COVER: Viper.Input.Keyboard.KeyLeftBracket
' COVER: Viper.Input.Keyboard.KeyRightBracket
' COVER: Viper.Input.Keyboard.KeyBackslash
' COVER: Viper.Input.Keyboard.KeySemicolon
' COVER: Viper.Input.Keyboard.KeyQuote
' COVER: Viper.Input.Keyboard.KeyGrave
' COVER: Viper.Input.Keyboard.KeyComma
' COVER: Viper.Input.Keyboard.KeyPeriod
' COVER: Viper.Input.Keyboard.KeySlash
' COVER: Viper.Input.Keyboard.KeyNum0
' COVER: Viper.Input.Keyboard.KeyNum1
' COVER: Viper.Input.Keyboard.KeyNum2
' COVER: Viper.Input.Keyboard.KeyNum3
' COVER: Viper.Input.Keyboard.KeyNum4
' COVER: Viper.Input.Keyboard.KeyNum5
' COVER: Viper.Input.Keyboard.KeyNum6
' COVER: Viper.Input.Keyboard.KeyNum7
' COVER: Viper.Input.Keyboard.KeyNum8
' COVER: Viper.Input.Keyboard.KeyNum9
' COVER: Viper.Input.Keyboard.KeyNumAdd
' COVER: Viper.Input.Keyboard.KeyNumSub
' COVER: Viper.Input.Keyboard.KeyNumMul
' COVER: Viper.Input.Keyboard.KeyNumDiv
' COVER: Viper.Input.Keyboard.KeyNumEnter
' COVER: Viper.Input.Keyboard.KeyNumDot
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
' COVER: Viper.Input.Keyboard.Shift
' COVER: Viper.Input.Keyboard.Ctrl
' COVER: Viper.Input.Keyboard.Alt
' COVER: Viper.Input.Keyboard.CapsLock
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
' COVER: Viper.Input.Mouse.Left
' COVER: Viper.Input.Mouse.Right
' COVER: Viper.Input.Mouse.Middle
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
' COVER: Viper.Input.Mouse.SetPos
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
keySum = keySum + Viper.Input.Keyboard.KeyUnknown
keySum = keySum + Viper.Input.Keyboard.KeyA
keySum = keySum + Viper.Input.Keyboard.KeyB
keySum = keySum + Viper.Input.Keyboard.KeyC
keySum = keySum + Viper.Input.Keyboard.KeyD
keySum = keySum + Viper.Input.Keyboard.KeyE
keySum = keySum + Viper.Input.Keyboard.KeyF
keySum = keySum + Viper.Input.Keyboard.KeyG
keySum = keySum + Viper.Input.Keyboard.KeyH
keySum = keySum + Viper.Input.Keyboard.KeyI
keySum = keySum + Viper.Input.Keyboard.KeyJ
keySum = keySum + Viper.Input.Keyboard.KeyK
keySum = keySum + Viper.Input.Keyboard.KeyL
keySum = keySum + Viper.Input.Keyboard.KeyM
keySum = keySum + Viper.Input.Keyboard.KeyN
keySum = keySum + Viper.Input.Keyboard.KeyO
keySum = keySum + Viper.Input.Keyboard.KeyP
keySum = keySum + Viper.Input.Keyboard.KeyQ
keySum = keySum + Viper.Input.Keyboard.KeyR
keySum = keySum + Viper.Input.Keyboard.KeyS
keySum = keySum + Viper.Input.Keyboard.KeyT
keySum = keySum + Viper.Input.Keyboard.KeyU
keySum = keySum + Viper.Input.Keyboard.KeyV
keySum = keySum + Viper.Input.Keyboard.KeyW
keySum = keySum + Viper.Input.Keyboard.KeyX
keySum = keySum + Viper.Input.Keyboard.KeyY
keySum = keySum + Viper.Input.Keyboard.KeyZ
keySum = keySum + Viper.Input.Keyboard.Key0
keySum = keySum + Viper.Input.Keyboard.Key1
keySum = keySum + Viper.Input.Keyboard.Key2
keySum = keySum + Viper.Input.Keyboard.Key3
keySum = keySum + Viper.Input.Keyboard.Key4
keySum = keySum + Viper.Input.Keyboard.Key5
keySum = keySum + Viper.Input.Keyboard.Key6
keySum = keySum + Viper.Input.Keyboard.Key7
keySum = keySum + Viper.Input.Keyboard.Key8
keySum = keySum + Viper.Input.Keyboard.Key9
keySum = keySum + Viper.Input.Keyboard.KeyF1
keySum = keySum + Viper.Input.Keyboard.KeyF2
keySum = keySum + Viper.Input.Keyboard.KeyF3
keySum = keySum + Viper.Input.Keyboard.KeyF4
keySum = keySum + Viper.Input.Keyboard.KeyF5
keySum = keySum + Viper.Input.Keyboard.KeyF6
keySum = keySum + Viper.Input.Keyboard.KeyF7
keySum = keySum + Viper.Input.Keyboard.KeyF8
keySum = keySum + Viper.Input.Keyboard.KeyF9
keySum = keySum + Viper.Input.Keyboard.KeyF10
keySum = keySum + Viper.Input.Keyboard.KeyF11
keySum = keySum + Viper.Input.Keyboard.KeyF12
keySum = keySum + Viper.Input.Keyboard.KeyUp
keySum = keySum + Viper.Input.Keyboard.KeyDown
keySum = keySum + Viper.Input.Keyboard.KeyLeft
keySum = keySum + Viper.Input.Keyboard.KeyRight
keySum = keySum + Viper.Input.Keyboard.KeyHome
keySum = keySum + Viper.Input.Keyboard.KeyEnd
keySum = keySum + Viper.Input.Keyboard.KeyPageUp
keySum = keySum + Viper.Input.Keyboard.KeyPageDown
keySum = keySum + Viper.Input.Keyboard.KeyInsert
keySum = keySum + Viper.Input.Keyboard.KeyDelete
keySum = keySum + Viper.Input.Keyboard.KeyBackspace
keySum = keySum + Viper.Input.Keyboard.KeyTab
keySum = keySum + Viper.Input.Keyboard.KeyEnter
keySum = keySum + Viper.Input.Keyboard.KeySpace
keySum = keySum + Viper.Input.Keyboard.KeyEscape
keySum = keySum + Viper.Input.Keyboard.KeyLeftShift
keySum = keySum + Viper.Input.Keyboard.KeyRightShift
keySum = keySum + Viper.Input.Keyboard.KeyLeftControl
keySum = keySum + Viper.Input.Keyboard.KeyRightControl
keySum = keySum + Viper.Input.Keyboard.KeyLeftAlt
keySum = keySum + Viper.Input.Keyboard.KeyRightAlt
keySum = keySum + Viper.Input.Keyboard.KeyMinus
keySum = keySum + Viper.Input.Keyboard.KeyEquals
keySum = keySum + Viper.Input.Keyboard.KeyLeftBracket
keySum = keySum + Viper.Input.Keyboard.KeyRightBracket
keySum = keySum + Viper.Input.Keyboard.KeyBackslash
keySum = keySum + Viper.Input.Keyboard.KeySemicolon
keySum = keySum + Viper.Input.Keyboard.KeyQuote
keySum = keySum + Viper.Input.Keyboard.KeyGrave
keySum = keySum + Viper.Input.Keyboard.KeyComma
keySum = keySum + Viper.Input.Keyboard.KeyPeriod
keySum = keySum + Viper.Input.Keyboard.KeySlash
keySum = keySum + Viper.Input.Keyboard.KeyNum0
keySum = keySum + Viper.Input.Keyboard.KeyNum1
keySum = keySum + Viper.Input.Keyboard.KeyNum2
keySum = keySum + Viper.Input.Keyboard.KeyNum3
keySum = keySum + Viper.Input.Keyboard.KeyNum4
keySum = keySum + Viper.Input.Keyboard.KeyNum5
keySum = keySum + Viper.Input.Keyboard.KeyNum6
keySum = keySum + Viper.Input.Keyboard.KeyNum7
keySum = keySum + Viper.Input.Keyboard.KeyNum8
keySum = keySum + Viper.Input.Keyboard.KeyNum9
keySum = keySum + Viper.Input.Keyboard.KeyNumAdd
keySum = keySum + Viper.Input.Keyboard.KeyNumSub
keySum = keySum + Viper.Input.Keyboard.KeyNumMul
keySum = keySum + Viper.Input.Keyboard.KeyNumDiv
keySum = keySum + Viper.Input.Keyboard.KeyNumEnter
keySum = keySum + Viper.Input.Keyboard.KeyNumDot
Viper.Core.Diagnostics.Assert(keySum > 0, "key.sum")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Keyboard.KeyA, 65, "key.a")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Keyboard.Key0, 48, "key.0")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Keyboard.KeySpace, 32, "key.space")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Keyboard.KeyF1, 290, "key.f1")
Viper.Core.Diagnostics.AssertEq(Viper.Input.Keyboard.KeyUnknown, 0, "key.unknown")

DIM down AS INTEGER
down = Viper.Input.Keyboard.IsDown(Viper.Input.Keyboard.KeyA)
Viper.Core.Diagnostics.Assert(IsBool(down), "key.isdown")
DIM up AS INTEGER
up = Viper.Input.Keyboard.IsUp(Viper.Input.Keyboard.KeyA)
Viper.Core.Diagnostics.Assert(IsBool(up), "key.isup")
DIM anyDown AS INTEGER
anyDown = Viper.Input.Keyboard.AnyDown()
Viper.Core.Diagnostics.Assert(IsBool(anyDown), "key.anydown")
DIM downKey AS INTEGER
downKey = Viper.Input.Keyboard.GetDown()
Viper.Core.Diagnostics.Assert(downKey >= 0, "key.getdown")
DIM wasPressed AS INTEGER
wasPressed = Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KeyA)
Viper.Core.Diagnostics.Assert(IsBool(wasPressed), "key.waspressed")
DIM wasReleased AS INTEGER
wasReleased = Viper.Input.Keyboard.WasReleased(Viper.Input.Keyboard.KeyA)
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
shiftDown = Viper.Input.Keyboard.Shift()
Viper.Core.Diagnostics.Assert(IsBool(shiftDown), "key.shift")
DIM ctrlDown AS INTEGER
ctrlDown = Viper.Input.Keyboard.Ctrl()
Viper.Core.Diagnostics.Assert(IsBool(ctrlDown), "key.ctrl")
DIM altDown AS INTEGER
altDown = Viper.Input.Keyboard.Alt()
Viper.Core.Diagnostics.Assert(IsBool(altDown), "key.alt")
DIM caps AS INTEGER
caps = Viper.Input.Keyboard.CapsLock()
Viper.Core.Diagnostics.Assert(IsBool(caps), "key.caps")
DIM keyName AS STRING
keyName = Viper.Input.Keyboard.KeyName(Viper.Input.Keyboard.KeyA)
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
Viper.Input.Mouse.SetPos(1, 1)
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
leftDown = Viper.Input.Mouse.Left()
DIM rightDown AS INTEGER
rightDown = Viper.Input.Mouse.Right()
DIM middleDown AS INTEGER
middleDown = Viper.Input.Mouse.Middle()
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
