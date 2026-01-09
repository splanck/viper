# Chapter 6: Collections

You're building a student grade tracker. You need to store grades for 30 students. With what we know, you'd create 30 variables: `grade1`, `grade2`, `grade3`, ... all the way to `grade30`. Then to find the average, you'd add them all up manually.

There must be a better way.

There is. *Collections* let you store multiple values under a single name. The most fundamental collection is the *array*.

---

## What Is an Array?

An array is a numbered list of values. Think of it as a row of mailboxes — each box has a number (its *index*), and each box can hold one value.

```rust
var scores = [85, 92, 78, 95, 88];
```

This creates an array named `scores` containing five numbers. The brackets `[ ]` define the array, and the values are separated by commas.

To access individual elements, use their index:

```rust
Viper.Terminal.Say(scores[0]);  // 85 (first element)
Viper.Terminal.Say(scores[1]);  // 92 (second element)
Viper.Terminal.Say(scores[4]);  // 88 (fifth element)
```

**Important:** Indices start at 0, not 1. This surprises beginners, but it's nearly universal in programming. The first element is at index 0, the second at index 1, and so on.

For an array of 5 elements, valid indices are 0, 1, 2, 3, and 4. Trying to access index 5 is an error — there's no sixth element.

---

## Why Zero-Based Indexing?

This convention comes from how computers think about memory. An index is really an *offset* — how far from the start to look. The first element is zero steps from the start, the second is one step, and so on.

You'll get used to it. Just remember: for an array of N elements, valid indices go from 0 to N-1.

---

## Creating Arrays

**With values:**
```rust
var names = ["Alice", "Bob", "Carol"];
var primes = [2, 3, 5, 7, 11, 13];
var flags = [true, false, true];
```

**Empty array (to fill later):**
```rust
var numbers: [i64] = [];  // Empty array of integers
```

**Array of a specific size:**
```rust
var zeros = [0; 10];  // Ten zeros: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
```

The `[value; count]` syntax creates an array with `count` copies of `value`.

---

## Array Length

Every array knows its length — how many elements it contains:

```rust
var names = ["Alice", "Bob", "Carol"];
Viper.Terminal.Say(names.length);  // 3
```

This is crucial for loops:

```rust
var scores = [85, 92, 78, 95, 88];

for i in 0..scores.length {
    Viper.Terminal.Say("Score " + i + ": " + scores[i]);
}
```

Output:
```
Score 0: 85
Score 1: 92
Score 2: 78
Score 3: 95
Score 4: 88
```

The range `0..scores.length` generates indices 0, 1, 2, 3, 4 — exactly what we need.

---

## Modifying Arrays

You can change individual elements:

```rust
var scores = [85, 92, 78, 95, 88];
scores[2] = 82;  // Change 78 to 82
Viper.Terminal.Say(scores[2]);  // 82
```

You can add elements to the end:

```rust
var names = ["Alice", "Bob"];
names.push("Carol");
Viper.Terminal.Say(names.length);  // 3
Viper.Terminal.Say(names[2]);      // Carol
```

You can remove the last element:

```rust
var numbers = [1, 2, 3, 4, 5];
var last = numbers.pop();  // Removes and returns 5
Viper.Terminal.Say(last);           // 5
Viper.Terminal.Say(numbers.length); // 4
```

---

## Iterating Over Arrays

The most common thing to do with an array is process every element. We saw the index-based approach:

```rust
for i in 0..scores.length {
    Viper.Terminal.Say(scores[i]);
}
```

But if you don't need the index, there's a cleaner way:

```rust
for score in scores {
    Viper.Terminal.Say(score);
}
```

This *foreach* style reads as "for each score in scores, do this." The variable `score` takes on each value in turn: 85, then 92, then 78, and so on.

**When to use which:**
- Use `for item in array` when you just need the values
- Use `for i in 0..array.length` when you need the indices (for position, or to modify elements)

---

## Common Array Operations

