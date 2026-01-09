# Chapter 4: Making Decisions

Until now, our programs have been like trains on a track — they go forward, line by line, never deviating. But real programs need to react to situations. Should we show an error message? Does the player have enough coins? Is the password correct?

This chapter teaches your programs to think: to examine conditions and choose what to do.

---

## The Fork in the Road

Imagine giving someone directions: "Go straight until the fountain. If the gate is open, go through it; otherwise, take the path around."

That's exactly what conditional statements do in programming. They check a condition (is the gate open?) and do different things based on the answer.

```rust
var gateOpen = true;

if gateOpen {
    Viper.Terminal.Say("Going through the gate");
} else {
    Viper.Terminal.Say("Taking the path around");
}
```

The `if` keyword introduces a condition. If it's true, the code inside the first `{ }` block runs. If it's false, the code inside the `else` block runs.

This is the fundamental pattern of decision-making in programs.

---

## The if Statement

The simplest form has no `else`:

```rust
var temperature = 35;

if temperature > 30 {
    Viper.Terminal.Say("It's hot today!");
}

Viper.Terminal.Say("Have a nice day.");
```

If temperature is greater than 30, we print the "hot" message. Either way, we print "Have a nice day." — that line is outside the `if`, so it always runs.

**The structure:**
```
if condition {
    // code that runs when condition is true
}
```

The condition must be something that evaluates to `true` or `false` — a boolean expression. Usually this involves comparisons:

```rust
if age >= 18 { ... }           // is age at least 18?
if name == "Alice" { ... }     // is name exactly "Alice"?
if score != 0 { ... }          // is score something other than 0?
if password == secret { ... }  // do passwords match?
```

---

## if-else: Two Paths

When you need to do one thing or another (but not both), use `else`:

```rust
var hour = 14;

if hour < 12 {
    Viper.Terminal.Say("Good morning!");
} else {
    Viper.Terminal.Say("Good afternoon!");
}
```

Exactly one of these messages will print. If `hour < 12` is true, we say "Good morning!" and skip the `else` block. If it's false, we skip the `if` block and run the `else`.

Think of it as a fork: you must take one path or the other, never both, never neither.

---

## if-else if-else: Multiple Paths

Sometimes there are more than two possibilities:

```rust
var hour = 20;

if hour < 12 {
    Viper.Terminal.Say("Good morning!");
} else if hour < 17 {
    Viper.Terminal.Say("Good afternoon!");
} else if hour < 21 {
    Viper.Terminal.Say("Good evening!");
} else {
    Viper.Terminal.Say("Good night!");
}
```

