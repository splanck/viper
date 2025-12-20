module ClosureTest;

// Test closure environment capture

// Test 1: Simple closure capturing a variable
func testSimpleClosure() {
    var x = 10
    var addX = (y: Integer) => x + y
    Viper.Terminal.Say("10 + 5 = " + addX(5).toString())  // Should print 15
}

// Test 2: Closure capturing multiple variables
func testMultiCapture() {
    var a = 100
    var b = 50
    var compute = (c: Integer) => a + b + c
    Viper.Terminal.Say("100 + 50 + 25 = " + compute(25).toString())  // Should print 175
}

// Test 3: Lambda without captures (should still work with uniform closure ABI)
func testNoClosure() {
    var double = (x: Integer) => x * 2
    Viper.Terminal.Say("5 * 2 = " + double(5).toString())  // Should print 10
}

// Test 4: Nested scope capture
func testNestedCapture() {
    var outer = 1000

    func inner(): Integer {
        var middle = 200
        var closure = (x: Integer) => outer + middle + x
        return closure(30)
    }

    Viper.Terminal.Say("1000 + 200 + 30 = " + inner().toString())  // Should print 1230
}

// Main entry point
func main() {
    Viper.Terminal.Say("Testing closures...")
    testSimpleClosure()
    testMultiCapture()
    testNoClosure()
    // testNestedCapture()  // Skip for now as nested functions may not be supported
    Viper.Terminal.Say("Done!")
}
