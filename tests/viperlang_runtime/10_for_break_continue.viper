module RuntimeTest10;

import "./_support";

// EXPECT_OUT: RESULT: ok

func start() {
    var sum = 0;
    for (var i = 0; i < 10; i = i + 1) {
        if (i == 5) {
            continue;
        }
        if (i == 8) {
            break;
        }
        sum = sum + i;
    }
    assertEqInt(sum, 23, "break_continue");
    report();
}
