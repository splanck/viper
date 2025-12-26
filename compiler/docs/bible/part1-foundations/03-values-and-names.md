# Chapter 3: Values and Names

In the last chapter, we displayed text on the screen. But our program always showed the same message. Real programs work with data that changes — numbers, text, true/false answers. They remember things. They calculate.

This chapter introduces the atoms of programming: values and the names we give them.

---

## Values: The Stuff Programs Work With

Everything a program manipulates is a *value*. When you type `42`, that's a value. When you type `"Hello"`, that's a value. When you type `true`, that's a value.

Values come in different *types* — different kinds of things:

**Numbers** are for math, counting, measuring. `42`, `3.14159`, `-7`, `1000000`.

**Text** (called *strings* because they're strings of characters) is for words, sentences, messages. `"Hello"`, `"Enter your name:"`, `"Game Over"`.

**Booleans** are for yes/no, true/false, on/off questions. Just two possible values: `true` and `false`.

These three types — numbers, strings, booleans — are enough to build surprisingly complex programs. Later we'll learn about collections and custom types, but these are the foundation.

---

## Names: Remembering Values

Here's a problem: you want to ask the user for their name, then greet them. You need to *remember* what they typed so you can use it later.

This is what *variables* are for. A variable is a name attached to a value. You can think of it as a labeled box: the label is the name, the contents are the value.

```viper
let age = 25;
```

This creates a variable named `age` and puts the value `25` inside it. Now whenever we write `age`, Viper knows we mean `25`.

Let's see it in action:

```viper
module Variables;

func start() {
    let name = "Alice";
    let age = 30;

    Viper.Terminal.Say("Name: " + name);
    Viper.Terminal.Say("Age: " + age);
}
```

Output:
```
Name: Alice
Age: 30
```

We created two variables: `name` holding the text `"Alice"`, and `age` holding the number `30`. Then we used them in our `Say` calls.

Notice the `+` between `"Name: "` and `name`. When you use `+` with strings, it joins them together. `"Name: " + "Alice"` becomes `"Name: Alice"`. This is called *concatenation*.

---

## Choosing Good Names

You can name variables almost anything, but good names make code readable:

```viper
// Hard to understand
let x = 100;
let y = 5;
let z = x * y;

// Easy to understand
let price = 100;
let quantity = 5;
let total = price * quantity;
```

Both do the same thing, but the second version tells a story. Six months from now, you'll thank yourself for using clear names.

**Rules for names:**
- Must start with a letter or underscore
- Can contain letters, numbers, underscores
- Can't be a keyword (like `let`, `func`, `if`)
- Case matters: `age`, `Age`, and `AGE` are three different names

**Conventions:**
- Use `camelCase` for variables: `firstName`, `totalScore`, `isGameOver`
- Use descriptive names: `count` not `c`, `userName` not `un`
- Boolean variables often start with `is`, `has`, `can`: `isReady`, `hasWon`, `canJump`

---

## Numbers in Detail

Viper has two kinds of numbers:

**Integers** are whole numbers: `1`, `42`, `-7`, `1000000`. No decimal point. Use these for counting, indexing, and anything that can't be fractional.

**Floating-point numbers** (or "floats") have decimal points: `3.14`, `0.5`, `-273.15`. Use these for measurements, calculations, anything that might be fractional.

```viper
let count = 10;        // integer
let price = 19.99;     // float
let temperature = -5;  // integer (negative)
let ratio = 0.75;      // float
```

You can do math with numbers:

```viper
let a = 10;
let b = 3;

Viper.Terminal.Say(a + b);   // 13 (addition)
Viper.Terminal.Say(a - b);   // 7  (subtraction)
Viper.Terminal.Say(a * b);   // 30 (multiplication)
Viper.Terminal.Say(a / b);   // 3  (division - integer result)
Viper.Terminal.Say(a % b);   // 1  (remainder/modulo)
```

The `%` operator gives the remainder after division. `10 % 3` is `1` because 10 divided by 3 is 3 with remainder 1. This is surprisingly useful — you can check if a number is even (`n % 2 == 0`) or wrap around within a range.

**Watch out for integer division:** When you divide two integers, you get an integer result. `10 / 3` is `3`, not `3.333...`. If you need the fractional part, use floats:

```viper
let result = 10.0 / 3.0;  // 3.333...
```

---

## Strings in Detail

Strings are text enclosed in double quotes:

```viper
let greeting = "Hello, World!";
let empty = "";                    // empty string (no characters)
let sentence = "She said \"Hi\"";  // use \" for quotes inside strings
```

The backslash (`\`) is an *escape character*. It gives special meaning to the next character:
- `\"` — a literal quote mark
- `\\` — a literal backslash
- `\n` — a newline (start a new line)
- `\t` — a tab

```viper
Viper.Terminal.Say("Line one\nLine two");
```

Output:
```
Line one
Line two
```

You can join strings with `+`:

```viper
let first = "Hello";
let second = "World";
let message = first + ", " + second + "!";  // "Hello, World!"
```

Strings have a length — the number of characters they contain. We'll learn how to work with individual characters in Chapter 8.

---

## Booleans: True and False

Boolean values represent truth. There are exactly two: `true` and `false`.

```viper
let isGameOver = false;
let hasPermission = true;
```

Booleans are the result of *comparisons*:

```viper
let age = 25;
let isAdult = age >= 18;  // true, because 25 >= 18
```

Here are the comparison operators:

| Operator | Meaning | Example | Result |
|----------|---------|---------|--------|
| `==` | equals | `5 == 5` | `true` |
| `!=` | not equals | `5 != 3` | `true` |
| `<` | less than | `3 < 5` | `true` |
| `>` | greater than | `5 > 3` | `true` |
| `<=` | less than or equal | `5 <= 5` | `true` |
| `>=` | greater than or equal | `5 >= 3` | `true` |

**Important:** Use `==` for comparison, not `=`. A single `=` is for assignment (putting a value in a variable). Double `==` is for checking equality. This is a common source of bugs.

You can combine booleans with *logical operators*:

| Operator | Meaning | Example | Result |
|----------|---------|---------|--------|
| `&&` | and (both true) | `true && false` | `false` |
| `\|\|` | or (either true) | `true \|\| false` | `true` |
| `!` | not (opposite) | `!true` | `false` |

```viper
let age = 25;
let hasTicket = true;

let canEnter = age >= 18 && hasTicket;  // true (both conditions met)
```

We'll use booleans extensively in the next chapter when we learn about making decisions.

---

## Changing Values

Variables can change. That's why they're called "variables" — they vary.

```viper
let score = 0;
Viper.Terminal.Say(score);  // 0

score = 10;
Viper.Terminal.Say(score);  // 10

score = score + 5;
Viper.Terminal.Say(score);  // 15
```

Notice that after creating a variable with `let`, we change it using just `=` (no `let`). Using `let` again would try to create a *new* variable with the same name, which is an error.

The line `score = score + 5` might look strange. Read it as: "take the current value of score, add 5, and store the result back in score." It's not an equation; it's an instruction.

This pattern is so common that there's a shortcut:

```viper
score += 5;   // same as: score = score + 5
score -= 3;   // same as: score = score - 3
score *= 2;   // same as: score = score * 2
score /= 4;   // same as: score = score / 4
```

---

## Constants: Values That Don't Change

Sometimes you have a value that should never change — like pi, or the number of days in a week. Use `const` instead of `let`:

```viper
const PI = 3.14159;
const DAYS_IN_WEEK = 7;
```

If you try to change a constant later, Viper will give you an error. This prevents accidental bugs and makes your intentions clear.

By convention, constants are named in `UPPER_CASE` to distinguish them from variables.

---

## Getting Input from the User

So far, our variables have been set by us in the code. But programs are more interesting when users can provide input.

```viper
module Greeting;

func start() {
    Viper.Terminal.Print("What is your name? ");
    let name = Viper.Terminal.ReadLine();

    Viper.Terminal.Say("Hello, " + name + "!");
}
```

Running this:
```
What is your name? Alice
Hello, Alice!
```

`ReadLine()` waits for the user to type something and press Enter, then returns what they typed as a string.

What if you want a number? The user types text, so you need to convert it:

```viper
module Age;

func start() {
    Viper.Terminal.Print("How old are you? ");
    let ageText = Viper.Terminal.ReadLine();
    let age = Viper.Parse.Int(ageText);

    let nextYear = age + 1;
    Viper.Terminal.Say("Next year you'll be " + nextYear);
}
```

`Viper.Parse.Int()` converts a string like `"25"` into the number `25`. There's also `Viper.Parse.Float()` for decimal numbers.

---

## The Three Languages

Let's see variables in all three Viper languages:

**ViperLang**
```viper
let name = "Alice";
let age = 30;
const PI = 3.14159;

age = age + 1;
Viper.Terminal.Say(name + " is " + age);
```

**BASIC**
```basic
DIM name AS STRING
DIM age AS INTEGER
CONST PI = 3.14159

name = "Alice"
age = 30

age = age + 1
PRINT name; " is "; age
```

BASIC requires you to declare variables with `DIM` and specify their type explicitly. Some find this more readable; others find it verbose.

**Pascal**
```pascal
var
    name: string;
    age: Integer;
const
    PI = 3.14159;
begin
    name := 'Alice';
    age := 30;

    age := age + 1;
    WriteLn(name, ' is ', age);
end.
```

Pascal uses `:=` for assignment (to distinguish from `=` in comparisons). Variables are declared in a `var` section before use.

Notice what's the same across all three:
- The concept of naming values
- The ability to change variables
- Constants that don't change
- Basic arithmetic operations
- String concatenation

The syntax differs, but the ideas are identical.

---

## Common Mistakes

**Forgetting to initialize:**
```viper
let count;          // Error: what value?
let count = 0;      // Good: starts at 0
```

**Using a variable before creating it:**
```viper
Viper.Terminal.Say(score);  // Error: what's score?
let score = 100;
```

**Confusing = and ==:**
```viper
if (x = 5)   // Wrong: this assigns 5 to x
if (x == 5)  // Right: this checks if x equals 5
```

**Trying to do math with strings:**
```viper
let result = "5" + 3;  // Might not do what you expect
```

When you add a string and a number, Viper converts the number to a string and concatenates. `"5" + 3` becomes `"53"`, not `8`. If you want math, make sure both values are numbers.

**Integer division surprise:**
```viper
let half = 1 / 2;  // Result is 0, not 0.5!
let half = 1.0 / 2.0;  // Result is 0.5
```

---

## Putting It Together

Here's a small program that uses everything we've learned:

```viper
module Calculator;

func start() {
    Viper.Terminal.Say("Simple Calculator");
    Viper.Terminal.Say("================");

    Viper.Terminal.Print("Enter first number: ");
    let first = Viper.Parse.Float(Viper.Terminal.ReadLine());

    Viper.Terminal.Print("Enter second number: ");
    let second = Viper.Parse.Float(Viper.Terminal.ReadLine());

    let sum = first + second;
    let difference = first - second;
    let product = first * second;
    let quotient = first / second;

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Results:");
    Viper.Terminal.Say(first + " + " + second + " = " + sum);
    Viper.Terminal.Say(first + " - " + second + " = " + difference);
    Viper.Terminal.Say(first + " * " + second + " = " + product);
    Viper.Terminal.Say(first + " / " + second + " = " + quotient);
}
```

Running it:
```
Simple Calculator
================
Enter first number: 10
Enter second number: 3
Results:
10 + 3 = 13
10 - 3 = 7
10 * 3 = 30
10 / 3 = 3.333333
```

---

## Summary

- **Values** are the data programs work with: numbers, strings, booleans
- **Variables** are names attached to values, created with `let`
- **Constants** are unchangeable values, created with `const`
- **Integers** are whole numbers; **floats** have decimal points
- **Strings** are text in quotes; join them with `+`
- **Booleans** are `true` or `false`, often from comparisons
- Variables can change; constants cannot
- Use `ReadLine()` to get text input; `Parse.Int()` or `Parse.Float()` to convert to numbers

---

## Exercises

**Exercise 3.1**: Write a program that asks for your name and age, then prints "Hello, [name]! You are [age] years old."

**Exercise 3.2**: Write a program that asks for a temperature in Celsius and converts it to Fahrenheit. The formula is: F = C × 9/5 + 32

**Exercise 3.3**: Write a program that asks for the length and width of a rectangle, then prints its area and perimeter.

**Exercise 3.4**: Write a program that asks for three test scores and prints their average.

**Exercise 3.5** (Challenge): Write a program that asks for a number of seconds and converts it to hours, minutes, and seconds. For example, 3665 seconds is 1 hour, 1 minute, and 5 seconds. (Hint: use integer division and the modulo operator.)

---

*Now we can store and calculate with data. But our programs still run the same way every time. Next, we'll learn how to make programs that choose different paths based on conditions.*

*[Continue to Chapter 4: Making Decisions →](04-decisions.md)*
