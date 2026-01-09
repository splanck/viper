# Chapter 10: Errors and Recovery

Programs don't always work perfectly. Files go missing. Networks fail. Users enter invalid data. Calculations overflow. Memory runs out.

You have two choices: let your program crash, or handle errors gracefully. Real programs handle errors. They tell users what went wrong. They recover when possible. They never just silently disappear.

This chapter teaches you to expect, catch, and recover from errors.

---

## What Are Errors?

An error (or *exception*) is something unexpected that disrupts normal flow. When code encounters a situation it can't handle, it *throws* an exception. If nothing catches that exception, the program crashes.

```rust
var x = 10 / 0;  // Error: division by zero
```

This throws an exception because division by zero is mathematically undefined. Without error handling, your program stops here with an error message.

---

## The try-catch Statement

To handle errors, wrap risky code in a `try` block:

```rust
try {
    var x = 10 / 0;
    Viper.Terminal.Say("This won't print");
} catch e {
    Viper.Terminal.Say("Error: " + e.message);
}

Viper.Terminal.Say("Program continues...");
```

Output:
```
Error: Division by zero
Program continues...
```

The `try` block attempts to run the code. If an error occurs, execution jumps immediately to the `catch` block. The variable `e` contains information about the error. After the catch block, the program continues normally.

Without the try-catch, the program would crash at the division. With it, we handle the error and keep going.

---

## What Can Go Wrong

Many things can throw exceptions:

**Division by zero:**
```rust
var result = 10 / 0;
```

**Array out of bounds:**
```rust
var arr = [1, 2, 3];
var x = arr[10];  // No index 10
```

**Invalid conversion:**
```rust
var num = Viper.Parse.Int("hello");  // Not a number
```

**File not found:**
```rust
var content = Viper.File.readText("nonexistent.txt");
```

**Null reference:**
```rust
var obj = null;
obj.doSomething();  // Can't call methods on null
```

---

## Error Information

The caught exception provides useful information:

```rust
try {
    var arr = [1, 2, 3];
    Viper.Terminal.Say(arr[100]);
} catch e {
    Viper.Terminal.Say("Error type: " + e.type);
    Viper.Terminal.Say("Message: " + e.message);
}
```

Output:
```
Error type: IndexOutOfBounds
Message: Array index 100 is out of range (length: 3)
```

The `type` tells you what kind of error occurred. The `message` gives details. Use this information to provide helpful feedback to users or to decide how to recover.

---

## Multiple catch Blocks

You can handle different error types differently:

```rust
try {
    var content = Viper.File.readText(filename);
    var value = Viper.Parse.Int(content);
} catch e: FileNotFound {
    Viper.Terminal.Say("File doesn't exist: " + filename);
} catch e: ParseError {
    Viper.Terminal.Say("File doesn't contain a valid number");
} catch e {
    Viper.Terminal.Say("Unexpected error: " + e.message);
}
```

Catch blocks are checked in order. The first matching type wins. A catch without a type matches any error — put it last as a fallback.

---

## The finally Block

Sometimes you need to clean up regardless of whether an error occurred:

```rust
var file = Viper.File.openRead("data.txt");

try {
    // Use the file...
    var data = file.readAll();
    processData(data);
} catch e {
    Viper.Terminal.Say("Error processing file: " + e.message);
} finally {
    file.close();  // Always runs, error or not
}
```

The `finally` block runs no matter what:
- If the try block succeeds, finally runs after it
- If the try block throws and catch handles it, finally runs after catch
- If the try block throws and nothing catches it, finally runs before the exception propagates

Use finally to release resources: close files, disconnect from databases, unlock locks.

---

## Throwing Your Own Errors

You can throw exceptions from your own code:

```rust
func divide(a: i64, b: i64) -> i64 {
    if b == 0 {
        throw Error("Cannot divide by zero");
    }
    return a / b;
}

func start() {
    try {
        var result = divide(10, 0);
    } catch e {
        Viper.Terminal.Say("Caught: " + e.message);
    }
}
```

Use `throw` to signal that something has gone wrong. This is better than returning a special value (like -1) because:
- It can't be ignored accidentally
- It carries information about what went wrong
- It separates error handling from normal return values

---

## When to Use Exceptions

Exceptions are for *exceptional* situations — things that shouldn't happen in normal operation:
- File that should exist is missing
- Network connection fails
- User input is malformed
- Internal logic error (bug)

Exceptions are *not* for normal control flow:
```rust
// Bad: using exceptions for normal logic
try {
    var value = getItem(index);
} catch e: NotFound {
    value = defaultValue;
}

// Better: check explicitly
if hasItem(index) {
    value = getItem(index);
} else {
    value = defaultValue;
}
```

If something is expected to happen frequently, use conditions, not exceptions.

---

## Defensive Programming

The best error handling is preventing errors in the first place:

**Validate input early:**
```rust
func processAge(ageStr: string) {
    // Validate before using
    var age = Viper.Parse.Int(ageStr);
    if age < 0 || age > 150 {
        throw Error("Age must be between 0 and 150");
    }
    // Now we know age is valid
    ...
}
```

**Check before accessing:**
```rust
if index >= 0 && index < arr.length {
    var value = arr[index];
}
```

**Use default values:**
```rust
func getConfig(key: string, default: string) -> string {
    if config.hasKey(key) {
        return config[key];
    }
    return default;
}
```

