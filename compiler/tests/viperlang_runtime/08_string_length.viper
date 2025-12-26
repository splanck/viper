module RuntimeTest08;

import "./_support";

// EXPECT_OUT: RESULT: ok
// COVER: Viper.String.Length

func start() {
    var len = Viper.String.Length("abc");
    assertEqInt(len, 3, "length");
    report();
}
