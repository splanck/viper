# Chapter 7: Breaking It Down

Our programs are growing. The grade tracker from the last chapter was 50+ lines. Real programs are thousands, even millions of lines. How do we manage that complexity?

The answer is the same as how you manage any complex task: break it into smaller, manageable pieces. In programming, these pieces are called *functions*.

Functions are not merely a convenience. They are the fundamental building block of organized, maintainable code. Every professional program you have ever used --- your web browser, your operating system, your favorite game --- is built from thousands of functions, each doing one specific job. Mastering functions transforms you from someone who writes code into someone who *designs* software.

---

## The Pain of Copy-Paste Programming

Before we define functions formally, let's experience the problem they solve. Imagine you're writing a program that needs to greet users at several different points:

```rust
module MessyProgram;

func start() {
    // Welcome the user
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("    Welcome to Our Program!     ");
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("");

    // ... some code happens ...

    // User completed a task, greet them again
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("    Welcome to Our Program!     ");
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("");

    // ... more code ...

    // User returned from settings, greet them again
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("    Welcome to Our Program!     ");
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("");
}
```

This works, but something is deeply wrong. We have copied the same four lines three times. Now imagine your boss says, "Change the greeting to say 'Hello!' instead of 'Welcome!'" You have to find and change three places. Miss one? Bug. What if there were twenty places? What if you copied the code into ten different files?

This is **copy-paste programming**, and it is a trap. Every time you copy code, you create a maintenance nightmare. The copies will diverge --- someone will fix a bug in one copy but not the others. Someone will update one copy but forget the rest. Your program becomes a minefield of subtle inconsistencies.

Let's see an even worse example. Suppose you need to calculate the area of rectangles throughout your program:

```rust
module MessierProgram;

func start() {
    // Calculate area for room 1
    var width1 = 10;
    var height1 = 8;
    var area1 = width1 * height1;
    Viper.Terminal.Say("Room 1 area: " + area1);

    // Calculate area for room 2
    var width2 = 15;
    var height2 = 12;
    var area2 = width2 * height2;
    Viper.Terminal.Say("Room 2 area: " + area2);

    // Calculate area for room 3
    var width3 = 20;
    var height3 = 10;
    var area3 = width3 * heigth3;  // Oops! Typo: heigth3
    Viper.Terminal.Say("Room 3 area: " + area3);

    // Calculate area for room 4
    var width4 = 12;
    var height4 = 9;
    var area4 = width4 + height4;  // Oops! Wrong operator: + instead of *
    Viper.Terminal.Say("Room 4 area: " + area4);
}
```

Did you spot the bugs? When you copy code, you often change variable names or adjust things slightly. Each copy is an opportunity to introduce a typo or logical error. And because the code looks almost identical, your eyes glaze over and miss the mistakes.

Now imagine your requirements change. Instead of simple rectangles, you need to account for walls having a thickness, or rooms having alcoves. You have to find every place you calculate area and update it correctly. One mistake, one forgotten copy, and your calculations are wrong.

**Functions solve all of these problems.** Write the logic once, give it a name, use it everywhere. Change it in one place, and every use gets the fix automatically. One source of truth. No copy-paste. No divergence. No maintenance nightmares.

---

## What Is a Function?

You've already used functions. `Viper.Terminal.Say()` is a function. `Viper.Parse.Int()` is a function. Someone else wrote the code that makes them work; you just use them by name.

A function is a named, reusable block of code. You define it once, then *call* it whenever you need that behavior. Think of a function like a recipe in a cookbook. The recipe has a name ("Chocolate Cake"), a list of ingredients you need to provide (flour, eggs, sugar), and a series of steps. When you want a chocolate cake, you don't invent the process from scratch --- you follow the recipe. You can make the cake multiple times, with slightly different ingredients (more sugar for a sweeter cake), and you always get a cake back.

Functions work the same way:
- They have a **name** (like `calculateArea`)
- They may require **inputs** (like width and height)
- They perform some **operations** (the recipe steps)
- They may produce an **output** (the finished cake... or a computed number)

This has enormous benefits:

**Reusability**: Write once, use many times. Need to greet someone? Call the greet function. Need to calculate ten different areas? Call the area function ten times with different inputs.

**Abstraction**: Hide complexity behind a simple name. You don't need to know how `Say` works internally to use it. You don't need to know how your car's engine works to drive --- you just press the gas pedal. Functions provide the same simplicity for code.

**Organization**: Group related code together. A function named `calculateTax` clearly contains tax calculation logic. When you need to understand or modify tax calculations, you know exactly where to look.

**Testing**: Test each function independently. If `calculateTax` works correctly with a variety of inputs, you can trust it throughout your program. You don't have to trace through hundreds of lines of code to verify correctness.

**Debugging**: When something goes wrong, functions help you isolate the problem. If `calculateTax` returns wrong values, the bug is in `calculateTax`. You don't have to search your entire program.

---

## The Black Box Concept

One of the most powerful ideas in programming is thinking of functions as **black boxes**. You don't need to see inside to use them. You only need to know:

1. **What goes in** (the inputs, called parameters)
2. **What comes out** (the output, called the return value)
3. **What the function does** (its purpose, described by its name)

Imagine a vending machine. You put in money and press a button (inputs). A snack comes out (output). You don't need to know how the internal mechanisms work --- the conveyor belts, the sensors, the coin counter. The machine is a black box. Input goes in, output comes out, internal details are hidden.

```
     +-----------------+
     |                 |
---->|  calculateArea  |----> 50
 5   |                 |
---->|   (black box)   |
 10  |                 |
     +-----------------+
```

