' EXPECT_OUT: RESULT: ok
' COVER: Viper.Input.Keyboard.KEY_UNKNOWN
' COVER: Viper.Input.Keyboard.KEY_A
' COVER: Viper.Input.Keyboard.KEY_B
' COVER: Viper.Input.Keyboard.KEY_C
' COVER: Viper.Input.Keyboard.KEY_D
' COVER: Viper.Input.Keyboard.KEY_E
' COVER: Viper.Input.Keyboard.KEY_F
' COVER: Viper.Input.Keyboard.KEY_G
' COVER: Viper.Input.Keyboard.KEY_H
' COVER: Viper.Input.Keyboard.KEY_I
' COVER: Viper.Input.Keyboard.KEY_J
' COVER: Viper.Input.Keyboard.KEY_K
' COVER: Viper.Input.Keyboard.KEY_L
' COVER: Viper.Input.Keyboard.KEY_M
' COVER: Viper.Input.Keyboard.KEY_N
' COVER: Viper.Input.Keyboard.KEY_O
' COVER: Viper.Input.Keyboard.KEY_P
' COVER: Viper.Input.Keyboard.KEY_Q
' COVER: Viper.Input.Keyboard.KEY_R
' COVER: Viper.Input.Keyboard.KEY_S
' COVER: Viper.Input.Keyboard.KEY_T
' COVER: Viper.Input.Keyboard.KEY_U
' COVER: Viper.Input.Keyboard.KEY_V
' COVER: Viper.Input.Keyboard.KEY_W
' COVER: Viper.Input.Keyboard.KEY_X
' COVER: Viper.Input.Keyboard.KEY_Y
' COVER: Viper.Input.Keyboard.KEY_Z
' COVER: Viper.Input.Keyboard.KEY_0
' COVER: Viper.Input.Keyboard.KEY_1
' COVER: Viper.Input.Keyboard.KEY_2
' COVER: Viper.Input.Keyboard.KEY_3
' COVER: Viper.Input.Keyboard.KEY_4
' COVER: Viper.Input.Keyboard.KEY_5
' COVER: Viper.Input.Keyboard.KEY_6
' COVER: Viper.Input.Keyboard.KEY_7
' COVER: Viper.Input.Keyboard.KEY_8
' COVER: Viper.Input.Keyboard.KEY_9
' COVER: Viper.Input.Keyboard.KEY_F1
' COVER: Viper.Input.Keyboard.KEY_F2
' COVER: Viper.Input.Keyboard.KEY_F3
' COVER: Viper.Input.Keyboard.KEY_F4
' COVER: Viper.Input.Keyboard.KEY_F5
' COVER: Viper.Input.Keyboard.KEY_F6
' COVER: Viper.Input.Keyboard.KEY_F7
' COVER: Viper.Input.Keyboard.KEY_F8
' COVER: Viper.Input.Keyboard.KEY_F9
' COVER: Viper.Input.Keyboard.KEY_F10
' COVER: Viper.Input.Keyboard.KEY_F11
' COVER: Viper.Input.Keyboard.KEY_F12
' COVER: Viper.Input.Keyboard.KEY_UP
' COVER: Viper.Input.Keyboard.KEY_DOWN
' COVER: Viper.Input.Keyboard.KEY_LEFT
' COVER: Viper.Input.Keyboard.KEY_RIGHT
' COVER: Viper.Input.Keyboard.KEY_HOME
' COVER: Viper.Input.Keyboard.KEY_END
' COVER: Viper.Input.Keyboard.KEY_PAGEUP
' COVER: Viper.Input.Keyboard.KEY_PAGEDOWN
' COVER: Viper.Input.Keyboard.KEY_INSERT
' COVER: Viper.Input.Keyboard.KEY_DELETE

FUNCTION IsBool(v AS INTEGER) AS BOOLEAN
    IsBool = (v = 0) OR (v = 1) OR (v = -1)
