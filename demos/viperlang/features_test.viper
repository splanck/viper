module FeaturesTest;

// Test entity with fields
entity Counter {
    Integer count;
    String label;

    func init(n: String, v: Integer) {
        label = n;
        count = v;
    }

    func increment() {
        count = count + 1;
    }

    func getCount() -> Integer {
        return count;
    }
}

// Test for-in with range
func sumRange() -> Integer {
    Integer total = 0;
    for (i in 1..5) {
        total = total + i;
    }
    return total;
}

// Test list operations
func testList() -> Integer {
    List[Integer] nums = [10, 20, 30];
    return nums.size();
}

// Test match expression
func matchTest(x: Integer) -> Integer {
    match x {
        1 => return 10;
        2 => return 20;
        _ => return 0;
    }
    return 99;
}

// Main entry point
func main() {
    // Test entity
    Counter c = new Counter();
    c.init("test", 0);
    c.increment();
    c.increment();
    Integer val = c.getCount();

    // Test range
    Integer sum = sumRange();

    // Test list
    Integer listCount = testList();

    // Test match
    Integer result = matchTest(2);

    // All features tested!
}
