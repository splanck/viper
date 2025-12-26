# Appendix E: Error Messages

A guide to common Viper error messages and how to fix them.

---

## Compilation Errors

### Syntax Errors

**"Unexpected token"**
```
Error: Unexpected token 'else' at line 5
```
**Cause**: Missing or misplaced syntax element.
**Fix**: Check for missing braces, parentheses, or semicolons before the error location.
```viper
// Wrong
if x > 0
    doSomething()
else  // Error here

// Right
if x > 0 {
    doSomething();
} else {
    doOther();
}
```

**"Expected ';'"**
```
Error: Expected ';' after statement at line 10
```
**Cause**: Missing semicolon.
**Fix**: Add semicolon at end of statement.

**"Expected '}'"**
```
Error: Expected '}' at end of block at line 25
```
**Cause**: Unclosed brace.
**Fix**: Count opening and closing braces. Ensure they match.

**"Invalid character"**
```
Error: Invalid character '@' at line 3
```
**Cause**: Unsupported character in source code.
**Fix**: Remove or replace the character.

---

### Type Errors

**"Type mismatch"**
```
Error: Type mismatch: expected 'i64', got 'string' at line 7
```
**Cause**: Using a value of the wrong type.
**Fix**: Convert the value or fix the expression.
```viper
// Wrong
let x: i64 = "hello";

// Right
let x: i64 = 42;
// or
let x: string = "hello";
```

**"Cannot assign to immutable variable"**
```
Error: Cannot assign to immutable variable 'x' at line 12
```
**Cause**: Trying to modify a `let` variable.
**Fix**: Use `var` if the variable needs to change.
```viper
// Wrong
let x = 5;
x = 10;  // Error

// Right
var x = 5;
x = 10;  // OK
```

**"Incompatible types in binary operation"**
```
Error: Cannot apply '+' to 'string' and 'i64' at line 8
```
**Cause**: Operator doesn't support these types.
**Fix**: Convert one operand to match the other.
```viper
// Wrong
let result = "Value: " + 42;

// Right
let result = "Value: " + 42.toString();
```

---

### Name Errors

**"Undefined variable"**
```
Error: Undefined variable 'count' at line 15
```
**Cause**: Using a variable that hasn't been declared.
**Fix**: Declare the variable or check spelling.
```viper
// Wrong
Viper.Terminal.Say(count);  // count not defined

// Right
let count = 0;
Viper.Terminal.Say(count);
```

**"Undefined function"**
```
Error: Undefined function 'calculate' at line 20
```
**Cause**: Calling a function that doesn't exist.
**Fix**: Define the function or check spelling/imports.

**"Duplicate definition"**
```
Error: Duplicate definition of 'myFunc' at line 30
```
**Cause**: Defining the same name twice.
**Fix**: Rename one of the definitions.

**"Variable used before declaration"**
```
Error: Variable 'x' used before declaration at line 5
```
**Cause**: Accessing a variable before its `let`/`var` statement.
**Fix**: Move the declaration before the usage.

---

### Function Errors

**"Wrong number of arguments"**
```
Error: Function 'add' expects 2 arguments, got 3 at line 10
```
**Cause**: Calling a function with incorrect argument count.
**Fix**: Match the function signature.
```viper
func add(a: i64, b: i64) -> i64 { ... }

// Wrong
add(1, 2, 3);

// Right
add(1, 2);
```

**"Missing return statement"**
```
Error: Function 'getValue' must return a value at line 25
```
**Cause**: Function declares a return type but doesn't return.
**Fix**: Add a return statement.
```viper
// Wrong
func getValue() -> i64 {
    let x = 42;
    // No return!
}

// Right
func getValue() -> i64 {
    let x = 42;
    return x;
}
```

**"Cannot return value from void function"**
```
Error: Cannot return a value from function with no return type at line 8
```
**Cause**: Returning a value from a void function.
**Fix**: Remove the value or add a return type.

---

### Class/Interface Errors

**"Class does not implement interface"**
```
Error: Class 'Circle' does not implement method 'draw' from interface 'Drawable'
```
**Cause**: Missing required method from interface.
**Fix**: Implement all interface methods.

**"Cannot access private member"**
```
Error: Cannot access private member 'count' of class 'Counter'
```
**Cause**: Accessing private field/method from outside class.
**Fix**: Use a public getter or make the member public.

**"Method signature mismatch"**
```
Error: Override of 'speak' has different signature than parent
```
**Cause**: Override method doesn't match parent signature.
**Fix**: Match the parameter types and return type.

---

## Runtime Errors

### Null/Undefined Errors

