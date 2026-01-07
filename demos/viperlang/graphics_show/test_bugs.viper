module test_bugs;

// Test BUG-005: Mixed type arithmetic
func testBug005() {
    var i = 5;
    var count = 10;
    // This should work: 1.0 - (5 * 0.4) / 10 = 1.0 - 2.0/10 = 1.0 - 0.2 = 0.8
    var fade = 1.0 - (i * 0.4) / count;
}

// Test BUG-004: Type mismatch in return contexts
func testBug004(x: Number) -> Integer {
    // Floor returns Number, but we want to return Integer
    return Viper.Math.Floor(x);
}

// Test BUG-006: Floor return type
func testBug006() {
    var n = 3.7;
    var floored = Viper.Math.Floor(n);  // floored should be Number (3.0)
    // This should fail if we try to use it as Integer:
    // var intVal: Integer = floored;  // Should error or need cast
}

func main() {
    testBug005();
    var result = testBug004(5.7);
    testBug006();
}
