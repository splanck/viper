' EXPECT_OUT: RESULT: ok
' EXPECT_TTY_INPUT: first\nsecond\nk\n
' COVER: Zanna.Terminal.AskResult
' COVER: Zanna.Terminal.BeginBatch
' COVER: Zanna.Terminal.Bell
' COVER: Zanna.Terminal.Clear
' COVER: Zanna.Terminal.EndBatch
' COVER: Zanna.Terminal.Flush
' COVER: Zanna.Terminal.ReadKey
' COVER: Zanna.Terminal.ReadKeyFor
' COVER: Zanna.Terminal.PollKey
' COVER: Zanna.Terminal.Print
' COVER: Zanna.Terminal.PrintInt
' COVER: Zanna.Terminal.PrintNum
' COVER: Zanna.Terminal.ReadLine
' COVER: Zanna.Terminal.Say
' COVER: Zanna.Terminal.SayBool
' COVER: Zanna.Terminal.SayInt
' COVER: Zanna.Terminal.SayNum
' COVER: Zanna.Terminal.SetAltScreen
' COVER: Zanna.Terminal.SetColor
' COVER: Zanna.Terminal.SetCursorVisible
' COVER: Zanna.Terminal.SetPosition

Zanna.Terminal.BeginBatch()
Zanna.Terminal.Clear()
Zanna.Terminal.SetColor(7, 0)
Zanna.Terminal.SetPosition(1, 1)
Zanna.Terminal.Print("hello ")
Zanna.Terminal.PrintInt(123)
Zanna.Terminal.Print(" ")
Zanna.Terminal.PrintNum(4.5)
Zanna.Terminal.Say("")
Zanna.Terminal.SayInt(42)
Zanna.Terminal.SayNum(3.25)
Zanna.Terminal.SayBool(TRUE)
Zanna.Terminal.Bell()
Zanna.Terminal.Flush()
Zanna.Terminal.SetCursorVisible(FALSE)
Zanna.Terminal.SetCursorVisible(TRUE)
Zanna.Terminal.SetAltScreen(FALSE)
Zanna.Terminal.EndBatch()

DIM line1 AS STRING
line1 = Zanna.Terminal.ReadLine()
Zanna.Core.Diagnostics.AssertEqStr(line1, "first", "term.readline")

DIM ans AS STRING
ans = Zanna.Result.UnwrapStr(Zanna.Terminal.AskResult("prompt>"))
Zanna.Core.Diagnostics.AssertEqStr(ans, "second", "term.ask")

DIM key AS STRING
key = Zanna.Terminal.ReadKey()
Zanna.Core.Diagnostics.Assert(key <> "", "term.getkey")

DIM key2 AS STRING
key2 = Zanna.Terminal.ReadKeyFor(0)
DIM key3 AS STRING
key3 = Zanna.Terminal.PollKey()
Zanna.Core.Diagnostics.Assert(key2 = key2, "term.getkeytimeout")
Zanna.Core.Diagnostics.Assert(key3 = key3, "term.inkey")

PRINT "RESULT: ok"
END
