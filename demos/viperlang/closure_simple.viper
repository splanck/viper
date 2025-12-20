module ClosureSimple;

// Simple closure test
func main(): Integer {
    var x = 10
    var addX = (y: Integer) => x + y
    return addX(5)
}
