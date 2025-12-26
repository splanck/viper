module RuntimeTest06;

import "./_support";

// EXPECT_OUT: RESULT: ok

func start() {
    var total = 0;
    for (var i = 0; i < 5; i = i + 1) {
        total = total + i;
    }
    assertEqInt(total, 10, "for_sum");

    var sum = 0;
    var j = 0;
    while (j < 3) {
        sum = sum + j;
        j = j + 1;
    }
    assertEqInt(sum, 3, "while_sum");

    report();
}