You give `calculateArea` the values 5 and 10. It returns 50. How does it calculate that? Maybe it multiplies. Maybe it uses some complex algorithm. Maybe it asks a server over the internet. You don't care. As long as the right answer comes out, the internal implementation is irrelevant to you as the user of the function.

This separation between *what a function does* and *how it does it* is called **abstraction**. It's one of the most important concepts in all of computer science. Abstraction lets you:

- **Use code without understanding it**: You use `Viper.Terminal.Say()` without understanding terminal I/O, buffering, or system calls.
- **Change implementations without breaking users**: If someone improves how `calculateArea` works internally (making it faster, for example), your code keeps working unchanged.
- **Build complex systems from simple parts**: Each function is a black box. You combine black boxes to build bigger black boxes. A car is made of an engine, transmission, wheels --- each a black box you use without fully understanding.

When you write your own functions, think about them as black boxes you're creating for other programmers (including your future self). What inputs does it need? What output does it produce? How would you describe its purpose in one sentence? If you can answer these questions clearly, you've designed a good function.

---

## Defining Functions

Here's a simple function:

```rust
func greet() {
    Viper.Terminal.Say("Hello!");
    Viper.Terminal.Say("Welcome to the program.");
}
```

Let's break down the syntax:

- `func` --- keyword that says "I'm defining a function"
- `greet` --- the name of the function (you choose this)
- `()` --- parentheses for parameters (empty means no parameters)
- `{ }` --- curly braces containing the function's code (called the function body)

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

Notice the syntax for calling a function: the function name followed by parentheses. The parentheses are required even if there are no parameters. `greet()` calls the function. `greet` without parentheses refers to the function itself as a value (more on this in later chapters).

**A crucial point**: Defining a function does not execute it. The code inside `greet` does not run when the computer reads the function definition. It only runs when you *call* the function with `greet()`. You can define a hundred functions, and none of their code runs until you call them.

---

## Parameters and Arguments: Giving Functions Information

A function that always does the exact same thing is limited. Usually, we want to customize behavior. We do this by giving functions **parameters** --- placeholders for values that will be provided when the function is called.

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

### The Difference Between Parameters and Arguments

These two terms are often confused, but they mean different things:

- **Parameter**: The variable defined in the function declaration. It's a placeholder, a slot waiting to be filled. In `func greet(name: string)`, `name` is a parameter.

- **Argument**: The actual value you provide when calling the function. In `greet("Alice")`, the string `"Alice"` is an argument.

Think of it this way: the **parameter** is the parking space, the **argument** is the car you park in it.

```rust
func greet(name: string) {    // 'name' is the PARAMETER (the parking space)
    Viper.Terminal.Say("Hello, " + name + "!");
}

func start() {
    greet("Alice");           // "Alice" is the ARGUMENT (the car)
    greet("Bob");             // "Bob" is another ARGUMENT

    var person = "Carol";
    greet(person);            // The value of 'person' ("Carol") is the ARGUMENT
}
```

### How Values Flow Into Functions

When you call `greet("Alice")`, here's what happens:

1. The computer sees you're calling `greet` with the argument `"Alice"`
2. It creates a new variable called `name` (the parameter)
3. It copies the value `"Alice"` into `name`
4. It executes the function body, where `name` has the value `"Alice"`
5. When the function ends, `name` disappears

For the next call, `greet("Bob")`:

1. A fresh `name` variable is created
2. `"Bob"` is copied into it
3. The function body runs with `name` being `"Bob"`
4. When the function ends, this `name` also disappears

Each call gets its own copy of the parameters. They don't interfere with each other.

### Multiple Parameters

You can have multiple parameters, separated by commas:

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

**Order matters!** The first argument goes to the first parameter, the second argument to the second parameter, and so on. `introduce("Alice", 30)` sets `name` to `"Alice"` and `age` to `30`. If you wrote `introduce(30, "Alice")`, you'd get an error because `30` is not a string and `"Alice"` is not a number.

### Parameters Are Copies

When you pass a value to a function, the function receives a **copy** of that value (for simple types like numbers and strings). Modifying the parameter inside the function does not affect the original:

```rust
func tryToChange(x: i64) {
    x = 999;  // This changes the local copy
    Viper.Terminal.Say("Inside function: " + x);
}

func start() {
    var number = 42;
    Viper.Terminal.Say("Before: " + number);  // 42
    tryToChange(number);                       // "Inside function: 999"
    Viper.Terminal.Say("After: " + number);   // Still 42!
}
```

The function received a copy of `42`. Changing the copy doesn't change the original `number` variable. This is called **pass by value**, and it's a safety feature --- functions can't accidentally mess up your variables.