### Finding the sum
```rust
var numbers = [10, 20, 30, 40, 50];
var total = 0;

for n in numbers {
    total = total + n;
}

Viper.Terminal.Say("Sum: " + total);  // Sum: 150
```

### Finding the average
```rust
var scores = [85, 92, 78, 95, 88];
var sum = 0;

for score in scores {
    sum = sum + score;
}

var average = sum / scores.length;
Viper.Terminal.Say("Average: " + average);  // Average: 87
```

### Finding the maximum
```rust
var values = [23, 7, 42, 15, 8, 31];
var max = values[0];  // Start with the first

for v in values {
    if v > max {
        max = v;
    }
}

Viper.Terminal.Say("Maximum: " + max);  // Maximum: 42
```

### Searching
```rust
var names = ["Alice", "Bob", "Carol", "Dave"];
var target = "Carol";
var found = false;

for i in 0..names.length {
    if names[i] == target {
        Viper.Terminal.Say("Found at index " + i);
        found = true;
        break;
    }
}

if !found {
    Viper.Terminal.Say("Not found");
}
```

### Counting matches
```rust
var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
var evenCount = 0;

for n in numbers {
    if n % 2 == 0 {
        evenCount = evenCount + 1;
    }
}

Viper.Terminal.Say("Even numbers: " + evenCount);  // Even numbers: 5
```

---

## Arrays of Strings

Arrays can hold any type, including strings:

```rust
var fruits = ["apple", "banana", "cherry"];

for fruit in fruits {
    Viper.Terminal.Say("I like " + fruit);
}
```

Output:
```
I like apple
I like banana
I like cherry
```

This is how you'd store a list of names, a menu of options, lines of a file, and countless other real-world data.

---

## Multidimensional Arrays

Sometimes you need a grid — rows and columns. You can make an array of arrays:

```rust
var grid = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
];

// Access element at row 1, column 2
Viper.Terminal.Say(grid[1][2]);  // 6
```

The first index selects the row (inner array), the second selects the column (element within that row).

To iterate over all elements:

```rust
for row in 0..grid.length {
    for col in 0..grid[row].length {
        Viper.Terminal.Print(grid[row][col] + " ");
    }
    Viper.Terminal.Say("");
}
```

Output:
```
1 2 3
4 5 6
7 8 9
```

Multidimensional arrays are perfect for game boards, images (pixels), spreadsheet data, and more.

---

## The Three Languages

**ViperLang**
```rust
var numbers = [10, 20, 30];

// Access
var first = numbers[0];

// Modify
numbers[1] = 25;

// Iterate
for n in numbers {
    Viper.Terminal.Say(n);
}

// Length
Viper.Terminal.Say(numbers.length);
```

**BASIC**
```basic
DIM numbers(2) AS INTEGER  ' Array with indices 0, 1, 2
numbers(0) = 10
numbers(1) = 20
numbers(2) = 30

' Access
DIM first AS INTEGER
first = numbers(0)

' Modify
numbers(1) = 25

' Iterate
FOR i = 0 TO 2
    PRINT numbers(i)
NEXT i

' Length (BASIC uses UBOUND for upper bound)
PRINT UBOUND(numbers) + 1
```

BASIC uses parentheses for array access and requires declaring the size upfront.

**Pascal**
```pascal
var
    numbers: array[0..2] of Integer;
    i: Integer;
begin
    numbers[0] := 10;
    numbers[1] := 20;
    numbers[2] := 30;

    { Access }
    WriteLn(numbers[0]);

    { Modify }
    numbers[1] := 25;

    { Iterate }
    for i := 0 to 2 do
        WriteLn(numbers[i]);

    { Length }
    WriteLn(Length(numbers));
end.
```

Pascal declares array bounds explicitly in the type.

---

## Bounds Checking

What happens if you try to access an invalid index?

```rust
var arr = [1, 2, 3];
var x = arr[10];  // Error! Index out of bounds
```

Viper checks array bounds and gives you an error message rather than crashing mysteriously or returning garbage data. This is a safety feature.