**"Null pointer exception"**
```
Error: Null pointer exception at line 15
```
**Cause**: Calling a method or accessing a field on null.
**Fix**: Check for null before accessing.
```viper
// Wrong
let user = findUser(id);
Viper.Terminal.Say(user.name);  // user might be null!

// Right
let user = findUser(id);
if user != null {
    Viper.Terminal.Say(user.name);
}
```

### Array Errors

**"Index out of bounds"**
```
Error: Array index 10 out of bounds for length 5 at line 20
```
**Cause**: Accessing an array element that doesn't exist.
**Fix**: Check array length before accessing.
```viper
// Wrong
let items = [1, 2, 3, 4, 5];
let x = items[10];  // Only 0-4 valid!

// Right
if index < items.length {
    let x = items[index];
}
```

### Arithmetic Errors

**"Division by zero"**
```
Error: Division by zero at line 12
```
**Cause**: Dividing by zero.
**Fix**: Check the divisor before dividing.
```viper
// Wrong
let result = a / b;  // b might be 0!

// Right
if b != 0 {
    let result = a / b;
} else {
    // Handle the error
}
```

**"Integer overflow"**
```
Error: Integer overflow at line 8
```
**Cause**: Calculation result too large for the type.
**Fix**: Use a larger type or check for overflow.

### Type Errors

**"Type cast failed"**
```
Error: Cannot cast 'Dog' to 'Cat' at line 30
```
**Cause**: Invalid type cast.
**Fix**: Check the actual type before casting.
```viper
// Wrong
let animal: Animal = Dog();
let cat = animal as Cat;  // Error!

// Right
if animal is Cat {
    let cat = animal as Cat;
}
```

---

## File/IO Errors

**"File not found"**
```
Error: File not found: 'data.txt' at line 5
```
**Cause**: Trying to read a file that doesn't exist.
**Fix**: Check if file exists or handle the error.
```viper
if Viper.File.exists("data.txt") {
    let content = Viper.File.readText("data.txt");
}
```

**"Permission denied"**
```
Error: Permission denied: '/etc/passwd' at line 10
```
**Cause**: No permission to access the file.
**Fix**: Check file permissions or use a different location.

**"File already exists"**
```
Error: File already exists: 'output.txt' at line 15
```
**Cause**: Creating a file that already exists (in exclusive mode).
**Fix**: Delete first or use overwrite mode.

---

## Network Errors

**"Connection refused"**
```
Error: Connection refused to localhost:8080 at line 20
```
**Cause**: Server not running or wrong port.
**Fix**: Check that the server is running on the specified port.

**"Connection timeout"**
```
Error: Connection timeout after 5000ms at line 25
```
**Cause**: Server not responding in time.
**Fix**: Increase timeout or check network connectivity.

**"Host not found"**
```
Error: Host not found: 'invalid.example.com' at line 30
```
**Cause**: DNS lookup failed.
**Fix**: Check the hostname spelling and network connection.

---

## Memory Errors

**"Out of memory"**
```
Error: Out of memory while allocating 1GB
```
**Cause**: Not enough memory available.
**Fix**: Reduce memory usage, process data in chunks, or increase memory limit.

**"Stack overflow"**
```
Error: Stack overflow at line 100
```
**Cause**: Too much recursion or very deep call stack.
**Fix**: Convert recursion to iteration or increase stack size.
```viper
// Wrong: infinite recursion
func bad() {
    bad();  // Never ends!
}

// Wrong: too deep recursion
func factorial(n: i64) -> i64 {
    return n * factorial(n - 1);  // No base case!
}

// Right
func factorial(n: i64) -> i64 {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}
```

---

## Concurrency Errors

**"Deadlock detected"**
```
Error: Deadlock detected - threads waiting on each other
```
**Cause**: Threads waiting for locks held by each other.
**Fix**: Acquire locks in consistent order.

**"Race condition"**
```
Warning: Potential race condition accessing 'counter'
```
**Cause**: Multiple threads accessing shared data unsafely.
**Fix**: Use synchronization (mutex, atomic operations).

---

## Debugging Tips

1. **Read the full message**: Error messages often include line numbers and context.

2. **Check the line before**: Syntax errors are often caused by the previous line.

3. **Use print debugging**: Add `Viper.Terminal.Say()` to trace execution.

4. **Simplify**: Create a minimal example that reproduces the error.

5. **Check types**: Many errors come from type mismatches.

6. **Check for null**: Null pointer errors are very common.

7. **Check array bounds**: Off-by-one errors are frequent.

8. **Read the stack trace**: It shows how you got to the error.

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix D](d-runtime-reference.md) | [Next: Appendix F: Glossary →](f-glossary.md)*
