# Chapter 7: Breaking It Down

Our programs are growing. The grade tracker from the last chapter was 50+ lines. Real programs are thousands, even millions of lines. How do we manage that complexity?

The answer is the same as how you manage any complex task: break it into smaller, manageable pieces. In programming, these pieces are called *functions*.

---

## What Is a Function?

You've already used functions. `Viper.Terminal.Say()` is a function. `Viper.Parse.Int()` is a function. Someone else wrote the code that makes them work; you just use them by name.

A function is a named, reusable block of code. You define it once, then *call* it whenever you need that behavior. This has enormous benefits:

**Reusability**: Write once, use many times. Need to greet someone? Call the greet function.

**Abstraction**: Hide complexity behind a simple name. You don't need to know how `Say` works internally to use it.

**Organization**: Group related code together. A function named `calculateTax` clearly contains tax calculation logic.

**Testing**: Test each function independently. If `calculateTax` works correctly, you can trust it throughout your program.

---

## Defining Functions

Here's a simple function:

```rust
func greet() {
    Viper.Terminal.Say("Hello!");
    Viper.Terminal.Say("Welcome to the program.");
}
```

This defines a function named `greet` that, when called, prints two lines.

To use it:

```rust
module Greeting;

func greet() {
    Viper.Terminal.Say("Hello!");
    Viper.Terminal.Say("Welcome to the program.");
}

func start() {
    greet();  // Call the function
    greet();  // Call it again
}
```

Output:
```
Hello!
Welcome to the program.
Hello!
Welcome to the program.
```

The function runs twice because we called it twice. The code inside the function is written once but executed each time it's called.

---

## Parameters: Giving Functions Information

A function that always does the exact same thing is limited. Usually, we want to customize behavior:

```rust
func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name + "!");
}

func start() {
    greet("Alice");
    greet("Bob");
    greet("Carol");
}
```

Output:
```
Hello, Alice!
Hello, Bob!
Hello, Carol!
```

The `name: string` in parentheses is a *parameter*. It's like a variable that gets its value when the function is called. `greet("Alice")` sets `name` to `"Alice"` for that call.

You can have multiple parameters:

```rust
func introduce(name: string, age: i64) {
    Viper.Terminal.Say(name + " is " + age + " years old.");
}

func start() {
    introduce("Alice", 30);
    introduce("Bob", 25);
}
```

Output:
```
Alice is 30 years old.
Bob is 25 years old.
```

Parameters are separated by commas. Each has a name and a type.

---

## Return Values: Getting Information Back

Some functions compute a value. They *return* it to the caller:

```rust
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

func start() {
    var sum = add(3, 4);
    Viper.Terminal.Say(sum);  // 7
}
```

The `-> i64` indicates this function returns an integer. The `return` statement specifies what value to return.

Think of it like a recipe: ingredients go in (parameters), a dish comes out (return value). The function `add` takes two numbers in and produces their sum out.

You can use returned values directly:

```rust
Viper.Terminal.Say(add(10, 20));           // 30
Viper.Terminal.Say(add(add(1, 2), 3));     // 6 (1+2=3, 3+3=6)
var result = add(5, 5) * 2;                // 20
```

---

## Functions That Don't Return Values

Some functions do things without computing a result — like printing, saving files, or modifying state. These don't specify a return type:

```rust
func sayGoodbye() {
    Viper.Terminal.Say("Goodbye!");
}
```

You can use `return` without a value to exit early:

```rust
func maybeGreet(shouldGreet: bool) {
    if !shouldGreet {
        return;  // Exit immediately
    }
    Viper.Terminal.Say("Hello!");
}
```

---

## Local Variables

Variables created inside a function are *local* — they exist only within that function:

```rust
func calculateArea(width: i64, height: i64) -> i64 {
    var area = width * height;  // Local variable
    return area;
}

func start() {
    var result = calculateArea(5, 3);
    Viper.Terminal.Say(result);  // 15
    // Viper.Terminal.Say(area);  // Error! 'area' doesn't exist here
}
```

