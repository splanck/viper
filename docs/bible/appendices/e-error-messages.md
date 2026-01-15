# Appendix E: Error Messages

A comprehensive guide to Viper error messages, their causes, and solutions. When you encounter an error, find it here for a clear explanation and fix.

---

## How to Use This Reference

Error messages in Viper follow a consistent format:

```
Error: ErrorType at filename.zia:LINE:COLUMN
  Description of what went wrong

    LINE-1 | previous line of code
  > LINE   | the problematic line
           |     ^^^ indicator pointing to the issue
    LINE+1 | next line of code

  in function 'functionName' at filename.zia:LINE
  called from 'callerName' at filename.zia:LINE
```

**Reading strategy:**
1. Note the **error type** (e.g., `TypeError`, `SyntaxError`)
2. Check the **line and column** numbers
3. Read the **description** for specifics
4. Look at the **code context** shown
5. Trace the **call stack** if the error is in a function

---

## Quick Reference by Category

| Category | Common Errors |
|----------|---------------|
| [Syntax Errors](#syntax-errors) | Missing punctuation, invalid tokens, unclosed delimiters |
| [Type Errors](#type-errors) | Mismatched types, invalid operations, conversion failures |
| [Name Errors](#name-errors) | Undefined variables, duplicate definitions, scope issues |
| [Function Errors](#function-errors) | Wrong arguments, missing returns, signature mismatches |
| [Entity/Interface Errors](#entityinterface-errors) | Unimplemented methods, access violations, inheritance issues |
| [Runtime Errors](#runtime-errors) | Null access, array bounds, division by zero |
| [File/IO Errors](#fileio-errors) | Missing files, permission issues, format errors |
| [Memory Errors](#memory-errors) | Out of memory, stack overflow, resource exhaustion |
| [Concurrency Errors](#concurrency-errors) | Deadlocks, race conditions, synchronization issues |
| [Module Errors](#module-errors) | Import failures, circular dependencies, missing exports |

---

## Syntax Errors

Syntax errors occur when your code violates the grammar rules of Zia. The compiler catches these before your program runs.

> **Cross-reference:** See [Chapter 2: Your First Program](../part1-foundations/02-first-program.md) for basic syntax rules.

---

### "Unexpected token"

```
Error: SyntaxError at main.zia:5:5
  Unexpected token 'else'
```

**What it means:** The compiler found a keyword or symbol where it wasn't expected, usually because something is missing earlier in the code.

**Common causes:**
- Missing braces `{}` around blocks
- Missing parentheses in conditions
- Missing semicolons
- Typos in keywords

**Fix examples:**

```rust
// Problem: Missing braces
if x > 0
    doSomething();
else              // Error: unexpected 'else'
    doOther();

// Solution 1: Add braces
if x > 0 {
    doSomething();
} else {
    doOther();
}

// Solution 2: Single-line format (for simple cases)
if x > 0 { doSomething(); } else { doOther(); }
```

```rust
// Problem: Missing parentheses
if x > 0 && y < 10 {  // OK in Viper, but...

// Problem: Missing closing paren
if (x > 0 && y < 10 {  // Error: unexpected '{'
    doSomething();
}

// Solution: Balance parentheses
if (x > 0 && y < 10) {
    doSomething();
}
```

**Prevention:** Use an editor with syntax highlighting and bracket matching. When you type an opening brace, immediately type the closing one, then fill in the middle.

---

### "Expected ';'"

```
Error: SyntaxError at main.zia:10:1
  Expected ';' after statement
```

**What it means:** A statement ended without the required semicolon.

**Common causes:**
- Simply forgetting the semicolon
- Breaking a statement across lines incorrectly
- Copy-pasting code and missing the trailing semicolon

**Fix examples:**

```rust
// Problem: Missing semicolon
var x = 10
var y = 20;    // Error reported here, but problem is line above

// Solution: Add semicolon
var x = 10;
var y = 20;
```

```rust
// Problem: Multi-line statement broken incorrectly
var result = someFunction(arg1, arg2)
    + anotherFunction(arg3);  // Error: unexpected '+'

// Solution: Keep operator at end of previous line
var result = someFunction(arg1, arg2) +
    anotherFunction(arg3);
```

**Prevention:** Configure your editor to highlight missing semicolons. Many editors can auto-insert them on save.

---

### "Expected '}'"

```
Error: SyntaxError at main.zia:EOF
  Expected '}' at end of block
```

**What it means:** A block (function, if, loop, etc.) was opened with `{` but never closed with `}`.

**Common causes:**
- Forgetting to close a nested block
- Deleting a closing brace during editing
- Mismatched braces from copy-paste

**Fix examples:**

```rust
// Problem: Missing closing brace
func calculate(x: i64) -> i64 {
    if x > 0 {
        return x * 2;
    // Missing } for the if
    return 0;
}  // This closes the function, but if is still open

// Solution: Add the missing brace
func calculate(x: i64) -> i64 {
    if x > 0 {
        return x * 2;
    }  // Close the if
    return 0;
}
```

**Prevention:**
- Use an editor that highlights matching braces
- Keep consistent indentation so mismatches are visually obvious
- When you type `{`, immediately type `}` and then fill in between

---

### "Invalid character"

```
Error: SyntaxError at main.zia:3:15
  Invalid character '@'
```

**What it means:** A character that isn't part of Zia syntax was found in your code.

**Common causes:**
- Copying code from websites (hidden special characters)
- Using smart quotes instead of straight quotes
- Typing symbols that aren't valid operators

**Fix examples:**

```rust
// Problem: Smart quotes from word processor
var message = "Hello";  // Uses curly quotes

// Solution: Use straight quotes
var message = "Hello";
```

```rust
// Problem: Invalid symbol
var email = user@domain.com;  // @ isn't valid here

// Solution: Make it a string
var email = "user@domain.com";
```

**Prevention:** Use a plain-text editor designed for code, not a word processor. If pasting from the web, use "Paste as plain text" or type it manually.

---

### "Unterminated string literal"

```
Error: SyntaxError at main.zia:7:20
  Unterminated string literal
```

**What it means:** A string was opened with `"` but never closed.

**Common causes:**
- Forgetting the closing quote
- Having an unescaped quote inside the string
- Multi-line string without proper continuation

**Fix examples:**

```rust
// Problem: Missing closing quote
var message = "Hello, world!;

// Solution: Add closing quote
var message = "Hello, world!";
```

```rust
// Problem: Quote inside string
var quote = "She said "Hello"";  // Inner quotes end the string early

// Solution: Escape inner quotes
var quote = "She said \"Hello\"";
```

```rust
// Problem: Unintended line break
var text = "This is a very long message that
continues on the next line";  // Error on first line

// Solution 1: Keep on one line
var text = "This is a very long message that continues on the next line";

// Solution 2: Use concatenation
var text = "This is a very long message that " +
           "continues on the next line";

// Solution 3: Use \n for intentional line breaks
var text = "Line one\nLine two";
```

**Prevention:** When typing a quote, type both opening and closing, then fill in the middle.

---

### "Expected identifier"

```
Error: SyntaxError at main.zia:5:5
  Expected identifier after 'var'
```

**What it means:** The compiler expected a name (for a variable, function, etc.) but found something else.

**Common causes:**
- Starting a name with a number
- Using a reserved keyword as a name
- Typo that resulted in invalid characters

**Fix examples:**

```rust
// Problem: Name starts with number
var 2ndPlace = "Silver";  // Error

// Solution: Start with letter or underscore
var secondPlace = "Silver";
var _2ndPlace = "Silver";
```

```rust
// Problem: Using keyword as name
var func = 10;    // Error: 'func' is a keyword
var if = true;    // Error: 'if' is a keyword

// Solution: Choose different names
var funcValue = 10;
var condition = true;
```

**Prevention:** Use descriptive names that start with a lowercase letter. Avoid single-letter names except for loop counters.

> **Cross-reference:** See [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md) for naming conventions.

---

## Type Errors

Type errors occur when you use a value in a way that doesn't match its type.

> **Cross-reference:** See [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md) for type fundamentals.

---

### "Type mismatch"

```
Error: TypeError at main.zia:7:14
  Type mismatch: expected 'i64', got 'string'
```

**What it means:** You used a value of one type where a different type was required.

**Common causes:**
- Assigning wrong type to a variable
- Passing wrong type to a function
- Returning wrong type from a function

**Fix examples:**

```rust
// Problem: Wrong type assignment
var count: i64 = "hello";  // Can't put string in i64 variable

// Solution 1: Use correct type
var count: i64 = 42;

// Solution 2: Change variable type
var count: string = "hello";

// Solution 3: Convert the value (if appropriate)
var count: i64 = Viper.Parse.Int("42");
```

```rust
// Problem: Wrong parameter type
func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name);
}

greet(42);  // Error: expected string, got i64

// Solution: Pass correct type
greet("Alice");
greet(42.toString());  // Convert if needed
```

```rust
// Problem: Wrong return type
func getAge() -> i64 {
    return "twenty-five";  // Error: expected i64
}

// Solution: Return correct type
func getAge() -> i64 {
    return 25;
}
```

**Prevention:** Be explicit about types when declaring variables. Use type annotations to catch mismatches early.

---

### "Cannot assign to immutable variable"

```
Error: TypeError at main.zia:12:1
  Cannot assign to immutable variable 'PI'
```

**What it means:** You tried to change a value that was declared as constant.

**Common causes:**
- Trying to modify a `final` constant
- Modifying a function parameter (which may be immutable)

**Fix examples:**

```rust
// Problem: Modifying constant
final MAX_SCORE = 100;
MAX_SCORE = 200;  // Error: cannot assign to constant

// Solution 1: Use 'var' if it needs to change
var maxScore = 100;
maxScore = 200;  // OK

// Solution 2: Create new variable
final MAX_SCORE = 100;
var currentMax = MAX_SCORE + 100;  // OK: new variable
```

```rust
// Problem: Should be var, not final
final counter = 0;
counter += 1;  // Error

// Solution: Use var for values that change
var counter = 0;
counter += 1;  // OK
```

**Prevention:** Use `final` only for true constants (values that should never change). Use `var` for variables that will be modified.

> **Cross-reference:** See [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md) for var vs final.

---

### "Incompatible types in binary operation"

```
Error: TypeError at main.zia:8:20
  Cannot apply '+' to 'string' and 'i64'
```

**What it means:** You tried to use an operator with types that don't support that operation together.

**Common causes:**
- Mixing strings and numbers in arithmetic
- Using comparison operators with incompatible types
- Attempting operations that don't make sense

**Fix examples:**

```rust
// Problem: Adding string and number
var result = "Value: " + 42;  // May error depending on context

// Solution 1: Convert number to string explicitly
var result = "Value: " + 42.toString();

// Solution 2: Use string interpolation
var result = "Value: ${42}";
```

```rust
// Problem: Comparing incompatible types
var name = "Alice";
var age = 30;
if name > age {  // Error: can't compare string to number
    // ...
}

// Solution: Compare same types
if name.length > age {  // Compare two numbers
    // ...
}
```

```rust
// Problem: Arithmetic on strings
var a = "5";
var b = "3";
var sum = a + b;  // Results in "53", not 8!

// Solution: Convert to numbers first
var a = Viper.Parse.Int("5");
var b = Viper.Parse.Int("3");
var sum = a + b;  // 8
```

**Prevention:** Be aware of your variable types. When doing math, ensure all operands are numeric. When building strings, convert numbers explicitly.

---

### "Cannot convert type"

```
Error: TypeError at main.zia:15:12
  Cannot convert 'string' to 'i64': invalid format
```

**What it means:** A type conversion was attempted but failed because the value couldn't be converted.

**Common causes:**
- Parsing non-numeric string as number
- Invalid format for date/time parsing
- Casting between incompatible entity types

**Fix examples:**

```rust
// Problem: Parsing non-number
var num = Viper.Parse.Int("hello");  // Error: "hello" is not a number

// Solution 1: Validate before parsing
var input = "hello";
if input.isNumeric() {
    var num = Viper.Parse.Int(input);
}

// Solution 2: Use try-catch
try {
    var num = Viper.Parse.Int(input);
} catch e: ParseError {
    Viper.Terminal.Say("Please enter a valid number");
}
```

```rust
// Problem: Trailing characters
var num = Viper.Parse.Int("42abc");  // Error or unexpected result

// Solution: Clean input first
var input = "42abc";
var cleaned = input.trim().replaceAll("[^0-9]", "");
var num = Viper.Parse.Int(cleaned);
```

**Prevention:** Always validate user input before parsing. Use try-catch when conversion might fail.

> **Cross-reference:** See [Chapter 10: Errors and Recovery](../part2-building-blocks/10-errors.md) for handling parse errors.

---

### "Null pointer exception" / "Cannot access property of null"

```
Error: NullPointerError at main.zia:15:18
  Cannot access property 'name' of null value
```

**What it means:** You tried to use a method or field on a value that is null.

**Common causes:**
- Not initializing an object
- Function returning null unexpectedly
- Array lookup returning null
- Optional value not checked

**Fix examples:**

```rust
// Problem: Using null value
var user = findUser(id);
Viper.Terminal.Say(user.name);  // Error if user is null!

// Solution 1: Check for null
var user = findUser(id);
if user != null {
    Viper.Terminal.Say(user.name);
} else {
    Viper.Terminal.Say("User not found");
}

// Solution 2: Use null-safe operator (if available)
var name = user?.name ?? "Unknown";

// Solution 3: Provide default
var user = findUser(id) ?? defaultUser;
```

```rust
// Problem: Array element might be null
var item = inventory[index];
item.use();  // Error if slot is empty (null)

// Solution: Check first
var item = inventory[index];
if item != null {
    item.use();
}
```

**Prevention:**
- Initialize all variables
- Check return values from functions that might return null
- Document which functions can return null
- Consider using optional types where appropriate

---

## Name Errors

Name errors occur when you use a name that doesn't exist or conflicts with another name.

---

### "Undefined variable"

```
Error: NameError at main.zia:15:20
  Undefined variable 'count'
```

**What it means:** You used a variable name that hasn't been declared.

**Common causes:**
- Typo in variable name
- Using variable before declaring it
- Variable declared in different scope
- Forgetting to import a module

**Fix examples:**

```rust
// Problem: Typo
var counter = 0;
Viper.Terminal.Say(counte);  // Error: typo in name

// Solution: Fix spelling
Viper.Terminal.Say(counter);
```

```rust
// Problem: Used before declaration
Viper.Terminal.Say(score);  // Error: score doesn't exist yet
var score = 100;

// Solution: Declare before use
var score = 100;
Viper.Terminal.Say(score);
```

```rust
// Problem: Out of scope
if someCondition {
    var temp = 42;
}
Viper.Terminal.Say(temp);  // Error: temp not visible here

// Solution: Declare in outer scope
var temp = 0;
if someCondition {
    temp = 42;
}
Viper.Terminal.Say(temp);
```

**Prevention:**
- Use consistent naming conventions
- Declare variables at the beginning of their scope
- Use an IDE with autocomplete to catch typos

---

### "Undefined function"

```
Error: NameError at main.zia:20:5
  Undefined function 'calulate'
```

**What it means:** You called a function that doesn't exist.

**Common causes:**
- Typo in function name
- Function defined in unimported module
- Function defined after it's called (in some cases)
- Function is a method but called as standalone

**Fix examples:**

```rust
// Problem: Typo
var result = calulate(5, 3);  // Should be 'calculate'

// Solution: Fix spelling
var result = calculate(5, 3);
```

```rust
// Problem: Missing import
var data = Json.parse(text);  // Error: Json not defined

// Solution: Import the module
bind Viper.Json;
var data = Json.parse(text);
```

```rust
// Problem: Calling method as function
var length = length(myString);  // Error

// Solution: Call as method
var length = myString.length();
```

**Prevention:** Use IDE autocomplete. When a function isn't found, check: (1) spelling, (2) imports, (3) whether it's a method.

---

### "Duplicate definition"

```
Error: NameError at main.zia:30:6
  Duplicate definition of 'processData'
```

**What it means:** The same name is defined twice in the same scope.

**Common causes:**
- Copy-paste error
- Importing two modules with same function name
- Declaring variable twice

**Fix examples:**

```rust
// Problem: Same function defined twice
func calculate(x: i64) -> i64 {
    return x * 2;
}

func calculate(x: i64) -> i64 {  // Error: duplicate
    return x * 3;
}

// Solution 1: Rename one
func calculateDouble(x: i64) -> i64 {
    return x * 2;
}

func calculateTriple(x: i64) -> i64 {
    return x * 3;
}

// Solution 2: Use overloading (different parameters)
func calculate(x: i64) -> i64 {
    return x * 2;
}

func calculate(x: i64, y: i64) -> i64 {
    return x + y;
}
```

```rust
// Problem: Variable declared twice
var count = 0;
// ... later ...
var count = 10;  // Error: count already exists

// Solution: Just assign
var count = 0;
// ... later ...
count = 10;  // No 'var', just assignment
```

**Prevention:** Use meaningful, specific names. Search your codebase before adding new names.

---

### "Variable used before declaration"

```
Error: NameError at main.zia:5:12
  Variable 'total' used before declaration
```

**What it means:** You referenced a variable on a line before its `var` declaration.

**Fix:**

```rust
// Problem: Usage before declaration
Viper.Terminal.Say(total);  // Error
var total = 100;

// Solution: Move declaration before use
var total = 100;
Viper.Terminal.Say(total);  // OK
```

---

### "Cannot shadow variable"

```
Error: NameError at main.zia:12:9
  Cannot shadow variable 'x' from outer scope
```

**What it means:** You declared a variable with the same name as one in an outer scope, which can cause confusion.

**Fix examples:**

```rust
// Problem: Shadowing outer variable
var count = 0;
for i in 0..10 {
    var count = i;  // Shadows outer 'count' - confusing!
}

// Solution: Use different names
var totalCount = 0;
for i in 0..10 {
    var currentCount = i;
    totalCount += currentCount;
}
```

**Prevention:** Use descriptive names that distinguish different variables. Avoid reusing names in nested scopes.

---

## Function Errors

Errors related to function definitions and calls.

> **Cross-reference:** See [Chapter 7: Functions](../part1-foundations/07-functions.md) for function basics.

---

### "Wrong number of arguments"

```
Error: ArgumentError at main.zia:10:5
  Function 'add' expects 2 arguments, got 3
```

**What it means:** You called a function with more or fewer arguments than it accepts.

**Fix examples:**

```rust
// The function definition
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

// Problem: Too many arguments
var result = add(1, 2, 3);  // Error: expects 2, got 3

// Solution: Match the signature
var result = add(1, 2);  // OK

// Problem: Too few arguments
var result = add(1);  // Error: expects 2, got 1

// Solution: Provide all required arguments
var result = add(1, 0);  // OK
```

```rust
// If you need variable arguments, use an array
func sum(numbers: [i64]) -> i64 {
    var total = 0;
    for n in numbers {
        total += n;
    }
    return total;
}

var result = sum([1, 2, 3, 4, 5]);  // Pass array
```

**Prevention:** Check the function signature before calling. Use IDE features to see parameter hints.

---

### "Missing return statement"

```
Error: TypeError at main.zia:25:1
  Function 'getValue' must return a value of type 'i64'
```

**What it means:** A function declares a return type but doesn't always return a value.

**Fix examples:**

```rust
// Problem: No return statement
func getValue() -> i64 {
    var x = 42;
    // Function ends without returning!
}

// Solution: Add return
func getValue() -> i64 {
    var x = 42;
    return x;
}
```

```rust
// Problem: Return only in some branches
func getStatus(code: i64) -> string {
    if code == 0 {
        return "OK";
    } else if code == 1 {
        return "Warning";
    }
    // What if code is 2? No return!
}

// Solution: Ensure all paths return
func getStatus(code: i64) -> string {
    if code == 0 {
        return "OK";
    } else if code == 1 {
        return "Warning";
    }
    return "Error";  // Default case
}
```

**Prevention:** When writing a function with a return type, ensure every possible execution path ends with a return statement.

---

### "Cannot return value from void function"

```
Error: TypeError at main.zia:8:5
  Cannot return a value from function with no return type
```

**What it means:** You tried to return a value from a function that doesn't declare a return type.

**Fix examples:**

```rust
// Problem: Returning from void function
func printMessage(msg: string) {
    Viper.Terminal.Say(msg);
    return msg;  // Error: function doesn't return anything
}

// Solution 1: Remove the return value
func printMessage(msg: string) {
    Viper.Terminal.Say(msg);
    return;  // OK: return without value
}

// Solution 2: Add return type if needed
func printMessage(msg: string) -> string {
    Viper.Terminal.Say(msg);
    return msg;  // OK: function returns string
}
```

---

### "Argument type mismatch"

```
Error: TypeError at main.zia:15:12
  Argument 1: expected 'string', got 'i64'
```

**What it means:** A function was called with an argument of the wrong type.

**Fix examples:**

```rust
func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name);
}

// Problem: Wrong argument type
greet(42);  // Error: expected string, got i64

// Solution: Pass correct type
greet("Alice");  // OK
greet(42.toString());  // OK: convert if needed
```

---

## Entity/Interface Errors

Errors related to entities (Viper's term for classes), interfaces, and object-oriented programming.

> **Cross-reference:** See [Chapter 14: Objects and Entities](../part3-objects/14-objects.md) and [Chapter 16: Interfaces](../part3-objects/16-interfaces.md).

---

### "Entity does not implement interface"

```
Error: TypeError at main.zia:20:8
  Entity 'Circle' does not implement method 'draw' from interface 'Drawable'
```

**What it means:** An entity claims to implement an interface but is missing one or more required methods.

**Fix examples:**

```rust
interface Drawable {
    func draw();
    func getColor() -> string;
}

// Problem: Missing method
entity Circle implements Drawable {
    radius: f64;

    func draw() {
        Viper.Terminal.Say("Drawing circle");
    }
    // Missing getColor()!
}

// Solution: Implement all methods
entity Circle implements Drawable {
    radius: f64;
    color: string;

    func draw() {
        Viper.Terminal.Say("Drawing circle with radius " + self.radius);
    }

    func getColor() -> string {
        return self.color;
    }
}
```

**Prevention:** When implementing an interface, check its definition and implement every method it requires.

---

### "Cannot access hidden member"

```
Error: AccessError at main.zia:30:15
  Cannot access hidden member 'balance' of entity 'BankAccount'
```

**What it means:** You tried to access a field or method marked `hide` from outside the entity.

**Fix examples:**

```rust
entity BankAccount {
    hide balance: f64;  // Hidden field

    expose func getBalance() -> f64 {
        return self.balance;
    }
}

// Problem: Accessing hidden field
var account = BankAccount();
Viper.Terminal.Say(account.balance);  // Error: balance is hidden

// Solution: Use exposed method
Viper.Terminal.Say(account.getBalance());  // OK
```

**Prevention:** Design your entities with clear exposed/hidden boundaries. Use `expose` for the public interface and `hide` for internal implementation.

> **Cross-reference:** See [Chapter 14: Objects and Entities](../part3-objects/14-objects.md) for encapsulation concepts.

---

### "Method signature mismatch"

```
Error: TypeError at main.zia:25:5
  Override of 'speak' has different signature than parent
```

**What it means:** When overriding a method from a parent entity, the signature must match exactly.

**Fix examples:**

```rust
entity Animal {
    func speak() -> string {
        return "...";
    }
}

// Problem: Different return type
entity Dog extends Animal {
    func speak() -> i64 {  // Error: parent returns string
        return 1;
    }
}

// Problem: Different parameters
entity Dog extends Animal {
    func speak(loudly: bool) -> string {  // Error: parent has no params
        return "Woof!";
    }
}

// Solution: Match signature exactly
entity Dog extends Animal {
    func speak() -> string {
        return "Woof!";
    }
}
```

---

### "Missing initializer"

```
Error: TypeError at main.zia:15:12
  Cannot create 'Player' without initializer
```

**What it means:** You tried to create an entity instance without providing required initialization values.

**Fix examples:**

```rust
entity Player {
    name: string;
    health: i64;

    expose func init(name: string, health: i64) {
        self.name = name;
        self.health = health;
    }
}

// Problem: No arguments
var player = Player();  // Error: needs name and health

// Solution: Provide required arguments
var player = Player("Alice", 100);  // OK
```

---

### "Cannot access 'self' in static context"

```
Error: ContextError at main.zia:12:16
  Cannot access 'self' outside of method context
```

**What it means:** You used `self` in a context where there's no object instance.

**Fix examples:**

```rust
entity Counter {
    count: i64;

    // Problem: Using self in static function
    static func printCount() {
        Viper.Terminal.Say(self.count);  // Error: no self in static
    }
}

// Solution 1: Make it a regular method
entity Counter {
    count: i64;

    func printCount() {
        Viper.Terminal.Say(self.count);  // OK: self exists in methods
    }
}

// Solution 2: Pass instance as parameter
entity Counter {
    count: i64;

    static func printCount(counter: Counter) {
        Viper.Terminal.Say(counter.count);  // Access via parameter
    }
}
```

---

## Runtime Errors

Runtime errors occur while your program is running, typically when an operation is impossible to perform.

> **Cross-reference:** See [Chapter 10: Errors and Recovery](../part2-building-blocks/10-errors.md) for handling runtime errors.

---

### "Index out of bounds"

```
Error: IndexError at main.zia:20:15
  Array index 10 is out of bounds for array of length 5
```

**What it means:** You tried to access an array element at a position that doesn't exist.

**Common causes:**
- Off-by-one errors (arrays are 0-indexed)
- Loop going too far
- Not checking array length
- Empty array access

**Fix examples:**

```rust
// Problem: Index too high
var items = [1, 2, 3, 4, 5];  // Indices 0-4
var x = items[5];  // Error: index 5 doesn't exist

// Solution: Use valid index
var x = items[4];  // OK: last valid index
```

```rust
// Problem: Off-by-one in loop
var items = ["a", "b", "c"];
for i in 0..items.length + 1 {  // Goes to 3, but max index is 2
    Viper.Terminal.Say(items[i]);  // Error on last iteration
}

// Solution: Correct loop bounds
for i in 0..items.length {  // 0 to 2 (exclusive)
    Viper.Terminal.Say(items[i]);  // OK
}

// Better: Use for-each
for item in items {
    Viper.Terminal.Say(item);  // No index needed
}
```

```rust
// Problem: Not checking for empty array
var scores: [i64] = [];
var first = scores[0];  // Error: array is empty

// Solution: Check length first
if scores.length > 0 {
    var first = scores[0];  // OK
}
```

**Prevention:**
- Use for-each loops when possible
- Always validate indices before use
- Check array length before accessing elements
- Remember: first index is 0, last index is length - 1

---

### "Division by zero"

```
Error: ArithmeticError at main.zia:12:16
  Division by zero
```

**What it means:** You divided a number by zero, which is mathematically undefined.

**Fix examples:**

```rust
// Problem: Literal division by zero
var result = 10 / 0;  // Error

// Problem: Variable could be zero
var total = 100;
var count = 0;
var average = total / count;  // Error: dividing by 0

// Solution: Check before dividing
if count != 0 {
    var average = total / count;
} else {
    Viper.Terminal.Say("Cannot calculate average: no items");
}
```

```rust
// Solution: Provide default for zero divisor
func safeDivide(a: i64, b: i64, default: i64) -> i64 {
    if b == 0 {
        return default;
    }
    return a / b;
}

var average = safeDivide(total, count, 0);
```

**Prevention:** Always validate divisors before division operations, especially when the divisor comes from user input or calculation.

---

### "Integer overflow"

```
Error: OverflowError at main.zia:8:12
  Integer overflow: result exceeds i64 range
```

**What it means:** A calculation produced a number too large (or too small) to fit in the integer type.

**Fix examples:**

```rust
// Problem: Result too large
var big = 9223372036854775807;  // Max i64
var bigger = big + 1;  // Error: overflow

// Solution 1: Use checked arithmetic
var result = big.checkedAdd(1);  // Returns null on overflow

// Solution 2: Use larger type or arbitrary precision
var big = BigInt("9223372036854775807");
var bigger = big + 1;  // OK with BigInt

// Solution 3: Validate before operation
if big < i64.MAX {
    var bigger = big + 1;  // Safe
}
```

**Prevention:** Be aware of integer limits when working with large numbers. Use appropriate data types for your range of values.

---

### "Type cast failed"

```
Error: CastError at main.zia:30:12
  Cannot cast 'Dog' to 'Cat'
```

**What it means:** You tried to cast an object to a type it isn't compatible with.

**Fix examples:**

```rust
// Problem: Invalid cast
var animal: Animal = Dog("Rex");
var cat = animal as Cat;  // Error: it's a Dog, not a Cat!

// Solution: Check type first
if animal is Cat {
    var cat = animal as Cat;  // Safe: we know it's a Cat
    cat.meow();
} else if animal is Dog {
    var dog = animal as Dog;
    dog.bark();
}
```

```rust
// Solution: Use pattern matching
match animal {
    case cat: Cat {
        cat.meow();
    }
    case dog: Dog {
        dog.bark();
    }
    else {
        Viper.Terminal.Say("Unknown animal");
    }
}
```

**Prevention:** Always use `is` to check types before casting, or use pattern matching.

---

### "Stack overflow"

```
Error: StackOverflowError at main.zia:15:5
  Stack overflow: too many nested function calls
```

**What it means:** Your program ran out of stack space, usually from too-deep recursion.

**Common causes:**
- Infinite recursion (no base case)
- Missing base case in recursive function
- Very deep recursion even with base case

**Fix examples:**

```rust
// Problem: Infinite recursion (no base case)
func countdown(n: i64) {
    Viper.Terminal.Say(n);
    countdown(n - 1);  // Never stops!
}

// Solution: Add base case
func countdown(n: i64) {
    if n < 0 {
        return;  // Base case: stop recursion
    }
    Viper.Terminal.Say(n);
    countdown(n - 1);
}
```

```rust
// Problem: Base case never reached
func factorial(n: i64) -> i64 {
    return n * factorial(n - 1);  // n keeps decreasing forever!
}

// Solution: Proper base case
func factorial(n: i64) -> i64 {
    if n <= 1 {
        return 1;  // Base case
    }
    return n * factorial(n - 1);
}
```

```rust
// Problem: Recursion too deep even with base case
func processDeep(depth: i64) {
    if depth == 0 { return; }
    processDeep(depth - 1);  // With depth = 1000000, stack overflow
}

// Solution: Convert to iteration
func processDeep(depth: i64) {
    for i in 0..depth {
        // Process iteration
    }
}
```

**Prevention:**
- Always define a base case for recursive functions
- Consider iterative solutions for deep recursion
- Use tail recursion when possible (if optimizer supports it)

> **Cross-reference:** See [Chapter 7: Functions](../part1-foundations/07-functions.md) for recursion.

---

## File/IO Errors

Errors related to file operations and input/output.

> **Cross-reference:** See [Chapter 9: Files](../part2-building-blocks/09-files.md) for file operations.

---

### "File not found"

```
Error: FileError at main.zia:5:20
  File not found: 'data.txt'
```

**What it means:** You tried to read a file that doesn't exist.

**Fix examples:**

```rust
// Problem: File doesn't exist
var content = Viper.File.readText("data.txt");  // Error if missing

// Solution 1: Check existence first
if Viper.File.exists("data.txt") {
    var content = Viper.File.readText("data.txt");
} else {
    Viper.Terminal.Say("File not found");
}

// Solution 2: Use try-catch
try {
    var content = Viper.File.readText("data.txt");
} catch e: FileNotFound {
    Viper.Terminal.Say("File not found, creating default...");
    Viper.File.writeText("data.txt", "default content");
}

// Solution 3: Provide path at runtime
Viper.Terminal.Print("Enter filename: ");
var filename = Viper.Terminal.ReadLine();
if Viper.File.exists(filename) {
    var content = Viper.File.readText(filename);
}
```

**Prevention:**
- Check if files exist before reading
- Handle missing files gracefully
- Use relative paths carefully (know your working directory)
- Provide fallback defaults

---

### "Permission denied"

```
Error: FileError at main.zia:10:5
  Permission denied: '/etc/passwd'
```

**What it means:** Your program doesn't have permission to access the file.

**Fix examples:**

```rust
// Problem: Writing to protected location
Viper.File.writeText("/etc/config.txt", data);  // Permission denied

// Solution: Write to allowed location
Viper.File.writeText("./config.txt", data);  // Local directory
Viper.File.writeText(Viper.OS.homeDir() + "/config.txt", data);  // User's home
```

**Prevention:**
- Write files to appropriate locations (user directories, temp directories)
- Don't assume write access to system directories
- Handle permission errors gracefully

---

### "File already exists"

```
Error: FileError at main.zia:15:5
  File already exists: 'output.txt' (exclusive create mode)
```

**What it means:** You tried to create a file that already exists when using exclusive create mode.

**Fix examples:**

```rust
// Problem: File exists in exclusive mode
Viper.File.createNew("output.txt", data);  // Error if exists

// Solution 1: Use overwrite mode
Viper.File.writeText("output.txt", data);  // Overwrites if exists

// Solution 2: Check and decide
if Viper.File.exists("output.txt") {
    Viper.Terminal.Print("File exists. Overwrite? (y/n): ");
    if Viper.Terminal.ReadLine() == "y" {
        Viper.File.writeText("output.txt", data);
    }
} else {
    Viper.File.writeText("output.txt", data);
}

// Solution 3: Generate unique name
var filename = "output_" + Viper.Time.now().toString() + ".txt";
Viper.File.writeText(filename, data);
```

---

### "Parse error"

```
Error: ParseError at main.zia:8:25
  Invalid JSON at position 42: unexpected '}'
```

**What it means:** File content couldn't be parsed in the expected format.

**Fix examples:**

```rust
// Problem: Invalid JSON
var data = Viper.Json.parse("{invalid json}");  // Parse error

// Solution: Validate and handle errors
try {
    var data = Viper.Json.parse(content);
    processData(data);
} catch e: ParseError {
    Viper.Terminal.Say("Invalid file format: " + e.message);
    // Use default or prompt user
}
```

**Prevention:**
- Validate file contents before parsing
- Provide helpful error messages
- Consider using schema validation for complex formats

---

## Memory Errors

Errors related to memory allocation and management.

---

### "Out of memory"

```
Error: MemoryError at main.zia:50:5
  Out of memory: failed to allocate 1073741824 bytes
```

**What it means:** Your program requested more memory than is available.

**Common causes:**
- Creating extremely large arrays
- Loading huge files into memory
- Memory leak from accumulating data
- Infinite loop creating objects

**Fix examples:**

```rust
// Problem: Array too large
var huge = Array.new(1000000000);  // 1 billion elements

// Solution: Process in chunks
func processLargeData(source: DataSource) {
    while source.hasMore() {
        var chunk = source.readChunk(10000);  // Process 10K at a time
        processChunk(chunk);
        // chunk is freed after each iteration
    }
}
```

```rust
// Problem: Loading huge file into memory
var content = Viper.File.readText("huge_file.txt");  // Out of memory

// Solution: Read line by line
var reader = Viper.File.openReader("huge_file.txt");
while reader.hasNextLine() {
    var line = reader.readLine();
    processLine(line);
}
reader.close();
```

**Prevention:**
- Process large data in chunks
- Release resources when done
- Monitor memory usage in development
- Set reasonable limits on data sizes

---

## Concurrency Errors

Errors related to multi-threaded and concurrent programming.

> **Cross-reference:** See [Chapter 24: Concurrency](../part4-applications/24-concurrency.md) for threading.

---

### "Deadlock detected"

```
Error: DeadlockError at runtime
  Deadlock detected: threads waiting on each other
```

**What it means:** Two or more threads are each waiting for the other to release a lock, so neither can proceed.

**Common causes:**
- Acquiring locks in different orders
- Holding one lock while waiting for another
- Circular dependency between locks

**Fix examples:**

```rust
// Problem: Lock ordering causes deadlock
// Thread 1: lock(A) then lock(B)
// Thread 2: lock(B) then lock(A)
// If Thread 1 holds A and Thread 2 holds B, deadlock!

// Solution: Always acquire locks in same order
var lockA = Mutex.create();
var lockB = Mutex.create();

func operation1() {
    lockA.lock();
    lockB.lock();  // Always A before B
    // ... work ...
    lockB.unlock();
    lockA.unlock();
}

func operation2() {
    lockA.lock();  // Same order: A before B
    lockB.lock();
    // ... work ...
    lockB.unlock();
    lockA.unlock();
}
```

**Prevention:**
- Always acquire multiple locks in the same order
- Use lock timeouts
- Consider lock-free data structures
- Keep critical sections small

---

### "Race condition" / "Data race detected"

```
Warning: DataRaceWarning at main.zia:45:12
  Potential data race accessing 'counter' from multiple threads
```

**What it means:** Multiple threads are accessing shared data without proper synchronization.

**Fix examples:**

```rust
// Problem: Unsynchronized access
var counter = 0;

var t1 = Thread.spawn(func() {
    for i in 0..1000 {
        counter += 1;  // Race condition!
    }
});

var t2 = Thread.spawn(func() {
    for i in 0..1000 {
        counter += 1;  // Race condition!
    }
});

// Solution 1: Use mutex
var counter = 0;
var mutex = Mutex.create();

var t1 = Thread.spawn(func() {
    for i in 0..1000 {
        mutex.lock();
        counter += 1;
        mutex.unlock();
    }
});

// Solution 2: Use atomic operations
var counter = Atomic.new(0);

var t1 = Thread.spawn(func() {
    for i in 0..1000 {
        counter.increment();  // Atomic, thread-safe
    }
});
```

**Prevention:**
- Protect shared data with mutexes
- Use atomic operations for simple counters
- Minimize shared mutable state
- Prefer message passing between threads

---

## Module Errors

Errors related to module imports and organization.

> **Cross-reference:** See [Chapter 12: Modules](../part2-building-blocks/12-modules.md) for module system.

---

### "Module not found"

```
Error: ImportError at main.zia:3:8
  Module not found: 'Utils'
```

**What it means:** An imported module couldn't be located.

**Fix examples:**

```rust
// Problem: Module name doesn't match file
bind Utils;  // Looking for Utils.zia, but file is utilities.zia

// Solution 1: Match name to file
bind Utilities;  // Matches utilities.zia

// Solution 2: Use correct path
bind src.utils.Utils;  // For Utils.zia in src/utils/
```

**Prevention:**
- Ensure module names match file names
- Check import paths
- Use consistent naming conventions

---

### "Circular import"

```
Error: ImportError at moduleA.zia:2:8
  Circular import detected: ModuleA -> ModuleB -> ModuleA
```

**What it means:** Two modules import each other, creating a circular dependency.

**Fix examples:**

```rust
// Problem: A imports B, B imports A
// moduleA.zia
bind ModuleB;
// moduleB.zia
bind ModuleA;  // Circular!

// Solution: Extract shared code to third module
// common.zia - shared types/functions
// moduleA.zia - imports Common
// moduleB.zia - imports Common
```

**Prevention:**
- Design module dependencies as a tree, not a graph
- Extract shared functionality to common modules
- Consider whether modules are properly separated

---

### "Symbol not exported"

```
Error: ImportError at main.zia:5:12
  Symbol 'internalFunc' is not exported from module 'Utils'
```

**What it means:** You tried to use something from a module that isn't marked for export.

**Fix examples:**

```rust
// In Utils module:
func internalFunc() { ... }  // Not exported (hidden by default)
expose func publicFunc() { ... }  // Exported

// Problem: Accessing non-exported symbol
bind Utils;
Utils.internalFunc();  // Error: not exported

// Solution 1: Use exported symbol
Utils.publicFunc();  // OK

// Solution 2: Export the symbol (if you control the module)
expose func internalFunc() { ... }  // Now exported
```

---

## Debugging Tips

When you encounter an error:

1. **Read the full message carefully.** Error messages contain the type, location, and often a description of what's wrong.

2. **Check the line BEFORE the error.** Many errors (like missing semicolons) are reported on the line after where the actual problem is.

3. **Trace back through the call stack.** The error might occur in a function, but the bug might be in the code that called it with wrong arguments.

4. **Print intermediate values.** Add `Viper.Terminal.Say()` calls to see what values variables have at different points.

5. **Simplify the problem.** Create a minimal example that reproduces the error. Often, the bug becomes obvious.

6. **Check types carefully.** Many errors stem from type mismatches. Verify your variable types.

7. **Look for null values.** Null pointer errors are extremely common. Check if any value might be null.

8. **Watch array bounds.** Off-by-one errors with array indices are frequent. Remember arrays start at 0.

9. **Use the debugger.** Step through code line by line to see exactly what's happening.

10. **Search for the error message.** Others have likely encountered the same error. Search documentation and forums.

---

## Error Handling Best Practices

### Validate Early

```rust
func processOrder(quantity: i64, price: f64) {
    // Validate at the start
    if quantity <= 0 {
        throw Error("Quantity must be positive");
    }
    if price < 0 {
        throw Error("Price cannot be negative");
    }

    // Now we know inputs are valid
    var total = quantity * price;
    // ...
}
```

### Provide Helpful Messages

```rust
// Unhelpful
throw Error("Invalid input");

// Helpful
throw Error("Invalid age: " + age + ". Age must be between 0 and 150.");
```

### Handle Specific Errors

```rust
try {
    var config = loadConfig();
} catch e: FileNotFound {
    Viper.Terminal.Say("Config file missing, using defaults");
    config = defaultConfig;
} catch e: ParseError {
    Viper.Terminal.Say("Config file corrupted: " + e.message);
    throw e;  // Re-throw - can't recover from this
} catch e {
    Viper.Terminal.Say("Unexpected error: " + e.message);
    log(e);
}
```

### Don't Ignore Errors

```rust
// WRONG: Silent failure
try {
    riskyOperation();
} catch e {
    // Empty catch - errors disappear!
}

// RIGHT: At least log errors
try {
    riskyOperation();
} catch e {
    log("Operation failed: " + e.message);
    // Handle appropriately
}
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix D](d-runtime-reference.md) | [Next: Appendix F: Glossary](f-glossary.md)*
