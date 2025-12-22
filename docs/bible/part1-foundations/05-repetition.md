# Chapter 5: Repetition

Imagine you need to print the numbers 1 to 1000. With what we know so far, you'd write a thousand `Say` statements. That's not just tedious — it's the wrong approach entirely.

Computers excel at repetition. They can do the same thing millions of times without getting bored, tired, or making mistakes. This chapter teaches you to harness that power.

---

## The while Loop

The simplest loop repeats as long as a condition is true:

```viper
let count = 1;

while count <= 5 {
    Viper.Terminal.Say(count);
    count = count + 1;
}
```

Output:
```
1
2
3
4
5
```

Let's trace through this step by step:

1. `count` starts at 1
2. Is `count <= 5`? Yes (1 <= 5). Run the body.
3. Print 1. Set `count` to 2.
4. Is `count <= 5`? Yes (2 <= 5). Run the body.
5. Print 2. Set `count` to 3.
6. ... (same pattern for 3, 4, 5)
7. Print 5. Set `count` to 6.
8. Is `count <= 5`? No (6 <= 5 is false). Stop looping.

After the loop ends, execution continues with whatever comes next.

**The structure:**
```
while condition {
    // body: code to repeat
}
```

The condition is checked *before* each iteration. If it's false from the start, the body never runs at all:

```viper
let x = 10;
while x < 5 {
    Viper.Terminal.Say("This never prints");
}
```

---

## Infinite Loops

What if the condition never becomes false?

```viper
while true {
    Viper.Terminal.Say("Forever!");
}
```

This runs forever — an *infinite loop*. Sometimes that's intentional (game loops run until the player quits). Usually it's a bug.

The classic mistake is forgetting to update the variable you're checking:

```viper
let count = 1;
while count <= 5 {
    Viper.Terminal.Say(count);
    // Oops! Forgot to increment count
    // count is always 1, always <= 5, loops forever
}
```

If your program seems frozen, an infinite loop is often the culprit. Press Ctrl+C to stop it.

---

## The for Loop

When you know how many times to repeat, `for` is cleaner than `while`:

```viper
for i in 1..6 {
    Viper.Terminal.Say(i);
}
```

Output:
```
1
2
3
4
5
```