The variable `area` exists only while `calculateArea` is running. When the function returns, it's gone. This is called *scope* — the region where a name is valid.

Each function call gets its own set of local variables. Two simultaneous calls to the same function don't interfere with each other.

---

## Putting It Together

Let's reorganize our grade tracker using functions:

```rust
module GradeTracker;

// Read grades from user, return array
func readGrades() -> [i64] {
    var grades: [i64] = [];

    Viper.Terminal.Say("Enter grades (enter -1 to finish):");

    while true {
        Viper.Terminal.Print("Grade: ");
        var input = Viper.Parse.Int(Viper.Terminal.ReadLine());

        if input == -1 {
            break;
        }

        if input >= 0 && input <= 100 {
            grades.push(input);
        } else {
            Viper.Terminal.Say("Please enter 0-100, or -1 to finish.");
        }
    }

    return grades;
}

// Calculate sum of grades
func sum(grades: [i64]) -> i64 {
    var total = 0;
    for grade in grades {
        total = total + grade;
    }
    return total;
}

// Find minimum grade
func min(grades: [i64]) -> i64 {
    var minimum = grades[0];
    for grade in grades {
        if grade < minimum {
            minimum = grade;
        }
    }
    return minimum;
}

// Find maximum grade
func max(grades: [i64]) -> i64 {
    var maximum = grades[0];
    for grade in grades {
        if grade > maximum {
            maximum = grade;
        }
    }
    return maximum;
}

// Print the report
func printReport(grades: [i64]) {
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Grade Report ===");
    Viper.Terminal.Say("Number of grades: " + grades.length);
    Viper.Terminal.Say("Lowest grade: " + min(grades));
    Viper.Terminal.Say("Highest grade: " + max(grades));
    Viper.Terminal.Say("Average: " + sum(grades) / grades.length);
}

func start() {
    var grades = readGrades();

    if grades.length == 0 {
        Viper.Terminal.Say("No grades entered.");
        return;
    }

    printReport(grades);
}
```

Compare this to the monolithic version from Chapter 6. Each function has one job:
- `readGrades` handles input
- `sum`, `min`, `max` handle calculations
- `printReport` handles output
- `start` coordinates everything

This is easier to understand, test, and modify. Want to change how grades are read? Just edit `readGrades`. Want to add median calculation? Add a `median` function.

---

## Function Design Guidelines

**Do one thing.** A function should have a single, clear purpose. If you find yourself writing "and" in the description ("reads grades AND calculates average"), consider splitting it.

**Name it clearly.** `calculateArea` is better than `calc` or `doStuff`. Function names are often verbs: `calculate`, `print`, `get`, `find`, `validate`.

**Keep it short.** If a function is more than 20-30 lines, it might be doing too much. Look for parts you can extract into helper functions.

**Minimize side effects.** A function that takes inputs and returns outputs, without modifying anything else, is easiest to understand and test. Functions that print, modify global state, or change their inputs are harder to reason about.

---

## The Three Languages

**ViperLang**
```rust
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name);
}

func start() {
    var sum = add(3, 4);
    greet("Alice");
}
```

**BASIC**
```basic
FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    Add = a + b
END FUNCTION

SUB Greet(name AS STRING)
    PRINT "Hello, "; name
END SUB

' Main program
DIM sum AS INTEGER
sum = Add(3, 4)
CALL Greet("Alice")
```

BASIC distinguishes between `FUNCTION` (returns a value) and `SUB` (no return value). The return value is assigned to the function name.

**Pascal**
```pascal
function Add(a, b: Integer): Integer;
begin
    Add := a + b;
end;

procedure Greet(name: string);
begin
    WriteLn('Hello, ', name);
end;

begin
    var sum := Add(3, 4);
    Greet('Alice');
end.
```

Pascal uses `function` (returns a value) and `procedure` (no return value). Like BASIC, the return value is assigned to the function name.

---

## Recursion: Functions Calling Themselves

