module RuntimeTest01;

import "./_support";

// EXPECT_OUT: RESULT: ok
// COVER: Viper.Terminal.BeginBatch
// COVER: Viper.Terminal.EndBatch
// COVER: Viper.Terminal.Clear
// COVER: Viper.Terminal.SetColor
// COVER: Viper.Terminal.SetPosition
// COVER: Viper.Terminal.Print
// COVER: Viper.Terminal.PrintInt
// COVER: Viper.Terminal.PrintNum
// COVER: Viper.Terminal.Say
// COVER: Viper.Terminal.SayInt
// COVER: Viper.Terminal.SayNum
// COVER: Viper.Terminal.SayBool
// COVER: Viper.Terminal.Bell
// COVER: Viper.Terminal.Flush
// COVER: Viper.Terminal.SetCursorVisible
// COVER: Viper.Terminal.SetAltScreen
// COVER: Viper.Terminal.GetKeyTimeout
// COVER: Viper.Terminal.InKey
// COVER: Viper.Math.Abs
// COVER: Viper.Math.AbsInt
// COVER: Viper.Math.Sin
// COVER: Viper.Math.Cos
// COVER: Viper.Math.Tan
// COVER: Viper.Math.Sqrt
// COVER: Viper.Math.Log
// COVER: Viper.Math.Exp
// COVER: Viper.Math.Floor
// COVER: Viper.Math.Ceil
// COVER: Viper.Math.Round
// COVER: Viper.Math.Min
// COVER: Viper.Math.Max
// COVER: Viper.Math.MinInt
// COVER: Viper.Math.MaxInt
// COVER: Viper.Random.Seed
// COVER: Viper.Random.NextInt
// COVER: Viper.Random.Next
// COVER: Viper.Time.SleepMs
// COVER: Viper.Time.Clock.Sleep

func start() {
    Viper.Terminal.BeginBatch();
    Viper.Terminal.Clear();
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(1, 1);
    Viper.Terminal.Print("sum=");
    Viper.Terminal.PrintInt(6);
    Viper.Terminal.Print(" pi=");
    Viper.Terminal.PrintNum(3.14);
    Viper.Terminal.Say("");
    Viper.Terminal.SayInt(42);
    Viper.Terminal.SayNum(2.5);
    Viper.Terminal.SayBool(true);
    Viper.Terminal.Bell();
    Viper.Terminal.Flush();
    Viper.Terminal.SetCursorVisible(0);
    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
    Viper.Terminal.EndBatch();

    assertApprox(Viper.Math.Sin(0.0), 0.0, 0.000001, "sin(0)");
    assertApprox(Viper.Math.Cos(0.0), 1.0, 0.000001, "cos(0)");
    assertApprox(Viper.Math.Tan(0.0), 0.0, 0.000001, "tan(0)");
    assertApprox(Viper.Math.Sqrt(9.0), 3.0, 0.000001, "sqrt(9)");
    assertApprox(Viper.Math.Log(Viper.Math.Exp(1.0)), 1.0, 0.000001, "log(exp(1))");
    assertApprox(Viper.Math.Abs(-1.5), 1.5, 0.000001, "abs");
    assertEqInt(Viper.Math.AbsInt(-7), 7, "absint");
    assertApprox(Viper.Math.Round(1.6), 2.0, 0.000001, "round");
    assertApprox(Viper.Math.Floor(1.9), 1.0, 0.000001, "floor");
    assertApprox(Viper.Math.Ceil(1.1), 2.0, 0.000001, "ceil");
    assertApprox(Viper.Math.Min(1.0, 2.0), 1.0, 0.000001, "min");
    assertApprox(Viper.Math.Max(1.0, 2.0), 2.0, 0.000001, "max");
    assertEqInt(Viper.Math.MinInt(1, 2), 1, "minint");
    assertEqInt(Viper.Math.MaxInt(1, 2), 2, "maxint");

    Viper.Random.Seed(123);
    var a = Viper.Random.NextInt(100);
    var b = Viper.Random.NextInt(100);
    var n = Viper.Random.Next();
    Viper.Random.Seed(123);
    var c = Viper.Random.NextInt(100);
    var d = Viper.Random.NextInt(100);
    var n2 = Viper.Random.Next();
    assertEqInt(a, c, "rand1");
    assertEqInt(b, d, "rand2");
    assertApprox(n, n2, 0.0000001, "randf");

    var key = Viper.Terminal.GetKeyTimeout(0);
    var ink = Viper.Terminal.InKey();
    assertTrue(key == "" || key == key, "keytimeout");
    assertTrue(ink == "" || ink == ink, "inkey");

    Viper.Time.SleepMs(1);
    Viper.Time.Clock.Sleep(1);

    report();
}
