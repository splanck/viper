module RuntimeTest18;

import "./_support";

// EXPECT_OUT: RESULT: ok

func gcd(a: Integer, b: Integer) -> Integer {
    var x = a;
    var y = b;
    while (y != 0) {
        var t = x % y;
        x = y;
        y = t;
    }
    return x;
}

func start() {
    assertEqInt(gcd(54, 24), 6, "gcd");
    report();
}
