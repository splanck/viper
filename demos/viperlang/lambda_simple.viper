module LambdaSimple;

// Simple lambda test without captures
func main(): Integer {
    var double = (x: Integer) => x * 2
    return double(5)
}