**Fail fast with clear messages:**
```rust
if filename.length == 0 {
    throw Error("Filename cannot be empty");
}
// Don't wait until we try to open an empty filename
```

---

## A Complete Example: Safe Calculator

Let's build a calculator that handles errors gracefully:

```rust
module SafeCalculator;

func getNumber(prompt: string) -> f64 {
    while true {
        Viper.Terminal.Print(prompt);
        var input = Viper.Terminal.ReadLine().trim();

        try {
            return Viper.Parse.Float(input);
        } catch e {
            Viper.Terminal.Say("Invalid number. Please try again.");
        }
    }
}

func calculate(a: f64, op: string, b: f64) -> f64 {
    if op == "+" {
        return a + b;
    } else if op == "-" {
        return a - b;
    } else if op == "*" {
        return a * b;
    } else if op == "/" {
        if b == 0 {
            throw Error("Division by zero");
        }
        return a / b;
    } else {
        throw Error("Unknown operator: " + op);
    }
}

func start() {
    Viper.Terminal.Say("Safe Calculator");
    Viper.Terminal.Say("===============");

    while true {
        var a = getNumber("First number (or 'q' to quit): ");

        Viper.Terminal.Print("Operator (+, -, *, /): ");
        var op = Viper.Terminal.ReadLine().trim();

        if op == "q" {
            break;
        }

        var b = getNumber("Second number: ");

        try {
            var result = calculate(a, op, b);
            Viper.Terminal.Say("Result: " + result);
        } catch e {
            Viper.Terminal.Say("Error: " + e.message);
        }

        Viper.Terminal.Say("");
    }

    Viper.Terminal.Say("Goodbye!");
}
```

This calculator:
- Keeps asking for valid input if parsing fails
- Handles division by zero with a clear message
- Reports unknown operators
- Never crashes, no matter what the user types

---

## The Three Languages

**ViperLang**
```rust
try {
    riskyOperation();
} catch e: FileNotFound {
    Viper.Terminal.Say("File not found");
} catch e {
    Viper.Terminal.Say("Error: " + e.message);
} finally {
    cleanup();
}

throw Error("Something went wrong");
```

**BASIC**
```basic
ON ERROR GOTO ErrorHandler

' Risky code here
OPEN "file.txt" FOR INPUT AS #1
' ...
GOTO Continue

ErrorHandler:
PRINT "Error: "; ERR; " - "; ERROR$
RESUME Continue

Continue:
' Continue execution
```

BASIC uses `ON ERROR GOTO` for older-style error handling.

**Pascal**
```pascal
try
    riskyOperation();
except
    on E: EFileNotFound do
        WriteLn('File not found');
    on E: Exception do
        WriteLn('Error: ', E.Message);
end;

try
    riskyOperation();
finally
    cleanup();
end;

raise Exception.Create('Something went wrong');
```

Pascal uses `try`/`except` (not catch) and `raise` (not throw).

---

## Common Mistakes

**Empty catch blocks:**
```rust
try {
    riskyOperation();
} catch e {
    // Silently ignoring errors — bad!
}

// At minimum, log the error
try {
    riskyOperation();
} catch e {
    log("Error: " + e.message);
}
```

**Catching too broadly:**
```rust
try {
    complexOperation();  // Many things could go wrong
} catch e {
    Viper.Terminal.Say("Something went wrong");  // Unhelpful!
}

// Better: let unexpected errors propagate
try {
    complexOperation();
} catch e: ExpectedError {
    Viper.Terminal.Say("Known issue: " + e.message);
}
// Unexpected errors will crash — which is appropriate for bugs
```

**Using exceptions for control flow:**
```rust
// Bad
func findItem(items: [string], target: string) -> i64 {
    for i in 0..items.length {
        if items[i] == target {
            throw Found(i);  // Abuse of exceptions
        }
    }
    throw NotFound();
}

// Good
func findItem(items: [string], target: string) -> i64 {
    for i in 0..items.length {
        if items[i] == target {
            return i;
        }
    }
    return -1;  // Conventional "not found" value
}
```

---

## Summary

- Exceptions signal errors that disrupt normal flow
- `try`/`catch` lets you handle errors gracefully
- `finally` runs cleanup code no matter what
- `throw` creates an exception from your own code
- Catch specific types when you can
- Use exceptions for exceptional situations, not normal control flow
- Validate input early to prevent errors
- Never silently ignore errors
- Provide helpful error messages to users

---

## Exercises

**Exercise 10.1**: Write a program that asks for two numbers and divides them, handling division by zero with a friendly message.

**Exercise 10.2**: Write a function that reads a number from a file, with error handling for missing files and invalid content.

**Exercise 10.3**: Modify the Note Keeper from Chapter 9 to handle file errors gracefully.

**Exercise 10.4**: Write a function `safeGet(arr, index, default)` that returns `arr[index]` if valid, or `default` if the index is out of bounds.

**Exercise 10.5**: Write a program that parses a date string like "2024-03-15" into year, month, and day variables, with validation and error handling.

**Exercise 10.6** (Challenge): Create a configuration file parser that reads key=value pairs from a file, handling missing files, invalid lines, and duplicate keys gracefully.

---

*We can now handle the unexpected. But our programs are still limited to built-in types. Next, we learn to create our own data structures.*

*[Continue to Chapter 11: Structures →](11-structures.md)*
