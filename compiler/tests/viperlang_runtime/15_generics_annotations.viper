module RuntimeTest15;

import "./_support";

// EXPECT_OUT: RESULT: ok

func start() {
    var nums: List[Integer] = [1, 2, 3];
    nums.add(4);
    assertEqInt(nums.count(), 4, "list.count");

    var m: Map[String, Integer] = {"x": 1};
    m.set("y", 2);
    assertEqInt(m.get("y"), 2, "map.get");

    report();
}