The `1..6` is a *range* — it includes 1 up to but not including 6. (This might seem odd, but it's a common convention that makes certain math easier.)

If you want to include the end value, use `..=`:

```viper
for i in 1..=5 {
    Viper.Terminal.Say(i);
}
```

This also prints 1 through 5.

**The structure:**
```
for variable in range {
    // body
}
```

The variable (`i` in the examples) is automatically created, updated, and scoped to the loop. You don't need `let` or manual incrementing.

**Counting down:**
```viper
for i in (10..0).rev() {
    Viper.Terminal.Say(i);
}
Viper.Terminal.Say("Liftoff!");
```

**Stepping by 2s:**
```viper
for i in (0..10).step(2) {
    Viper.Terminal.Say(i);  // 0, 2, 4, 6, 8
}
```

---

## Choosing Between while and for

Use `for` when you know the number of iterations ahead of time:
- "Do this 10 times"
- "Process each item from 1 to 100"
- "Count down from 5 to 1"

Use `while` when you don't know how many iterations you'll need:
- "Keep asking until they enter a valid password"
- "Read lines until end of file"
- "Run until the game ends"

```viper
// for: we know it's 10 iterations
for i in 1..=10 {
    Viper.Terminal.Say("Iteration " + i);
}

// while: we don't know how many tries
let password = "";
while password != "secret" {
    Viper.Terminal.Print("Password: ");
    password = Viper.Terminal.ReadLine();
}
Viper.Terminal.Say("Access granted!");
```

---

## Breaking Out Early

Sometimes you want to stop a loop before its condition becomes false. The `break` statement does this:

```viper
for i in 1..100 {
    if i * i > 50 {
        Viper.Terminal.Say("Stopping at " + i);
        break;
    }
    Viper.Terminal.Say(i + " squared is " + (i * i));
}
```

Output:
```
1 squared is 1
2 squared is 4
3 squared is 9
4 squared is 16
5 squared is 25
6 squared is 36
7 squared is 49
Stopping at 8
```

When `break` runs, the loop immediately exits. Execution continues after the loop's closing brace.

This is useful for searching:

```viper
let numbers = [4, 8, 15, 16, 23, 42];
let target = 16;
let found = false;

for i in 0..numbers.length {
    if numbers[i] == target {
        Viper.Terminal.Say("Found at position " + i);
        found = true;
        break;  // No need to keep searching
    }
}

if !found {
    Viper.Terminal.Say("Not found");
}
```

---

## Skipping Iterations

The `continue` statement skips the rest of the current iteration and moves to the next:

```viper
for i in 1..=10 {
    if i % 2 == 0 {
        continue;  // Skip even numbers
    }
    Viper.Terminal.Say(i);
}
```

Output:
```
1
3
5
7
9
```

When `i` is even, `continue` jumps back to the `for` line, increments `i`, and starts the next iteration. The `Say` never runs for even numbers.

This is useful for filtering:

```viper
// Print only positive numbers
for i in -5..6 {
    if i <= 0 {
        continue;
    }
    Viper.Terminal.Say(i);
}
```

---

## Nested Loops

Loops can contain other loops. This is common for working with grids, tables, and combinations:

```viper
for row in 1..=3 {
    for col in 1..=4 {
        Viper.Terminal.Print("*");
    }
    Viper.Terminal.Say("");  // New line after each row
}
```

Output:
```
****
****
****
```

The outer loop runs 3 times (once per row). Each time, the inner loop runs 4 times (printing 4 stars). That's 12 stars total, arranged in a 3×4 grid.

**Multiplication table:**
```viper
for row in 1..=5 {
    for col in 1..=5 {
        let product = row * col;
        Viper.Terminal.Print(product + "\t");  // \t is a tab
    }
    Viper.Terminal.Say("");
}
```

Output:
```
1	2	3	4	5
2	4	6	8	10
3	6	9	12	15
4	8	12	16	20
5	10	15	20	25
```

With nested loops, the total iterations multiply. A 100×100 grid means 10,000 iterations. A 1000×1000 grid means a million. Computers handle this easily, but be mindful of what you're asking for.

---

## Common Loop Patterns

### Counting
```viper
let count = 0;
for i in 1..=100 {
    if someCondition(i) {
        count = count + 1;
    }
}
Viper.Terminal.Say("Found " + count + " matches");
```

### Summing
```viper
let total = 0;
for i in 1..=100 {
    total = total + i;
}
Viper.Terminal.Say("Sum is " + total);  // 5050
```

### Finding maximum
```viper
let values = [23, 7, 42, 15, 8];
let max = values[0];

for i in 1..values.length {
    if values[i] > max {
        max = values[i];
    }
}
Viper.Terminal.Say("Maximum is " + max);  // 42
```

### Building strings
```viper
let result = "";
for i in 1..=5 {
    result = result + i + " ";
}
Viper.Terminal.Say(result);  // "1 2 3 4 5 "
```

### Validating input
```viper
let valid = false;
let age = 0;

while !valid {
    Viper.Terminal.Print("Enter your age (0-120): ");
    age = Viper.Parse.Int(Viper.Terminal.ReadLine());

    if age >= 0 && age <= 120 {
        valid = true;
    } else {
        Viper.Terminal.Say("Please enter a valid age.");
    }
}
```

---

## The Three Languages

**ViperLang**
```viper
// while loop
let i = 0;
while i < 5 {
    Viper.Terminal.Say(i);
    i += 1;
}

// for loop
for i in 0..5 {
    Viper.Terminal.Say(i);
}
```

**BASIC**
```basic
' WHILE loop
DIM i AS INTEGER
i = 0
WHILE i < 5
    PRINT i
    i = i + 1
WEND

' FOR loop
FOR i = 0 TO 4
    PRINT i
NEXT i

' FOR with STEP
FOR i = 10 TO 0 STEP -2
    PRINT i
NEXT i
```

BASIC's `FOR` includes the end value (unlike ViperLang's `..`). `WEND` ends the `WHILE` block.