(Note: For complex types like arrays and objects, the behavior is different. We'll cover this in later chapters.)

---

## Return Values: Getting Information Back

Some functions compute a value and give it back to the caller. They *return* a result:

```rust
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

func start() {
    var sum = add(3, 4);
    Viper.Terminal.Say(sum);  // 7
}
```

### Understanding the Syntax

- `-> i64` after the parentheses declares that this function returns an integer. The arrow `->` can be read as "produces" or "results in."
- `return a + b;` specifies what value to send back.

### What "Return" Really Means

When a function executes a `return` statement, two things happen:

1. **The function ends immediately.** No code after the `return` runs.
2. **The specified value is sent back to the caller.** The function call expression "becomes" that value.

Think of it like a tennis ball machine. You press the button (call the function), the machine does some internal work (the function body), and a ball shoots out (the return value). The `return` statement is the moment the ball leaves the machine.

```rust
func add(a: i64, b: i64) -> i64 {
    return a + b;
    Viper.Terminal.Say("This never runs!");  // Dead code - never executed
}
```

The `Say` line never executes because `return` exits the function immediately.

### What Happens to the Returned Value?

When a function returns a value, that value appears at the place where the function was called. It's as if the function call gets replaced by the returned value:

```rust
var sum = add(3, 4);
// After add() returns, this becomes:
// var sum = 7;
```

The returned value can be used in any way a regular value can be used:

```rust
// Store it in a variable
var result = add(3, 4);

// Use it directly in output
Viper.Terminal.Say(add(3, 4));           // prints 7

// Use it in an expression
var doubled = add(3, 4) * 2;             // 14

// Use it as an argument to another function
var bigger = add(add(1, 2), add(3, 4));  // add(3, 7) = 10

// Use it in a condition
if add(3, 4) > 5 {
    Viper.Terminal.Say("Sum is big!");
}
```

### The Flow of a Function Call

Let's trace through `var bigger = add(add(1, 2), add(3, 4));` step by step:

1. To evaluate the outer `add()`, we need its arguments
2. First argument: `add(1, 2)`
   - Jump into `add`, with `a=1`, `b=2`
   - Calculate `1 + 2 = 3`
   - Return `3`
   - We're back, and `add(1, 2)` has become `3`
3. Second argument: `add(3, 4)`
   - Jump into `add`, with `a=3`, `b=4`
   - Calculate `3 + 4 = 7`
   - Return `7`
   - We're back, and `add(3, 4)` has become `7`
4. Now the outer call is `add(3, 7)`
   - Jump into `add`, with `a=3`, `b=7`
   - Calculate `3 + 7 = 10`
   - Return `10`
5. Finally: `var bigger = 10`

This nested evaluation is powerful. Functions can use other functions, which can use other functions, building complex computations from simple parts.

---

## Functions That Don't Return Values

Some functions do things without computing a result --- like printing, saving files, or modifying state. These functions perform *actions* rather than *calculations*. They don't specify a return type:

```rust
func sayGoodbye() {
    Viper.Terminal.Say("Goodbye!");
}

func printLine() {
    Viper.Terminal.Say("================");
}

func start() {
    sayGoodbye();   // Works fine
    printLine();    // Works fine

    // But this doesn't make sense:
    // var x = sayGoodbye();  // Error! sayGoodbye doesn't return anything
}
```

You can use `return` without a value to exit early:

```rust
func maybeGreet(shouldGreet: bool) {
    if !shouldGreet {
        return;  // Exit immediately, do nothing else
    }
    Viper.Terminal.Say("Hello!");
}

func start() {
    maybeGreet(true);   // Prints "Hello!"
    maybeGreet(false);  // Prints nothing (exits early)
}
```

Early returns are useful for handling special cases at the beginning of a function, so the main logic doesn't have to be nested inside conditions.

---

## Understanding Scope: Where Variables Live

Variables created inside a function are *local* --- they exist only within that function. This region where a variable is valid is called its **scope**.

```rust
func calculateArea(width: i64, height: i64) -> i64 {
    var area = width * height;  // 'area' is born here
    return area;
}                               // 'area' dies here

func start() {
    var result = calculateArea(5, 3);
    Viper.Terminal.Say(result);  // 15
    // Viper.Terminal.Say(area);  // Error! 'area' doesn't exist here
}
```

The variable `area` exists only while `calculateArea` is running. When the function returns, `area` is destroyed. It's as if the variable was written on a whiteboard that gets erased when the function ends.

### Why Scope Matters

Scope is a feature, not a limitation. It provides **isolation**:

```rust
func calculateRectangleArea(w: i64, h: i64) -> i64 {
    var temp = w * h;
    return temp;
}

func calculateTriangleArea(base: i64, height: i64) -> i64 {
    var temp = base * height / 2;  // Same name 'temp', different variable!
    return temp;
}

func start() {
    var r = calculateRectangleArea(10, 5);
    var t = calculateTriangleArea(10, 5);
    Viper.Terminal.Say(r);  // 50
    Viper.Terminal.Say(t);  // 25
}
```

Both functions use a variable named `temp`, but they don't conflict. Each function has its own isolated world. What happens inside `calculateRectangleArea` doesn't affect `calculateTriangleArea`.

Without scope, every variable name in your entire program would have to be unique. In a large program with thousands of functions, you'd run out of good names quickly. With scope, you can reuse simple names like `i`, `temp`, `result`, `count` in every function without worry.

### Local vs. Global Variables

A **local variable** is declared inside a function and exists only within that function. A **global variable** is declared outside any function and exists throughout the program.

```rust
module ScopeDemo;

var globalCounter = 0;  // Global: accessible everywhere

func incrementCounter() {
    globalCounter = globalCounter + 1;  // Can access global
    var localValue = 100;               // Local: only in this function
}

func printCounter() {
    Viper.Terminal.Say(globalCounter);  // Can access global
    // Viper.Terminal.Say(localValue);  // Error! localValue is not in scope
}

func start() {
    incrementCounter();
    incrementCounter();
    printCounter();  // Prints 2
}
```

**Use global variables sparingly.** They create hidden connections between functions. When any function can modify a global, it becomes hard to understand what might change it. Bugs become harder to track down. Prefer passing values through parameters and returning results --- this makes the flow of data explicit and easy to follow.

### Nested Scope

Blocks (code inside `{ }`) create nested scopes:

```rust
func scopeExample() {
    var outer = 10;

    if true {
        var inner = 20;
        Viper.Terminal.Say(outer);  // OK: can see outer from inside
        Viper.Terminal.Say(inner);  // OK: inner is in scope
    }

    Viper.Terminal.Say(outer);  // OK: still in scope
    // Viper.Terminal.Say(inner);  // Error! inner went out of scope
}
```

Inner scopes can see variables from outer scopes, but outer scopes cannot see into inner scopes. Think of it like rooms in a house: from inside a closet (inner scope), you can see into the bedroom (outer scope), but from the bedroom you can't see what's inside a closed closet.

### Why Can't You Access Variables from Other Functions?

Each function is completely separate. Variables in one function are invisible to all other functions:

```rust
func functionA() {
    var secretA = 42;
}

func functionB() {
    // Viper.Terminal.Say(secretA);  // Error! secretA doesn't exist here
}
```

This is essential for **modularity**. When you write `functionB`, you don't need to know or care what variables `functionA` uses. They're private implementation details. You could completely rewrite the inside of `functionA`, changing all its variable names, and `functionB` would be unaffected.

If all functions could see all variables, changing anything would be terrifying. You'd have to search your entire codebase to see if any other function depends on that variable. With proper scope, you can change a function's internals with confidence.

---

## The Call Stack: Functions Calling Functions

When functions call other functions, the computer needs to keep track of where to return to. It does this using a **call stack**.

A stack is a data structure like a stack of plates: you add plates to the top, and remove plates from the top. The last plate added is the first one removed (Last In, First Out, or LIFO).

### Visualizing the Call Stack

Let's trace through this code:

```rust
func multiply(a: i64, b: i64) -> i64 {
    return a * b;
}

func square(n: i64) -> i64 {
    return multiply(n, n);
}

func sumOfSquares(x: i64, y: i64) -> i64 {
    var sq1 = square(x);
    var sq2 = square(y);
    return sq1 + sq2;
}

func start() {
    var result = sumOfSquares(3, 4);
    Viper.Terminal.Say(result);
}
```

Here's how the call stack evolves:

**Step 1**: `start()` is called
```
+------------------+
|  start()         |  <-- current
+------------------+
```

**Step 2**: `start` calls `sumOfSquares(3, 4)`
```
+------------------+
|  sumOfSquares()  |  <-- current
|  x=3, y=4        |
+------------------+
|  start()         |
+------------------+
```

**Step 3**: `sumOfSquares` calls `square(3)`
```
+------------------+
|  square()        |  <-- current
|  n=3             |
+------------------+
|  sumOfSquares()  |
|  x=3, y=4        |
+------------------+
|  start()         |
+------------------+
```

**Step 4**: `square` calls `multiply(3, 3)`
```
+------------------+
|  multiply()      |  <-- current
|  a=3, b=3        |
+------------------+
|  square()        |
|  n=3             |
+------------------+
|  sumOfSquares()  |
|  x=3, y=4        |
+------------------+
|  start()         |
+------------------+
```

**Step 5**: `multiply` returns 9, gets popped off
```
+------------------+
|  square()        |  <-- current (has 9)
|  n=3             |
+------------------+
|  sumOfSquares()  |
|  x=3, y=4        |
+------------------+
|  start()         |
+------------------+
```

**Step 6**: `square` returns 9, gets popped off
```
+------------------+
|  sumOfSquares()  |  <-- current (has sq1=9)
|  x=3, y=4        |
+------------------+
|  start()         |
+------------------+
```

**Step 7**: `sumOfSquares` calls `square(4)`, same process...

Eventually, the stack unwinds completely and `start` prints `25` (which is 3^2 + 4^2 = 9 + 16 = 25).

### Why the Call Stack Matters

1. **Each call gets its own variables**: When `square` is called twice (with 3 and 4), each call has its own `n`. The stack keeps them separate.

2. **The computer knows where to return**: When `multiply` finishes, the computer pops it off the stack and sees `square` is waiting. When `square` finishes, it returns to `sumOfSquares`.

3. **Stack overflow**: The stack has limited size. If you call functions too deeply (often from runaway recursion), you'll run out of stack space and crash with a "stack overflow" error.

Understanding the call stack helps you trace through complex code and debug problems. When something goes wrong, error messages often show a "stack trace" --- the list of functions that were active when the error occurred, which is essentially a snapshot of the call stack.

---

## Function Overloading: Same Name, Different Signatures

In many languages (including ViperLang), you can define multiple functions with the same name as long as they have different parameters. This is called **function overloading**.

```rust
func greet() {
    Viper.Terminal.Say("Hello, stranger!");
}

func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name + "!");
}

func greet(name: string, times: i64) {
    for i in 0..times {
        Viper.Terminal.Say("Hello, " + name + "!");
    }
}

func start() {
    greet();                  // "Hello, stranger!"
    greet("Alice");           // "Hello, Alice!"
    greet("Bob", 3);          // "Hello, Bob!" three times
}
```

The compiler looks at the arguments you provide and chooses the matching function:
- `greet()` --- no arguments, calls the first version
- `greet("Alice")` --- one string argument, calls the second version
- `greet("Bob", 3)` --- string and integer, calls the third version

### When to Use Overloading

Overloading is useful for:

1. **Providing defaults**: A no-argument version uses defaults, a parameterized version lets users customize.

2. **Handling different types**:
```rust
func printValue(n: i64) {
    Viper.Terminal.Say("Integer: " + n);
}

func printValue(s: string) {
    Viper.Terminal.Say("String: " + s);
}

func printValue(b: bool) {
    Viper.Terminal.Say("Boolean: " + b);
}
```

3. **Convenience functions**: A simpler version for common cases, a more detailed version for special cases.

### Overloading Rules

- Functions must differ in the number or types of parameters
- Return type alone is not enough to distinguish overloaded functions
- Be careful not to create ambiguous overloads that the compiler can't resolve

---

## Recursion: Functions That Call Themselves

A function can call itself. This is called *recursion*, and it's a powerful technique for problems that have a self-similar structure --- problems that can be broken into smaller versions of the same problem.

### The Classic Example: Factorial

Factorial is the product of all positive integers up to n. For example, 5! (read "five factorial") = 5 x 4 x 3 x 2 x 1 = 120.

Notice something interesting: 5! = 5 x 4! And 4! = 4 x 3! The factorial of n is n times the factorial of (n-1). This is a recursive definition --- factorial is defined in terms of itself.

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

### How Recursion Works

Let's trace `factorial(5)` step by step:

```
factorial(5)
  = 5 * factorial(4)
  = 5 * (4 * factorial(3))
  = 5 * (4 * (3 * factorial(2)))
  = 5 * (4 * (3 * (2 * factorial(1))))
  = 5 * (4 * (3 * (2 * 1)))         <- base case reached!
  = 5 * (4 * (3 * 2))               <- unwinding begins
  = 5 * (4 * 6)
  = 5 * 24
  = 120
```

The function keeps calling itself with smaller values until it hits the base case (n <= 1). Then all the pending multiplications complete as the calls return.

On the call stack:

```
+------------------+
|  factorial(1)    |  <- returns 1 (base case)
+------------------+
|  factorial(2)    |  <- waiting for factorial(1), then returns 2*1=2
+------------------+
|  factorial(3)    |  <- waiting for factorial(2), then returns 3*2=6
+------------------+
|  factorial(4)    |  <- waiting for factorial(3), then returns 4*6=24
+------------------+
|  factorial(5)    |  <- waiting for factorial(4), then returns 5*24=120
+------------------+
|  start()         |
+------------------+
```

### The Two Requirements for Recursion

Every recursive function needs:

1. **Base case**: A condition that stops the recursion. Without this, the function calls itself forever (until it crashes with stack overflow). In factorial, the base case is `n <= 1`.

2. **Progress toward the base case**: Each recursive call must get closer to the base case. In factorial, we call `factorial(n - 1)`, which is smaller than n, so we're always getting closer to 1.

### Another Example: Fibonacci Numbers

The Fibonacci sequence is: 0, 1, 1, 2, 3, 5, 8, 13, 21, ... Each number is the sum of the two before it.

```rust
func fib(n: i64) -> i64 {
    if n <= 1 {
        return n;  // Base cases: fib(0) = 0, fib(1) = 1
    }
    return fib(n - 1) + fib(n - 2);  // Recursive case
}

func start() {
    for i in 0..10 {
        Viper.Terminal.Print(fib(i) + " ");
    }
}
```

Output: `0 1 1 2 3 5 8 13 21 34`

### The Danger of Naive Recursion

The Fibonacci function above is elegant but inefficient. To calculate `fib(5)`, it calculates `fib(4)` and `fib(3)`. But `fib(4)` also calculates `fib(3)`. We're doing the same work multiple times!

```
fib(5)
├── fib(4)
│   ├── fib(3)
│   │   ├── fib(2)
│   │   │   ├── fib(1)
│   │   │   └── fib(0)
│   │   └── fib(1)
│   └── fib(2)
│       ├── fib(1)
│       └── fib(0)
└── fib(3)          <- calculated again!
    ├── fib(2)      <- calculated again!
    │   ├── fib(1)
    │   └── fib(0)
    └── fib(1)
```

For `fib(5)`, this is fine. For `fib(50)`, this naive approach would take an astronomical number of operations. There are techniques to fix this (memoization, dynamic programming), which we'll cover in later chapters. For now, just be aware that recursion can be inefficient if you're not careful.

### When to Use Recursion

Recursion shines when:
- The problem naturally breaks into smaller versions of itself
- You're working with recursive data structures (trees, nested lists)
- The iterative solution would be complex and hard to understand

For simple loops (counting from 1 to n), prefer iteration. For problems with natural recursive structure, recursion can be more elegant and easier to understand.

---

## Practical Refactoring: Making Messy Code Clean

Let's see functions in action by taking messy code and improving it. Here's a disorganized program that calculates statistics about numbers:

### Before: Messy, Repetitive Code

```rust
module MessyStats;

func start() {
    var numbers: [i64] = [4, 8, 15, 16, 23, 42];

    // Calculate sum
    var sum = 0;
    for n in numbers {
        sum = sum + n;
    }
    Viper.Terminal.Say("Sum: " + sum);

    // Calculate average
    var sum2 = 0;
    for n in numbers {
        sum2 = sum2 + n;
    }
    var avg = sum2 / numbers.length;
    Viper.Terminal.Say("Average: " + avg);

    // Find minimum
    var min = numbers[0];
    for n in numbers {
        if n < min {
            min = n;
        }
    }
    Viper.Terminal.Say("Minimum: " + min);

    // Find maximum
    var max = numbers[0];
    for n in numbers {
        if n > max {
            max = n;
        }
    }
    Viper.Terminal.Say("Maximum: " + max);

    // Calculate range
    var min2 = numbers[0];
    for n in numbers {
        if n < min2 {
            min2 = n;
        }
    }
    var max2 = numbers[0];
    for n in numbers {
        if n > max2 {
            max2 = n;
        }
    }
    var range = max2 - min2;
    Viper.Terminal.Say("Range: " + range);
}
```

This code has serious problems:
- Sum calculation is duplicated
- Min calculation is duplicated (and could just reuse `min`!)
- Max calculation is duplicated
- Everything is in one giant function
- Hard to test any individual piece
- If the sum algorithm needs to change, we have to change it in two places

### After: Clean, Organized Code

```rust
module CleanStats;

// Calculate the sum of an array of numbers
func sum(numbers: [i64]) -> i64 {
    var total = 0;
    for n in numbers {
        total = total + n;
    }
    return total;
}

// Calculate the average of an array of numbers
func average(numbers: [i64]) -> i64 {
    return sum(numbers) / numbers.length;
}

// Find the minimum value in an array
func minimum(numbers: [i64]) -> i64 {
    var min = numbers[0];
    for n in numbers {
        if n < min {
            min = n;
        }
    }
    return min;
}

// Find the maximum value in an array
func maximum(numbers: [i64]) -> i64 {
    var max = numbers[0];
    for n in numbers {
        if n > max {
            max = n;
        }
    }
    return max;
}

// Calculate the range (max - min)
func range(numbers: [i64]) -> i64 {
    return maximum(numbers) - minimum(numbers);
}

// Print all statistics for an array
func printStats(numbers: [i64]) {
    Viper.Terminal.Say("Sum: " + sum(numbers));
    Viper.Terminal.Say("Average: " + average(numbers));
    Viper.Terminal.Say("Minimum: " + minimum(numbers));
    Viper.Terminal.Say("Maximum: " + maximum(numbers));
    Viper.Terminal.Say("Range: " + range(numbers));
}

func start() {
    var numbers: [i64] = [4, 8, 15, 16, 23, 42];
    printStats(numbers);
}
```

The improved version:
- Each function has one job
- No duplicated code
- `average` reuses `sum`, `range` reuses `minimum` and `maximum`
- Each function can be tested independently
- Adding new statistics is easy --- just add a new function
- `start` is simple and clear

### The Refactoring Process

When you have messy code, here's how to refactor it:

1. **Identify repeated code**: Look for copy-pasted sections or very similar logic.

2. **Name the concept**: What is this code doing? "Calculating the sum." "Finding the minimum." The name tells you what function to create.

3. **Extract into a function**: Move the code into a new function. Replace the original code with a function call.

4. **Identify inputs and outputs**: What values does this code need (parameters)? What does it produce (return value)?

5. **Look for functions calling functions**: Can your new functions build on each other? `average` can call `sum`. `range` can call `minimum` and `maximum`.

6. **Test**: Make sure everything still works after each change.

---

## Function Design Principles

Over decades, programmers have developed principles for writing good functions. These aren't arbitrary rules --- they come from hard-won experience about what makes code maintainable.

### Do One Thing (Single Responsibility)

A function should have a single, clear purpose. If you find yourself using "and" to describe what a function does ("calculates the average AND prints it AND saves it to a file"), consider splitting it.

```rust
// Bad: Does three things
func processGrades(grades: [i64]) {
    var sum = 0;
    for g in grades { sum = sum + g; }
    var avg = sum / grades.length;
    Viper.Terminal.Say("Average: " + avg);
    Viper.File.Write("grades.txt", "Average: " + avg);
}

// Good: Each function does one thing
func calculateAverage(grades: [i64]) -> i64 {
    var sum = 0;
    for g in grades { sum = sum + g; }
    return sum / grades.length;
}

func displayAverage(avg: i64) {
    Viper.Terminal.Say("Average: " + avg);
}

func saveAverage(avg: i64) {
    Viper.File.Write("grades.txt", "Average: " + avg);
}

func processGrades(grades: [i64]) {
    var avg = calculateAverage(grades);
    displayAverage(avg);
    saveAverage(avg);
}
```

Single-responsibility functions are easier to test, easier to understand, and easier to reuse. `calculateAverage` can be used anywhere you need an average, even if you don't want to print or save it.

### Name Functions Clearly

Function names should describe what the function does. Good names are usually verbs or verb phrases:

- `calculate`, `compute` --- for functions that compute values
- `get`, `fetch`, `find` --- for functions that retrieve data
- `is`, `has`, `can` --- for functions that return booleans (`isEmpty`, `hasPermission`, `canProceed`)
- `print`, `display`, `show` --- for functions that output information
- `save`, `write`, `store` --- for functions that persist data
- `validate`, `check`, `verify` --- for functions that check conditions
- `create`, `make`, `build` --- for functions that construct things
- `convert`, `transform`, `parse` --- for functions that change data formats

Bad names:
- `doStuff()` --- what stuff?
- `process()` --- process how?
- `handle()` --- vague
- `x()` --- meaningless

Good names:
- `calculateTax(income: i64) -> i64`
- `validateEmail(email: string) -> bool`
- `formatCurrency(amount: i64) -> string`
- `findUserByEmail(email: string) -> User`

A good test: can someone understand what the function does just from its name and parameters, without reading the code inside?

### Keep Functions Short

If a function is more than 20-30 lines, it's probably doing too much. Long functions are hard to understand, hard to test, and often contain subtle bugs.

When you find a long function, look for sections that could be extracted:
- A block of code with a comment explaining it? That comment is begging to become a function name.
- Similar code repeated? Extract it.
- Deep nesting (if inside if inside loop inside if)? Extract inner parts.

Short functions are also easier to name. If you struggle to name a function, it might be doing too many things.

### Minimize Side Effects

A **pure function** takes inputs and returns outputs without modifying anything else. Given the same inputs, it always returns the same output.

```rust
// Pure function: no side effects
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

// Pure function: no side effects
func formatName(first: string, last: string) -> string {
    return last + ", " + first;
}
```

A function with **side effects** changes something outside itself:

```rust
var counter = 0;

// Side effect: modifies global variable
func incrementCounter() {
    counter = counter + 1;
}

// Side effect: performs I/O
func printMessage(msg: string) {
    Viper.Terminal.Say(msg);
}
```

Side effects aren't always bad --- printing output is a side effect, and programs need to do that! But minimize unnecessary side effects. Pure functions are:
- Easier to test (just check that inputs produce expected outputs)
- Easier to understand (no hidden state changes)
- Easier to parallelize (no shared state to corrupt)
- Easier to debug (behavior is predictable)

When a function must have side effects, make them obvious from the name (`printReport`, `saveFile`, `updateDatabase`).

### Write Functions at Consistent Levels of Abstraction

Within a single function, keep all the code at the same level of abstraction. Don't mix high-level operations with low-level details.

```rust
// Bad: Mixed levels of abstraction
func processOrder(order: Order) {
    // High level
    validateOrder(order);

    // Suddenly low-level string manipulation
    var email = order.customerEmail;
    var atPos = email.indexOf("@");
    var domain = email.substring(atPos + 1);
    if domain == "spam.com" {
        return;
    }

    // Back to high level
    calculateTotals(order);
    chargePayment(order);
    sendConfirmation(order);
}

// Good: Consistent abstraction level
func processOrder(order: Order) {
    validateOrder(order);
    if isSpamEmail(order.customerEmail) {
        return;
    }
    calculateTotals(order);
    chargePayment(order);
    sendConfirmation(order);
}

func isSpamEmail(email: string) -> bool {
    var atPos = email.indexOf("@");
    var domain = email.substring(atPos + 1);
    return domain == "spam.com";
}
```

The improved version reads like a story: validate, check for spam, calculate, charge, send. The details of spam detection are hidden in their own function.

---

## Putting It Together: The Complete Grade Tracker

Let's see all these principles applied to a complete, well-organized program:

```rust
module GradeTracker;

// ============================================
// Input Functions
// ============================================

// Prompt for and read a single grade from the user
// Returns -1 if user wants to finish
func readGrade() -> i64 {
    Viper.Terminal.Print("Grade: ");
    return Viper.Parse.Int(Viper.Terminal.ReadLine());
}

// Check if a grade is within valid range
func isValidGrade(grade: i64) -> bool {
    return grade >= 0 && grade <= 100;
}

// Read all grades from user until they enter -1
func readAllGrades() -> [i64] {
    var grades: [i64] = [];

    Viper.Terminal.Say("Enter grades (0-100). Enter -1 to finish.");
    Viper.Terminal.Say("");

    while true {
        var grade = readGrade();

        if grade == -1 {
            break;
        }

        if isValidGrade(grade) {
            grades.push(grade);
        } else {
            Viper.Terminal.Say("Invalid grade. Please enter 0-100, or -1 to finish.");
        }
    }

    return grades;
}

// ============================================
// Calculation Functions
// ============================================

// Calculate the sum of all grades
func sum(grades: [i64]) -> i64 {
    var total = 0;
    for grade in grades {
        total = total + grade;
    }
    return total;
}

// Calculate the average grade
func average(grades: [i64]) -> i64 {
    if grades.length == 0 {
        return 0;
    }
    return sum(grades) / grades.length;
}

// Find the lowest grade
func minimum(grades: [i64]) -> i64 {
    var min = grades[0];
    for grade in grades {
        if grade < min {
            min = grade;
        }
    }
    return min;
}

// Find the highest grade
func maximum(grades: [i64]) -> i64 {
    var max = grades[0];
    for grade in grades {
        if grade > max {
            max = grade;
        }
    }
    return max;
}

// Convert numeric grade to letter grade
func letterGrade(grade: i64) -> string {
    if grade >= 90 { return "A"; }
    if grade >= 80 { return "B"; }
    if grade >= 70 { return "C"; }
    if grade >= 60 { return "D"; }
    return "F";
}

// Count how many grades fall into each letter category
func countLetterGrades(grades: [i64]) -> [i64] {
    var counts: [i64] = [0, 0, 0, 0, 0];  // A, B, C, D, F

    for grade in grades {
        var letter = letterGrade(grade);
        if letter == "A" { counts[0] = counts[0] + 1; }
        if letter == "B" { counts[1] = counts[1] + 1; }
        if letter == "C" { counts[2] = counts[2] + 1; }
        if letter == "D" { counts[3] = counts[3] + 1; }
        if letter == "F" { counts[4] = counts[4] + 1; }
    }

    return counts;
}

// ============================================
// Output Functions
// ============================================

// Print a separator line
func printSeparator() {
    Viper.Terminal.Say("================================");
}

// Print the main statistics
func printStatistics(grades: [i64]) {
    Viper.Terminal.Say("Number of grades: " + grades.length);
    Viper.Terminal.Say("Sum of grades:    " + sum(grades));
    Viper.Terminal.Say("Average grade:    " + average(grades));
    Viper.Terminal.Say("Lowest grade:     " + minimum(grades));
    Viper.Terminal.Say("Highest grade:    " + maximum(grades));
    Viper.Terminal.Say("Class average:    " + letterGrade(average(grades)));
}

// Print the grade distribution
func printDistribution(grades: [i64]) {
    var counts = countLetterGrades(grades);
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Grade Distribution:");
    Viper.Terminal.Say("  A: " + counts[0]);
    Viper.Terminal.Say("  B: " + counts[1]);
    Viper.Terminal.Say("  C: " + counts[2]);
    Viper.Terminal.Say("  D: " + counts[3]);
    Viper.Terminal.Say("  F: " + counts[4]);
}

// Print the complete grade report
func printReport(grades: [i64]) {
    Viper.Terminal.Say("");
    printSeparator();
    Viper.Terminal.Say("       GRADE REPORT            ");
    printSeparator();
    Viper.Terminal.Say("");
    printStatistics(grades);
    printDistribution(grades);
    Viper.Terminal.Say("");
    printSeparator();
}

// ============================================
// Main Program
// ============================================

func start() {
    var grades = readAllGrades();

    if grades.length == 0 {
        Viper.Terminal.Say("No grades entered.");
        return;
    }

    printReport(grades);
}
```

Notice how `start()` is now just three lines that tell the whole story: read grades, check if we have any, print the report. All the details are hidden in well-named functions.

Each function does one thing. They're short. They have clear names. They build on each other. This is professional-quality code organization.

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

BASIC distinguishes between `FUNCTION` (returns a value) and `SUB` (no return value, short for "subroutine"). The return value is assigned to the function name, which feels strange to modern programmers but was common in early languages.

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

Pascal uses `function` (returns a value) and `procedure` (no return value). Like BASIC, the return value is assigned to the function name. Pascal was designed for teaching programming, and its clear distinction between functions and procedures helped students understand the difference.

---

## Common Mistakes

**Forgetting to return:**
```rust
func add(a: i64, b: i64) -> i64 {
    var sum = a + b;
    // Oops! Forgot 'return sum'
}  // Compiler error: function must return a value
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
    return forever(n);  // Never stops! No base case, n never changes
}

func badFactorial(n: i64) -> i64 {
    return n * badFactorial(n - 1);  // No base case! Runs forever (then crashes)
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

**Expecting parameters to modify original variables:**
```rust
func double(x: i64) {
    x = x * 2;  // Only modifies local copy
}

func start() {
    var num = 5;
    double(num);
    Viper.Terminal.Say(num);  // Still 5! The function modified its copy
}
```

**Creating functions that do too much:**
```rust
// Bad: One function doing everything
func doEverything(numbers: [i64]) {
    // 50 lines of reading input
    // 30 lines of validation
    // 40 lines of calculation
    // 25 lines of formatting
    // 20 lines of output
}  // Hard to test, hard to understand, hard to modify

// Good: Small, focused functions
func readNumbers() -> [i64] { ... }
func validateNumbers(numbers: [i64]) -> bool { ... }
func calculateStatistics(numbers: [i64]) -> Statistics { ... }
func formatReport(stats: Statistics) -> string { ... }
func printReport(report: string) { ... }
```

---

## Summary

Functions are the fundamental tool for organizing code. They transform unmanageable complexity into composed simplicity. Here's what we learned:

- **Functions are named, reusable blocks of code** --- define once, use anywhere
- **Parameters** are variables that receive values; **arguments** are the values you pass
- **Return values** let functions send information back to callers
- **Local variables** exist only within their function (scope)
- **The call stack** tracks active functions and enables functions to call functions
- **Function overloading** lets you have multiple functions with the same name but different parameters
- **Recursion** is when a function calls itself --- powerful but requires a base case
- **Good functions** do one thing, have clear names, are short, and minimize side effects

The ability to break a complex problem into simple functions is one of the most important skills in programming. It takes practice. When you find yourself copying code, ask: "Should this be a function?" When a function gets long, ask: "What parts could I extract?" When code is hard to understand, ask: "Would better function names help?"

---

## Exercises

**Exercise 7.1**: Write a function `double(n: i64) -> i64` that returns n x 2. Test it with several values.

**Exercise 7.2**: Write a function `isEven(n: i64) -> bool` that returns true if n is even. Then write `isOdd` in terms of `isEven`.

**Exercise 7.3**: Write a function `max3(a: i64, b: i64, c: i64) -> i64` that returns the largest of three numbers. Try doing it by calling a `max2` function that handles two numbers.

**Exercise 7.4**: Write a function `countVowels(text: string) -> i64` that returns how many vowels (a, e, i, o, u) are in the text. Consider both uppercase and lowercase.

**Exercise 7.5**: Write a function `isPrime(n: i64) -> bool` that returns true if n is prime. Use it to print all primes from 1 to 100.

**Exercise 7.6**: Write overloaded `print` functions that handle different types: `print(n: i64)`, `print(s: string)`, `print(b: bool)`. Each should print with an appropriate label.

**Exercise 7.7** (Challenge): Write a recursive function `power(base: i64, exp: i64) -> i64` that calculates base^exp. For example, `power(2, 3)` should return 8. What's the base case?

**Exercise 7.8** (Challenge): Write a recursive function `countDigits(n: i64) -> i64` that returns how many digits are in a number. For example, `countDigits(1234)` should return 4.

**Exercise 7.9** (Challenge): Write a recursive function to reverse a string. Hint: the reverse of "hello" is (reverse of "ello") + "h".

**Exercise 7.10** (Challenge): Take a messy program you wrote earlier in this book and refactor it using functions. Can you make `start()` fit in 10 lines or less?

---

*We've finished Part I! You now understand the foundations: values, variables, decisions, loops, arrays, and functions. These concepts appear in every programming language. More importantly, you've learned to think about code organization --- how to break complex problems into manageable pieces.*

*Part II builds on this foundation with more sophisticated techniques: working with text, files, errors, and organizing larger programs. But the skills from Part I, especially this chapter on functions, will remain central to everything you do.*

*[Continue to Part II: Building Blocks](../part2-building-blocks/08-strings.md)*