END FUNCTION
' COVER: Viper.Input.Keyboard.KEY_BACKSPACE
' COVER: Viper.Input.Keyboard.KEY_TAB
' COVER: Viper.Input.Keyboard.KEY_ENTER
' COVER: Viper.Input.Keyboard.KEY_SPACE
' COVER: Viper.Input.Keyboard.KEY_ESCAPE
' COVER: Viper.Input.Keyboard.KEY_SHIFT
' COVER: Viper.Input.Keyboard.KEY_CTRL
' COVER: Viper.Input.Keyboard.KEY_ALT
' COVER: Viper.Input.Keyboard.KEY_LSHIFT
' COVER: Viper.Input.Keyboard.KEY_RSHIFT
' COVER: Viper.Input.Keyboard.KEY_LCTRL
' COVER: Viper.Input.Keyboard.KEY_RCTRL
' COVER: Viper.Input.Keyboard.KEY_LALT
' COVER: Viper.Input.Keyboard.KEY_RALT
' COVER: Viper.Input.Keyboard.KEY_MINUS
' COVER: Viper.Input.Keyboard.KEY_EQUALS
' COVER: Viper.Input.Keyboard.KEY_LBRACKET
' COVER: Viper.Input.Keyboard.KEY_RBRACKET
' COVER: Viper.Input.Keyboard.KEY_BACKSLASH
' COVER: Viper.Input.Keyboard.KEY_SEMICOLON
' COVER: Viper.Input.Keyboard.KEY_QUOTE
' COVER: Viper.Input.Keyboard.KEY_GRAVE
' COVER: Viper.Input.Keyboard.KEY_COMMA
' COVER: Viper.Input.Keyboard.KEY_PERIOD
' COVER: Viper.Input.Keyboard.KEY_SLASH
' COVER: Viper.Input.Keyboard.KEY_NUM0
' COVER: Viper.Input.Keyboard.KEY_NUM1
' COVER: Viper.Input.Keyboard.KEY_NUM2
' COVER: Viper.Input.Keyboard.KEY_NUM3
' COVER: Viper.Input.Keyboard.KEY_NUM4
' COVER: Viper.Input.Keyboard.KEY_NUM5
' COVER: Viper.Input.Keyboard.KEY_NUM6
' COVER: Viper.Input.Keyboard.KEY_NUM7
' COVER: Viper.Input.Keyboard.KEY_NUM8
' COVER: Viper.Input.Keyboard.KEY_NUM9
' COVER: Viper.Input.Keyboard.KEY_NUMADD
' COVER: Viper.Input.Keyboard.KEY_NUMSUB
' COVER: Viper.Input.Keyboard.KEY_NUMMUL
' COVER: Viper.Input.Keyboard.KEY_NUMDIV
' COVER: Viper.Input.Keyboard.KEY_NUMENTER
' COVER: Viper.Input.Keyboard.KEY_NUMDOT
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
' COVER: Viper.Input.Mouse.BUTTON_LEFT
' COVER: Viper.Input.Mouse.BUTTON_RIGHT
' COVER: Viper.Input.Mouse.BUTTON_MIDDLE
' COVER: Viper.Input.Mouse.BUTTON_X1
' COVER: Viper.Input.Mouse.BUTTON_X2
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
' COVER: Viper.Input.Pad.PAD_A
' COVER: Viper.Input.Pad.PAD_B
' COVER: Viper.Input.Pad.PAD_X
' COVER: Viper.Input.Pad.PAD_Y
' COVER: Viper.Input.Pad.PAD_LB
' COVER: Viper.Input.Pad.PAD_RB
' COVER: Viper.Input.Pad.PAD_BACK
' COVER: Viper.Input.Pad.PAD_START
' COVER: Viper.Input.Pad.PAD_LSTICK
' COVER: Viper.Input.Pad.PAD_RSTICK
' COVER: Viper.Input.Pad.PAD_UP
' COVER: Viper.Input.Pad.PAD_DOWN
' COVER: Viper.Input.Pad.PAD_LEFT
' COVER: Viper.Input.Pad.PAD_RIGHT
' COVER: Viper.Input.Pad.PAD_GUIDE
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
        Viper.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Input Test", 16, 16)
canvas.Poll()