**Pascal**
```pascal
var i: Integer;
begin
    { while loop }
    i := 0;
    while i < 5 do
    begin
        WriteLn(i);
        i := i + 1;
    end;

    { for loop }
    for i := 0 to 4 do
        WriteLn(i);

    { for counting down }
    for i := 10 downto 1 do
        WriteLn(i);
end.
```

Pascal uses `do` and `begin`/`end` for blocks. `downto` counts backward.

---

## A Complete Example: Guessing Game

Let's improve our guessing game from Chapter 4 with loops:

```viper
module GuessGame;

func start() {
    const SECRET = 7;
    const MAX_TRIES = 3;
    let tries = 0;
    let won = false;

    Viper.Terminal.Say("I'm thinking of a number between 1 and 10.");
    Viper.Terminal.Say("You have " + MAX_TRIES + " tries.");
    Viper.Terminal.Say("");

    while tries < MAX_TRIES && !won {
        tries = tries + 1;
        Viper.Terminal.Print("Guess #" + tries + ": ");
        let guess = Viper.Parse.Int(Viper.Terminal.ReadLine());

        if guess == SECRET {
            Viper.Terminal.Say("Correct! You win!");
            won = true;
        } else if guess < SECRET {
            Viper.Terminal.Say("Too low!");
        } else {
            Viper.Terminal.Say("Too high!");
        }
    }

    if !won {
        Viper.Terminal.Say("Game over! The number was " + SECRET);
    }
}
```

Sample run:
```
I'm thinking of a number between 1 and 10.
You have 3 tries.

Guess #1: 5
Too low!
Guess #2: 8
Too high!
Guess #3: 7
Correct! You win!
```

The loop continues while we have tries left AND haven't won. Either running out of tries or winning will end the loop.

---

## Common Mistakes

**Off-by-one errors:**
```viper
// Intended to print 1-10, but prints 1-9
for i in 1..10 {
    Viper.Terminal.Say(i);
}

// Correct: use ..= or use ..11
for i in 1..=10 {
    Viper.Terminal.Say(i);
}
```

**Modifying loop variable:**
```viper
// Don't do this — confusing behavior
for i in 1..10 {
    Viper.Terminal.Say(i);
    i = i + 1;  // Doesn't work as expected
}
```

**Infinite loops:**
```viper
// Missing increment
let i = 0;
while i < 10 {
    Viper.Terminal.Say(i);
    // Forgot: i = i + 1
}
```

**Wrong condition direction:**
```viper
// Never runs: 10 is not less than 1
for i in 10..1 {
    Viper.Terminal.Say(i);
}

// Correct: use reverse
for i in (1..11).rev() {
    Viper.Terminal.Say(i);
}
```

---

## Summary

- `while` loops repeat as long as a condition is true
- `for` loops iterate over a range of values
- `..` creates a range excluding the end; `..=` includes it
- `break` exits a loop immediately
- `continue` skips to the next iteration
- Loops can be nested for multi-dimensional patterns
- Use `for` when you know the count; `while` when you don't
- Watch out for infinite loops — always ensure the condition will eventually become false

---

## Exercises

**Exercise 5.1**: Write a program that prints the numbers 1 to 20 using a `while` loop, then again using a `for` loop.

**Exercise 5.2**: Write a program that prints all multiples of 3 between 1 and 50.

**Exercise 5.3**: Write a program that asks for a number N and prints the first N numbers of the Fibonacci sequence (1, 1, 2, 3, 5, 8, 13, ...). Each number is the sum of the two before it.

**Exercise 5.4**: Write a program that asks for a positive integer and prints whether it's prime. A prime number is only divisible by 1 and itself. (Hint: check if any number from 2 to n-1 divides it evenly.)

**Exercise 5.5**: Print this pattern:
```
*
**
***
****
*****
```

**Exercise 5.6** (Challenge): Print this pattern:
```
    *
   ***
  *****
 *******
*********
```
(Hint: you need to print spaces before the stars on each line.)

---

*We can now repeat actions. But we've been working with individual values — one number, one string. What if we have a hundred numbers? A thousand names? Next, we learn about collections.*

*[Continue to Chapter 6: Collections →](06-collections.md)*