Always be careful with indices:
- Valid indices go from 0 to length - 1
- If `length` is 0 (empty array), there are no valid indices
- Variables used as indices must not go out of range

---

## A Complete Example: Student Grades

Let's build a proper grade tracker:

```rust
module GradeTracker;

func start() {
    var grades: [i64] = [];

    Viper.Terminal.Say("Enter grades (enter -1 to finish):");

    // Collect grades
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

    // Calculate statistics
    if grades.length == 0 {
        Viper.Terminal.Say("No grades entered.");
        return;
    }

    var sum = 0;
    var min = grades[0];
    var max = grades[0];

    for grade in grades {
        sum = sum + grade;
        if grade < min {
            min = grade;
        }
        if grade > max {
            max = grade;
        }
    }

    var average = sum / grades.length;

    // Report
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Grade Report ===");
    Viper.Terminal.Say("Number of grades: " + grades.length);
    Viper.Terminal.Say("Lowest grade: " + min);
    Viper.Terminal.Say("Highest grade: " + max);
    Viper.Terminal.Say("Average: " + average);
}
```

Sample run:
```
Enter grades (enter -1 to finish):
Grade: 85
Grade: 92
Grade: 78
Grade: 95
Grade: -1

=== Grade Report ===
Number of grades: 4
Lowest grade: 78
Highest grade: 95
Average: 87
```

This demonstrates:
- Dynamic array (adding elements with `push`)
- Input validation loop
- Processing all elements to find min, max, sum
- Computing derived values (average)

---

## Common Mistakes

**Off-by-one with length:**
```rust
var arr = [1, 2, 3];
var last = arr[arr.length];  // Error! Should be arr.length - 1
var last = arr[arr.length - 1];  // Correct: index 2
```

**Forgetting arrays are zero-indexed:**
```rust
var scores = [85, 92, 78];
Viper.Terminal.Say(scores[1]);  // Prints 92, not 85!
Viper.Terminal.Say(scores[0]);  // Prints 85 (the first element)
```

**Empty array access:**
```rust
var arr: [i64] = [];
var x = arr[0];  // Error! No elements to access
```

**Using the wrong loop range:**
```rust
var arr = [1, 2, 3, 4, 5];

// Wrong: includes index 5, which doesn't exist
for i in 0..=arr.length {
    Viper.Terminal.Say(arr[i]);
}

// Correct: 0 to length-1
for i in 0..arr.length {
    Viper.Terminal.Say(arr[i]);
}
```

---

## Summary

- Arrays store multiple values under one name
- Access elements with `array[index]`
- Indices start at 0 and go to length - 1
- `array.length` gives the number of elements
- Use `push` to add, `pop` to remove from the end
- Iterate with `for item in array` or `for i in 0..array.length`
- Multidimensional arrays use `array[row][col]`
- Viper checks bounds and reports errors on invalid access

---

## Exercises

**Exercise 6.1**: Create an array of your five favorite foods and print them all.

**Exercise 6.2**: Write a program that creates an array of 10 numbers (you choose the values), then prints them in reverse order.

**Exercise 6.3**: Write a program that asks the user to enter 5 numbers, stores them in an array, then prints the smallest and largest.

**Exercise 6.4**: Write a program that asks for numbers until -1 is entered, then prints how many numbers were entered and how many were positive.

**Exercise 6.5**: Create a 3×3 tic-tac-toe board (use strings like "X", "O", and " " for empty). Write code to print the board in a nice format:
```
 X | O | X
-----------
   | X | O
-----------
 O |   | X
```

**Exercise 6.6** (Challenge): Write a program that checks if an array is *sorted* (each element is >= the one before it). Test it with `[1, 2, 3, 4, 5]` (sorted) and `[1, 3, 2, 4, 5]` (not sorted).

---

*We can now work with groups of data. But our code is getting longer and more complex. How do we organize it? How do we avoid repeating ourselves? Next, we learn about functions — reusable blocks of code.*

*[Continue to Chapter 7: Breaking It Down →](07-functions.md)*
