module RuntimeTest04;

import "./_support";

// EXPECT_OUT: RESULT: ok
// EXPECT_ARGS: alpha beta
// COVER: Viper.Environment.GetArgumentCount
// COVER: Viper.Environment.GetArgument
// COVER: Viper.Environment.GetCommandLine

func start() {
    var count = Viper.Environment.GetArgumentCount();
    assertTrue(count >= 1, "argc");
    if (count >= 3) {
        assertEqStr(Viper.Environment.GetArgument(1), "alpha", "arg1");
        assertEqStr(Viper.Environment.GetArgument(2), "beta", "arg2");
    }
    var cmd = Viper.Environment.GetCommandLine();
    assertNotEmpty(cmd, "cmdline");
    report();
}