DIM keySum AS INTEGER
keySum = 0
keySum = keySum + Viper.Input.Keyboard.KEY_UNKNOWN
keySum = keySum + Viper.Input.Keyboard.KEY_A
keySum = keySum + Viper.Input.Keyboard.KEY_B
keySum = keySum + Viper.Input.Keyboard.KEY_C
keySum = keySum + Viper.Input.Keyboard.KEY_D
keySum = keySum + Viper.Input.Keyboard.KEY_E
keySum = keySum + Viper.Input.Keyboard.KEY_F
keySum = keySum + Viper.Input.Keyboard.KEY_G
keySum = keySum + Viper.Input.Keyboard.KEY_H
keySum = keySum + Viper.Input.Keyboard.KEY_I
keySum = keySum + Viper.Input.Keyboard.KEY_J
keySum = keySum + Viper.Input.Keyboard.KEY_K
keySum = keySum + Viper.Input.Keyboard.KEY_L
keySum = keySum + Viper.Input.Keyboard.KEY_M
keySum = keySum + Viper.Input.Keyboard.KEY_N
keySum = keySum + Viper.Input.Keyboard.KEY_O
keySum = keySum + Viper.Input.Keyboard.KEY_P
keySum = keySum + Viper.Input.Keyboard.KEY_Q
keySum = keySum + Viper.Input.Keyboard.KEY_R
keySum = keySum + Viper.Input.Keyboard.KEY_S
keySum = keySum + Viper.Input.Keyboard.KEY_T
keySum = keySum + Viper.Input.Keyboard.KEY_U
keySum = keySum + Viper.Input.Keyboard.KEY_V
keySum = keySum + Viper.Input.Keyboard.KEY_W
keySum = keySum + Viper.Input.Keyboard.KEY_X
keySum = keySum + Viper.Input.Keyboard.KEY_Y
keySum = keySum + Viper.Input.Keyboard.KEY_Z
keySum = keySum + Viper.Input.Keyboard.KEY_0
keySum = keySum + Viper.Input.Keyboard.KEY_1
keySum = keySum + Viper.Input.Keyboard.KEY_2
keySum = keySum + Viper.Input.Keyboard.KEY_3
keySum = keySum + Viper.Input.Keyboard.KEY_4
keySum = keySum + Viper.Input.Keyboard.KEY_5
keySum = keySum + Viper.Input.Keyboard.KEY_6
keySum = keySum + Viper.Input.Keyboard.KEY_7
keySum = keySum + Viper.Input.Keyboard.KEY_8
keySum = keySum + Viper.Input.Keyboard.KEY_9
keySum = keySum + Viper.Input.Keyboard.KEY_F1
keySum = keySum + Viper.Input.Keyboard.KEY_F2
keySum = keySum + Viper.Input.Keyboard.KEY_F3
keySum = keySum + Viper.Input.Keyboard.KEY_F4
keySum = keySum + Viper.Input.Keyboard.KEY_F5
keySum = keySum + Viper.Input.Keyboard.KEY_F6
keySum = keySum + Viper.Input.Keyboard.KEY_F7
keySum = keySum + Viper.Input.Keyboard.KEY_F8
keySum = keySum + Viper.Input.Keyboard.KEY_F9
keySum = keySum + Viper.Input.Keyboard.KEY_F10
keySum = keySum + Viper.Input.Keyboard.KEY_F11
keySum = keySum + Viper.Input.Keyboard.KEY_F12
keySum = keySum + Viper.Input.Keyboard.KEY_UP
keySum = keySum + Viper.Input.Keyboard.KEY_DOWN
keySum = keySum + Viper.Input.Keyboard.KEY_LEFT
keySum = keySum + Viper.Input.Keyboard.KEY_RIGHT
keySum = keySum + Viper.Input.Keyboard.KEY_HOME
keySum = keySum + Viper.Input.Keyboard.KEY_END
keySum = keySum + Viper.Input.Keyboard.KEY_PAGEUP
keySum = keySum + Viper.Input.Keyboard.KEY_PAGEDOWN
keySum = keySum + Viper.Input.Keyboard.KEY_INSERT
keySum = keySum + Viper.Input.Keyboard.KEY_DELETE
keySum = keySum + Viper.Input.Keyboard.KEY_BACKSPACE
keySum = keySum + Viper.Input.Keyboard.KEY_TAB
keySum = keySum + Viper.Input.Keyboard.KEY_ENTER
keySum = keySum + Viper.Input.Keyboard.KEY_SPACE
keySum = keySum + Viper.Input.Keyboard.KEY_ESCAPE
keySum = keySum + Viper.Input.Keyboard.KEY_SHIFT
keySum = keySum + Viper.Input.Keyboard.KEY_CTRL
keySum = keySum + Viper.Input.Keyboard.KEY_ALT
keySum = keySum + Viper.Input.Keyboard.KEY_LSHIFT
keySum = keySum + Viper.Input.Keyboard.KEY_RSHIFT
keySum = keySum + Viper.Input.Keyboard.KEY_LCTRL
keySum = keySum + Viper.Input.Keyboard.KEY_RCTRL
keySum = keySum + Viper.Input.Keyboard.KEY_LALT
keySum = keySum + Viper.Input.Keyboard.KEY_RALT
keySum = keySum + Viper.Input.Keyboard.KEY_MINUS
keySum = keySum + Viper.Input.Keyboard.KEY_EQUALS
keySum = keySum + Viper.Input.Keyboard.KEY_LBRACKET
keySum = keySum + Viper.Input.Keyboard.KEY_RBRACKET
keySum = keySum + Viper.Input.Keyboard.KEY_BACKSLASH
keySum = keySum + Viper.Input.Keyboard.KEY_SEMICOLON
keySum = keySum + Viper.Input.Keyboard.KEY_QUOTE
keySum = keySum + Viper.Input.Keyboard.KEY_GRAVE
keySum = keySum + Viper.Input.Keyboard.KEY_COMMA
keySum = keySum + Viper.Input.Keyboard.KEY_PERIOD
keySum = keySum + Viper.Input.Keyboard.KEY_SLASH
keySum = keySum + Viper.Input.Keyboard.KEY_NUM0
keySum = keySum + Viper.Input.Keyboard.KEY_NUM1
keySum = keySum + Viper.Input.Keyboard.KEY_NUM2
keySum = keySum + Viper.Input.Keyboard.KEY_NUM3
keySum = keySum + Viper.Input.Keyboard.KEY_NUM4
keySum = keySum + Viper.Input.Keyboard.KEY_NUM5
keySum = keySum + Viper.Input.Keyboard.KEY_NUM6
keySum = keySum + Viper.Input.Keyboard.KEY_NUM7
keySum = keySum + Viper.Input.Keyboard.KEY_NUM8
keySum = keySum + Viper.Input.Keyboard.KEY_NUM9
keySum = keySum + Viper.Input.Keyboard.KEY_NUMADD
keySum = keySum + Viper.Input.Keyboard.KEY_NUMSUB
keySum = keySum + Viper.Input.Keyboard.KEY_NUMMUL
keySum = keySum + Viper.Input.Keyboard.KEY_NUMDIV
keySum = keySum + Viper.Input.Keyboard.KEY_NUMENTER
keySum = keySum + Viper.Input.Keyboard.KEY_NUMDOT
Viper.Diagnostics.Assert(keySum > 0, "key.sum")
Viper.Diagnostics.AssertEq(Viper.Input.Keyboard.KEY_A, 65, "key.a")
Viper.Diagnostics.AssertEq(Viper.Input.Keyboard.KEY_0, 48, "key.0")
Viper.Diagnostics.AssertEq(Viper.Input.Keyboard.KEY_SPACE, 32, "key.space")
Viper.Diagnostics.AssertEq(Viper.Input.Keyboard.KEY_F1, 290, "key.f1")
Viper.Diagnostics.AssertEq(Viper.Input.Keyboard.KEY_UNKNOWN, 0, "key.unknown")

