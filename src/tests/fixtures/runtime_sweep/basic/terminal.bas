' EXPECT_OUT: RESULT: ok
' EXPECT_TTY_INPUT: first\nsecond\nk\n
' COVER: Viper.Terminal.AskResult
' COVER: Viper.Terminal.BeginBatch
' COVER: Viper.Terminal.Bell
' COVER: Viper.Terminal.Clear
' COVER: Viper.Terminal.EndBatch
' COVER: Viper.Terminal.Flush
' COVER: Viper.Terminal.ReadKey
' COVER: Viper.Terminal.ReadKeyFor
' COVER: Viper.Terminal.PollKey
' COVER: Viper.Terminal.Print
' COVER: Viper.Terminal.PrintInt
' COVER: Viper.Terminal.PrintNum
' COVER: Viper.Terminal.ReadLine
' COVER: Viper.Terminal.Say
' COVER: Viper.Terminal.SayBool
' COVER: Viper.Terminal.SayInt
' COVER: Viper.Terminal.SayNum
' COVER: Viper.Terminal.SetAltScreen
' COVER: Viper.Terminal.SetColor
' COVER: Viper.Terminal.SetCursorVisible
' COVER: Viper.Terminal.SetPosition

Viper.Terminal.BeginBatch()
Viper.Terminal.Clear()
Viper.Terminal.SetColor(7, 0)
Viper.Terminal.SetPosition(1, 1)
Viper.Terminal.Print("hello ")
Viper.Terminal.PrintInt(123)
Viper.Terminal.Print(" ")
Viper.Terminal.PrintNum(4.5)
Viper.Terminal.Say("")
Viper.Terminal.SayInt(42)
Viper.Terminal.SayNum(3.25)
Viper.Terminal.SayBool(TRUE)
Viper.Terminal.Bell()
Viper.Terminal.Flush()
Viper.Terminal.SetCursorVisible(FALSE)
Viper.Terminal.SetCursorVisible(TRUE)
Viper.Terminal.SetAltScreen(FALSE)
Viper.Terminal.EndBatch()

DIM line1 AS STRING
line1 = Viper.Terminal.ReadLine()
Viper.Core.Diagnostics.AssertEqStr(line1, "first", "term.readline")

DIM ans AS STRING
ans = Viper.Result.UnwrapStr(Viper.Terminal.AskResult("prompt>"))
Viper.Core.Diagnostics.AssertEqStr(ans, "second", "term.ask")

DIM key AS STRING
key = Viper.Terminal.ReadKey()
Viper.Core.Diagnostics.Assert(key <> "", "term.getkey")

DIM key2 AS STRING
key2 = Viper.Terminal.ReadKeyFor(0)
DIM key3 AS STRING
key3 = Viper.Terminal.PollKey()
Viper.Core.Diagnostics.Assert(key2 = key2, "term.getkeytimeout")
Viper.Core.Diagnostics.Assert(key3 = key3, "term.inkey")

PRINT "RESULT: ok"
END