A function can call itself. This is called *recursion*, and it's a powerful technique for problems that have a self-similar structure.

The classic example is factorial. 5! = 5 × 4 × 3 × 2 × 1. Notice that 5! = 5 × 4!, and 4! = 4 × 3!, and so on. The factorial of N is N times the factorial of N-1.

```rust
func factorial(n: i64) -> i64 {
    if n <= 1 {
        return 1;  // Base case: 0! = 1! = 1
    }
    return n * factorial(n - 1);  // Recursive case
}

func start() {
    Viper.Terminal.Say(factorial(5));  // 120
}
```

How it works for `factorial(5)`:
1. `factorial(5)` returns `5 * factorial(4)`
2. `factorial(4)` returns `4 * factorial(3)`
3. `factorial(3)` returns `3 * factorial(2)`
4. `factorial(2)` returns `2 * factorial(1)`
5. `factorial(1)` returns `1` (base case!)
6. Unwind: `2*1=2`, `3*2=6`, `4*6=24`, `5*24=120`

Every recursive function needs:
- **Base case**: When to stop recurring (here: n <= 1)
- **Recursive case**: How to break the problem into smaller pieces

Without a base case, you get infinite recursion — the function calls itself forever until the program crashes.

Another example — Fibonacci:

```rust
func fib(n: i64) -> i64 {
    if n <= 1 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

func start() {
    for i in 0..10 {
        Viper.Terminal.Print(fib(i) + " ");
    }
}
```

Output: `0 1 1 2 3 5 8 13 21 34`

Recursion is elegant but can be inefficient. The naive Fibonacci above recalculates the same values many times. Later, we'll learn techniques to optimize this.

---

## Common Mistakes

**Forgetting to return:**
```rust
func add(a: i64, b: i64) -> i64 {
    var sum = a + b;
    // Oops! Forgot 'return sum'
}
```

**Wrong parameter order:**
```rust
func greet(name: string, age: i64) { ... }

greet(25, "Alice");  // Error! Arguments are swapped
greet("Alice", 25);  // Correct
```

**Infinite recursion:**
```rust
func forever(n: i64) -> i64 {
    return forever(n);  // Never stops!
}
```

**Trying to use local variables outside their function:**
```rust
func compute() {
    var result = 42;
}

func start() {
    compute();
    Viper.Terminal.Say(result);  // Error! 'result' doesn't exist here
}
```

---

## Summary

- Functions are named, reusable blocks of code
- Parameters let you pass information to functions
- Return values let functions give information back
- Local variables exist only within their function
- `return` exits a function and optionally provides a value
- Good functions do one thing and have clear names
- Recursion is when a function calls itself (needs a base case!)

---

## Exercises

**Exercise 7.1**: Write a function `double(n: i64) -> i64` that returns n × 2. Test it.

**Exercise 7.2**: Write a function `isEven(n: i64) -> bool` that returns true if n is even.

**Exercise 7.3**: Write a function `max3(a: i64, b: i64, c: i64) -> i64` that returns the largest of three numbers.

**Exercise 7.4**: Write a function `countVowels(text: string) -> i64` that returns how many vowels (a, e, i, o, u) are in the text.

**Exercise 7.5**: Write a function `isPrime(n: i64) -> bool` that returns true if n is prime. Use it to print all primes from 1 to 100.

**Exercise 7.6** (Challenge): Write a recursive function `power(base: i64, exp: i64) -> i64` that calculates base^exp. For example, `power(2, 3)` should return 8.

**Exercise 7.7** (Challenge): Write a recursive function to reverse a string. Hint: the reverse of "hello" is (reverse of "ello") + "h".

---

*We've finished Part I! You now understand the foundations: values, variables, decisions, loops, arrays, and functions. These concepts appear in every programming language.*

*Part II builds on this foundation with more sophisticated techniques: working with text, files, errors, and organizing larger programs.*

*[Continue to Part II: Building Blocks →](../part2-building-blocks/08-strings.md)*