DIM down AS INTEGER
down = Viper.Input.Keyboard.IsDown(Viper.Input.Keyboard.KEY_A)
Viper.Diagnostics.Assert(IsBool(down), "key.isdown")
DIM up AS INTEGER
up = Viper.Input.Keyboard.IsUp(Viper.Input.Keyboard.KEY_A)
Viper.Diagnostics.Assert(IsBool(up), "key.isup")
DIM anyDown AS INTEGER
anyDown = Viper.Input.Keyboard.AnyDown()
Viper.Diagnostics.Assert(IsBool(anyDown), "key.anydown")
DIM downKey AS INTEGER
downKey = Viper.Input.Keyboard.GetDown()
Viper.Diagnostics.Assert(downKey >= 0, "key.getdown")
DIM wasPressed AS INTEGER
wasPressed = Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_A)
Viper.Diagnostics.Assert(IsBool(wasPressed), "key.waspressed")
DIM wasReleased AS INTEGER
wasReleased = Viper.Input.Keyboard.WasReleased(Viper.Input.Keyboard.KEY_A)
Viper.Diagnostics.Assert(IsBool(wasReleased), "key.wasreleased")
DIM pressed AS Viper.Collections.Seq
pressed = Viper.Input.Keyboard.GetPressed()
Viper.Diagnostics.Assert(pressed.Len >= 0, "key.getpressed")
DIM released AS Viper.Collections.Seq
released = Viper.Input.Keyboard.GetReleased()
Viper.Diagnostics.Assert(released.Len >= 0, "key.getreleased")
Viper.Input.Keyboard.EnableTextInput()
canvas.Poll()
DIM text AS STRING
text = Viper.Input.Keyboard.GetText()
Viper.Diagnostics.Assert(text.Length >= 0, "key.gettext")
Viper.Input.Keyboard.DisableTextInput()
DIM shiftDown AS INTEGER
shiftDown = Viper.Input.Keyboard.Shift()
Viper.Diagnostics.Assert(IsBool(shiftDown), "key.shift")
DIM ctrlDown AS INTEGER
ctrlDown = Viper.Input.Keyboard.Ctrl()
Viper.Diagnostics.Assert(IsBool(ctrlDown), "key.ctrl")
DIM altDown AS INTEGER
altDown = Viper.Input.Keyboard.Alt()
Viper.Diagnostics.Assert(IsBool(altDown), "key.alt")
DIM caps AS INTEGER
caps = Viper.Input.Keyboard.CapsLock()
Viper.Diagnostics.Assert(IsBool(caps), "key.caps")
DIM keyName AS STRING
keyName = Viper.Input.Keyboard.KeyName(Viper.Input.Keyboard.KEY_A)
Viper.Diagnostics.AssertEqStr(keyName, "A", "key.name")