The computer checks each condition in order:
1. Is hour < 12? No (it's 20), move on.
2. Is hour < 17? No, move on.
3. Is hour < 21? Yes! Print "Good evening!" and stop checking.

The final `else` catches anything that didn't match earlier conditions. It's optional — you can have a chain of `if-else if` without a final `else`.

**Important:** Once a condition matches, the rest are skipped. In the example above, `20 < 21` is true, so we never even check if we need the final "Good night!" message.

---

## Conditions and Comparisons

A condition is any expression that produces a boolean (`true` or `false`). The comparison operators from Chapter 3 are your main tools:

```rust
x == y    // equal
x != y    // not equal
x < y     // less than
x > y     // greater than
x <= y    // less than or equal
x >= y    // greater than or equal
```

You can compare numbers:
```rust
if score >= 100 {
    Viper.Terminal.Say("High score!");
}
```

You can compare strings:
```rust
if answer == "yes" {
    Viper.Terminal.Say("Great!");
}
```

String comparisons are case-sensitive: `"Yes"` is not equal to `"yes"`.

---

## Combining Conditions

Sometimes you need to check multiple things. The logical operators combine booleans:

**AND (`&&`)**: Both must be true
```rust
if age >= 18 && hasTicket {
    Viper.Terminal.Say("Welcome to the show!");
}
```
You can enter only if you're at least 18 AND have a ticket.

**OR (`||`)**: At least one must be true
```rust
if day == "Saturday" || day == "Sunday" {
    Viper.Terminal.Say("It's the weekend!");
}
```
It's the weekend if it's Saturday OR Sunday (or both, but that's impossible with days).

**NOT (`!`)**: Reverses the boolean
```rust
if !gameOver {
    Viper.Terminal.Say("Keep playing!");
}
```
If `gameOver` is `false`, then `!gameOver` is `true`.

You can build complex conditions:
```rust
if (age >= 18 && age <= 65) || hasSpecialPass {
    Viper.Terminal.Say("You qualify for the program.");
}
```

Use parentheses to make your intentions clear. Even when not strictly necessary, they help humans read the code.

---

## Nesting: Decisions Within Decisions

You can put `if` statements inside other `if` statements:

```rust
var hasAccount = true;
var password = "secret123";
var inputPassword = "secret123";

if hasAccount {
    if password == inputPassword {
        Viper.Terminal.Say("Login successful!");
    } else {
        Viper.Terminal.Say("Wrong password.");
    }
} else {
    Viper.Terminal.Say("Please create an account first.");
}
```

This checks: Do you have an account? If yes, is the password correct? Each decision leads to more specific outcomes.

Nesting works, but deep nesting gets hard to read. Often you can flatten it:

```rust
if !hasAccount {
    Viper.Terminal.Say("Please create an account first.");
} else if password != inputPassword {
    Viper.Terminal.Say("Wrong password.");
} else {
    Viper.Terminal.Say("Login successful!");
}
```

This handles the same logic but reads more linearly. It's called "guard clauses" — checking for problems first and handling the happy path at the end.

---

## A Practical Example

Let's build a simple grading program:

```rust
module Grader;

func start() {
    Viper.Terminal.Print("Enter the score (0-100): ");
    var score = Viper.Parse.Int(Viper.Terminal.ReadLine());

    var grade = "";

    if score >= 90 {
        grade = "A";
    } else if score >= 80 {
        grade = "B";
    } else if score >= 70 {
        grade = "C";
    } else if score >= 60 {
        grade = "D";
    } else {
        grade = "F";
    }

    Viper.Terminal.Say("Your grade is: " + grade);

    if grade == "A" {
        Viper.Terminal.Say("Excellent work!");
    } else if grade == "F" {
        Viper.Terminal.Say("Please see the teacher for help.");
    }
}
```

Sample run:
```
Enter the score (0-100): 85
Your grade is: B
```

Notice how the conditions are ordered from highest to lowest. A score of 85 isn't >= 90, but it is >= 80, so we get "B" and stop checking.

---

## The match Statement

When you're comparing one value against many possibilities, `match` can be cleaner than many `if-else if` chains:

```rust
var day = 3;

match day {
    1 => Viper.Terminal.Say("Monday"),
    2 => Viper.Terminal.Say("Tuesday"),
    3 => Viper.Terminal.Say("Wednesday"),
    4 => Viper.Terminal.Say("Thursday"),
    5 => Viper.Terminal.Say("Friday"),
    6 => Viper.Terminal.Say("Saturday"),
    7 => Viper.Terminal.Say("Sunday"),
    _ => Viper.Terminal.Say("Invalid day")
}
```

The `_` is a wildcard that matches anything not already matched. It's like the `else` in an `if` chain.

Match is especially useful when you have many specific values to check. We'll see more powerful uses of `match` later in the book.

---

## The Three Languages

**ViperLang**
```rust
var score = 85;

if score >= 60 {
    Viper.Terminal.Say("You passed!");
} else {
    Viper.Terminal.Say("Try again.");
}

match score / 10 {
    10 | 9 => Viper.Terminal.Say("A"),
    8 => Viper.Terminal.Say("B"),
    7 => Viper.Terminal.Say("C"),
    6 => Viper.Terminal.Say("D"),
    _ => Viper.Terminal.Say("F")
}
```

**BASIC**
```basic
DIM score AS INTEGER
score = 85

IF score >= 60 THEN
    PRINT "You passed!"
ELSE
    PRINT "Try again."
END IF

SELECT CASE score \ 10
    CASE 10, 9
        PRINT "A"
    CASE 8
        PRINT "B"
    CASE 7
        PRINT "C"
    CASE 6
        PRINT "D"
    CASE ELSE
        PRINT "F"
END SELECT
```

BASIC uses `THEN` and `END IF`. The `SELECT CASE` is its version of `match`.

**Pascal**
```pascal
var score: Integer;
begin
    score := 85;

    if score >= 60 then
        WriteLn('You passed!')
    else
        WriteLn('Try again.');

    case score div 10 of
        10, 9: WriteLn('A');
        8: WriteLn('B');
        7: WriteLn('C');
        6: WriteLn('D');
        else WriteLn('F');
    end;
end.
```

Pascal uses `then` instead of braces, and `case` for pattern matching.

All three express the same logic. The keywords differ, but the structure — condition, then branch, else branch — is universal.

---

## Truthiness and Falsiness

In ViperLang, conditions must be actual booleans. This is different from some languages where `0` counts as false and other numbers count as true.

```rust
var count = 5;

if count {           // Error in ViperLang: count is a number, not a boolean
    ...
}

if count != 0 {      // Correct: explicitly check if count is not zero
    ...
}

if count > 0 {       // Also correct: check if count is positive
    ...
}
```

This strictness helps prevent bugs. It forces you to be explicit about what you mean.

---

## Common Mistakes

**Forgetting braces:**
```rust
if score > 100
    Viper.Terminal.Say("High score!");  // Error: missing braces
```
ViperLang requires `{ }` around conditional blocks.

**Using = instead of ==:**
```rust
if score = 100 {   // Wrong: this assigns 100 to score
    ...
}
if score == 100 {  // Right: this compares score to 100
    ...
}
```

**Checking impossible conditions:**
```rust
if score >= 90 {
    grade = "A";
} else if score >= 80 {
    grade = "B";
} else if score >= 90 {   // This will never be true! We already checked >= 90
    grade = "A+";
}
```

**Overlapping conditions:**
```rust
// Both could be true for score = 85
if score >= 80 {
    Viper.Terminal.Say("B or better");
}
if score >= 70 {
    Viper.Terminal.Say("C or better");
}
```

If you want only one to run, use `else if`:
```rust
if score >= 80 {
    Viper.Terminal.Say("B or better");
} else if score >= 70 {
    Viper.Terminal.Say("C or better");
}
```

---

## Putting It Together

Here's a number guessing game that uses everything we've learned:

```rust
module GuessGame;

func start() {
    final SECRET = 7;

    Viper.Terminal.Say("I'm thinking of a number between 1 and 10.");
    Viper.Terminal.Print("Your guess: ");

    var guess = Viper.Parse.Int(Viper.Terminal.ReadLine());

    if guess == SECRET {
        Viper.Terminal.Say("Correct! You win!");
    } else if guess < SECRET {
        Viper.Terminal.Say("Too low! The answer was " + SECRET);
    } else {
        Viper.Terminal.Say("Too high! The answer was " + SECRET);
    }
}
```

This isn't a great game yet — the number is always 7! In Chapter 5, we'll add loops so the player can guess multiple times. In Chapter 13, we'll learn to generate random numbers for real games.

---

## Summary

- `if` checks a condition and runs code only when it's true
- `else` provides an alternative when the condition is false
- `else if` chains let you check multiple conditions
- Comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`) produce booleans
- Logical operators (`&&`, `||`, `!`) combine booleans
- `match` compares one value against many possibilities
- Conditions are checked in order; once one matches, the rest are skipped
- Use clear, explicit conditions — don't rely on implicit conversions

---

## Exercises

**Exercise 4.1**: Write a program that asks for a number and prints whether it's positive, negative, or zero.

**Exercise 4.2**: Write a program that asks for the user's age and prints whether they can vote (18+), will be able to vote soon (16-17), or are too young (under 16).

**Exercise 4.3**: Write a program that asks for two numbers and prints which one is larger, or if they're equal.

**Exercise 4.4**: Write a program that asks for a year and prints whether it's a leap year. A year is a leap year if:
- It's divisible by 4, AND
- Either it's not divisible by 100, OR it's divisible by 400

**Exercise 4.5** (Challenge): Write a simple calculator that asks for two numbers and an operation (+, -, *, /) and prints the result. Handle division by zero with an error message.

---

*We can now make decisions. But what if we need to do something many times — print 100 lines, check every character in a name, run a game loop 60 times per second? Next, we learn about repetition.*

*[Continue to Chapter 5: Repetition →](05-repetition.md)*