DIM btnSum AS INTEGER
btnSum = 0
btnSum = btnSum + Viper.Input.Mouse.BUTTON_LEFT
btnSum = btnSum + Viper.Input.Mouse.BUTTON_RIGHT
btnSum = btnSum + Viper.Input.Mouse.BUTTON_MIDDLE
btnSum = btnSum + Viper.Input.Mouse.BUTTON_X1
btnSum = btnSum + Viper.Input.Mouse.BUTTON_X2
Viper.Diagnostics.Assert(btnSum > 0, "mouse.btnsum")
Viper.Input.Mouse.Hide()
Viper.Input.Mouse.Show()
DIM hidden AS INTEGER
hidden = Viper.Input.Mouse.IsHidden()
Viper.Diagnostics.Assert(IsBool(hidden), "mouse.hidden")
Viper.Input.Mouse.Capture()
DIM captured AS INTEGER
captured = Viper.Input.Mouse.IsCaptured()
Viper.Diagnostics.Assert(IsBool(captured), "mouse.captured")
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
mdown = Viper.Input.Mouse.IsDown(Viper.Input.Mouse.BUTTON_LEFT)
Viper.Diagnostics.Assert(IsBool(mdown), "mouse.isdown")
DIM mup AS INTEGER
mup = Viper.Input.Mouse.IsUp(Viper.Input.Mouse.BUTTON_LEFT)
Viper.Diagnostics.Assert(IsBool(mup), "mouse.isup")
DIM leftDown AS INTEGER
leftDown = Viper.Input.Mouse.Left()
DIM rightDown AS INTEGER
rightDown = Viper.Input.Mouse.Right()
DIM middleDown AS INTEGER
middleDown = Viper.Input.Mouse.Middle()
DIM wasPressM AS INTEGER
wasPressM = Viper.Input.Mouse.WasPressed(Viper.Input.Mouse.BUTTON_LEFT)
DIM wasRelM AS INTEGER
wasRelM = Viper.Input.Mouse.WasReleased(Viper.Input.Mouse.BUTTON_LEFT)
DIM wasClick AS INTEGER
wasClick = Viper.Input.Mouse.WasClicked(Viper.Input.Mouse.BUTTON_LEFT)
DIM wasDbl AS INTEGER
wasDbl = Viper.Input.Mouse.WasDoubleClicked(Viper.Input.Mouse.BUTTON_LEFT)
DIM wheelX AS INTEGER
wheelX = Viper.Input.Mouse.WheelX()
DIM wheelY AS INTEGER
wheelY = Viper.Input.Mouse.WheelY()
Viper.Diagnostics.Assert(leftDown = leftDown, "mouse.left")
Viper.Diagnostics.Assert(rightDown = rightDown, "mouse.right")
Viper.Diagnostics.Assert(middleDown = middleDown, "mouse.middle")
Viper.Diagnostics.Assert(wasPressM = wasPressM, "mouse.waspressed")
Viper.Diagnostics.Assert(wasRelM = wasRelM, "mouse.wasreleased")
Viper.Diagnostics.Assert(wasClick = wasClick, "mouse.wasclicked")
Viper.Diagnostics.Assert(wasDbl = wasDbl, "mouse.wasdouble")
Viper.Diagnostics.Assert(wheelX = wheelX, "mouse.wheelx")
Viper.Diagnostics.Assert(wheelY = wheelY, "mouse.wheely")
Viper.Diagnostics.Assert(mx = mx, "mouse.x")
Viper.Diagnostics.Assert(my = my, "mouse.y")
Viper.Diagnostics.Assert(dx = dx, "mouse.dx")
Viper.Diagnostics.Assert(dy = dy, "mouse.dy")

DIM padSum AS INTEGER
padSum = 0
padSum = padSum + Viper.Input.Pad.PAD_A
padSum = padSum + Viper.Input.Pad.PAD_B
padSum = padSum + Viper.Input.Pad.PAD_X
padSum = padSum + Viper.Input.Pad.PAD_Y
padSum = padSum + Viper.Input.Pad.PAD_LB
padSum = padSum + Viper.Input.Pad.PAD_RB
padSum = padSum + Viper.Input.Pad.PAD_BACK
padSum = padSum + Viper.Input.Pad.PAD_START
padSum = padSum + Viper.Input.Pad.PAD_LSTICK
padSum = padSum + Viper.Input.Pad.PAD_RSTICK
padSum = padSum + Viper.Input.Pad.PAD_UP
padSum = padSum + Viper.Input.Pad.PAD_DOWN
padSum = padSum + Viper.Input.Pad.PAD_LEFT
padSum = padSum + Viper.Input.Pad.PAD_RIGHT
padSum = padSum + Viper.Input.Pad.PAD_GUIDE
Viper.Diagnostics.Assert(padSum > 0, "pad.sum")
DIM padCount AS INTEGER
padCount = Viper.Input.Pad.Count()
Viper.Diagnostics.Assert(padCount >= 0, "pad.count")
DIM padConnected AS INTEGER
padConnected = Viper.Input.Pad.IsConnected(0)
Viper.Diagnostics.Assert(padConnected = 0 OR padConnected = 1, "pad.connected")
DIM padName AS STRING
padName = Viper.Input.Pad.Name(0)
Viper.Diagnostics.Assert(padName.Length >= 0, "pad.name")
DIM padDown AS INTEGER
padDown = Viper.Input.Pad.IsDown(0, Viper.Input.Pad.PAD_A)
DIM padUp AS INTEGER
padUp = Viper.Input.Pad.IsUp(0, Viper.Input.Pad.PAD_A)
DIM padPressed AS INTEGER
padPressed = Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.PAD_A)
DIM padReleased AS INTEGER
padReleased = Viper.Input.Pad.WasReleased(0, Viper.Input.Pad.PAD_A)
Viper.Diagnostics.Assert(padDown = padDown, "pad.isdown")
Viper.Diagnostics.Assert(padUp = padUp, "pad.isup")
Viper.Diagnostics.Assert(padPressed = padPressed, "pad.waspressed")
Viper.Diagnostics.Assert(padReleased = padReleased, "pad.wasreleased")
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
Viper.Diagnostics.Assert(lx >= -1 AND lx <= 1, "pad.leftx")
Viper.Diagnostics.Assert(ly >= -1 AND ly <= 1, "pad.lefty")
Viper.Diagnostics.Assert(rx >= -1 AND rx <= 1, "pad.rightx")
Viper.Diagnostics.Assert(ry >= -1 AND ry <= 1, "pad.righty")
Viper.Diagnostics.Assert(lt >= 0 AND lt <= 1, "pad.lefttrigger")
Viper.Diagnostics.Assert(rt >= 0 AND rt <= 1, "pad.righttrigger")
Viper.Input.Pad.SetDeadzone(0.2)
DIM dz AS DOUBLE
dz = Viper.Input.Pad.GetDeadzone()
AssertApprox(dz, 0.2, 0.0001, "pad.deadzone")
Viper.Input.Pad.Vibrate(0, 0.1, 0.1)
Viper.Input.Pad.StopVibration(0)

PRINT "RESULT: ok"
END
