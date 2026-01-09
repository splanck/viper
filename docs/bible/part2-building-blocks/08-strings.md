# Chapter 8: Text and Strings

Humans communicate with text. Names, messages, addresses, documents, code itself — all text. Programs that work with text are everywhere: word processors, search engines, social media, email clients, and countless more.

In Chapter 3, we introduced strings briefly. Now we go deep. By the end of this chapter, you'll be able to slice, search, transform, and build text with confidence.

---

## Strings Are Sequences of Characters

A string is a sequence of characters. "Hello" contains 5 characters: H, e, l, l, o. Each character has a position, numbered from 0:

```
Index:     0   1   2   3   4
String:    H   e   l   l   o
```

You can access individual characters:

```rust
var word = "Hello";
Viper.Terminal.Say(word[0]);  // H
Viper.Terminal.Say(word[4]);  // o
Viper.Terminal.Say(word.length);  // 5
```

This should feel familiar — strings behave like arrays of characters.

---

## String Length

The `.length` property tells you how many characters a string contains:

```rust
Viper.Terminal.Say("Hello".length);     // 5
Viper.Terminal.Say("".length);          // 0 (empty string)
Viper.Terminal.Say("Hi there!".length); // 9 (space counts)
```

This is essential for loops:

```rust
var text = "Viper";
for i in 0..text.length {
    Viper.Terminal.Say("Character " + i + ": " + text[i]);
}
```

Output:
```
Character 0: V
Character 1: i
Character 2: p
Character 3: e
Character 4: r
```

---

## Concatenation: Joining Strings

The `+` operator joins strings together:

```rust
var first = "Hello";
var second = "World";
var greeting = first + ", " + second + "!";
Viper.Terminal.Say(greeting);  // Hello, World!
```

When you "add" a string and a number, the number is converted to a string:

```rust
var score = 42;
Viper.Terminal.Say("Your score: " + score);  // Your score: 42
```

For building strings piece by piece, concatenation works but can be slow for many operations. Later we'll learn about `StringBuilder` for performance-critical code.

---

## Substrings: Extracting Parts

Often you need just part of a string. The `substring` method extracts a portion:

```rust
var text = "Hello, World!";
var hello = text.substring(0, 5);    // "Hello" (from 0, length 5)
var world = text.substring(7, 5);    // "World" (from 7, length 5)
```

The first argument is the starting position, the second is how many characters to take.

Convenient shortcuts:

```rust
var text = "Hello, World!";

// First N characters
var first3 = text.left(3);   // "Hel"

// Last N characters
var last3 = text.right(3);   // "ld!"

// Skip first N characters
var rest = text.skip(7);     // "World!"
```

---

## Searching in Strings

To find where something appears in a string:

```rust
var text = "Hello, World!";
var pos = text.indexOf("World");  // 7
var notFound = text.indexOf("xyz");  // -1 (not found)
```

`indexOf` returns the position of the first occurrence, or -1 if not found.

To check if a string contains something:

```rust
var text = "The quick brown fox";

if text.contains("quick") {
    Viper.Terminal.Say("Found it!");
}
```

To check how a string starts or ends:

```rust
var filename = "report.pdf";

if filename.endsWith(".pdf") {
    Viper.Terminal.Say("It's a PDF file");
}

if filename.startsWith("report") {
    Viper.Terminal.Say("It's a report");
}
```

---

## Case Conversion

Strings can be converted to all uppercase or all lowercase:

```rust
var text = "Hello, World!";
Viper.Terminal.Say(text.upper());  // HELLO, WORLD!
Viper.Terminal.Say(text.lower());  // hello, world!
```

This is useful for case-insensitive comparisons:

```rust
var input = "YES";

if input.lower() == "yes" {
    Viper.Terminal.Say("User said yes!");
}
```

Without `.lower()`, "YES", "Yes", "yes", and "yEs" would all be different. Converting to lowercase first makes them equal.

---

## Trimming Whitespace

User input often has extra spaces. The `trim` method removes whitespace from both ends:

```rust
var input = "   hello   ";
Viper.Terminal.Say("[" + input.trim() + "]");  // [hello]
```

Variations:

```rust
var text = "   hello   ";
text.trimStart();  // "hello   " (left only)
text.trimEnd();    // "   hello" (right only)
text.trim();       // "hello" (both sides)
```

---

## Replacing Text

To replace occurrences of one string with another:

```rust
var text = "Hello, World!";
var result = text.replace("World", "Viper");
Viper.Terminal.Say(result);  // Hello, Viper!
```

By default, this replaces all occurrences:

```rust
var text = "one fish, two fish, red fish, blue fish";
var result = text.replace("fish", "bird");
// "one bird, two bird, red bird, blue bird"
```

---

## Splitting Strings

To break a string into an array of pieces:

```rust
var csv = "apple,banana,cherry";
var fruits = csv.split(",");

for fruit in fruits {
    Viper.Terminal.Say(fruit);
}
```

Output:
```
apple
banana
cherry
```

Split by any string:

```rust
var text = "one::two::three";
var parts = text.split("::");  // ["one", "two", "three"]
```

This is how you parse data: split by the delimiter, then work with the pieces.

---

## Joining Arrays into Strings

The reverse of splitting — joining an array into a single string:

```rust
var words = ["Hello", "World"];
var sentence = words.join(" ");
Viper.Terminal.Say(sentence);  // Hello World
```

The argument is what to put between elements:

```rust
var numbers = ["1", "2", "3"];
Viper.Terminal.Say(numbers.join(", "));  // 1, 2, 3
Viper.Terminal.Say(numbers.join("-"));   // 1-2-3
Viper.Terminal.Say(numbers.join(""));    // 123
```

---

## Converting Between Strings and Numbers

We've used these before, but they deserve a proper introduction:

**String to number:**
```rust
var text = "42";
var num = Viper.Parse.Int(text);     // 42 (integer)
var pi = Viper.Parse.Float("3.14");  // 3.14 (float)
```

**Number to string:**
```rust
var num = 42;
var text = num.toString();  // "42"
```

Conversion happens automatically in concatenation:

```rust
var result = "Answer: " + 42;  // "Answer: 42"
```

But be careful — `"5" + 3` is `"53"`, not `8`. If you want arithmetic, convert to numbers first.

---

## Character Codes

Every character has a numeric code. The common encoding is ASCII/UTF-8, where 'A' is 65, 'a' is 97, '0' is 48, and so on.

```rust
var code = 'A'.code();        // 65
var char = Char.fromCode(65); // 'A'
```

This lets you do character arithmetic:

```rust
// Check if a character is a digit
func isDigit(c: char) -> bool {
    return c.code() >= '0'.code() && c.code() <= '9'.code();
}

// Convert lowercase to uppercase
func toUpper(c: char) -> char {
    if c.code() >= 'a'.code() && c.code() <= 'z'.code() {
        return Char.fromCode(c.code() - 32);
    }
    return c;
}
```

---

## String Formatting

For complex output, string formatting is cleaner than concatenation:

```rust
var name = "Alice";
var score = 95;
var message = Viper.Fmt.format("Player {} scored {} points!", name, score);
// "Player Alice scored 95 points!"
```

The `{}` placeholders are replaced by the arguments in order.

You can also control formatting:

```rust
var pi = 3.14159265;
Viper.Terminal.Say(Viper.Fmt.format("Pi is approximately {:.2}", pi));
// "Pi is approximately 3.14"
```

The `:.2` means "2 decimal places." We'll explore formatting options more in Chapter 13 when we cover the standard library in depth.

---

## A Complete Example: Word Counter

Let's build a program that analyzes text:

```rust
module WordCounter;

func countWords(text: string) -> i64 {
    var words = text.split(" ");
    var count = 0;

    for word in words {
        if word.trim().length > 0 {
            count += 1;
        }
    }

    return count;
}

func countVowels(text: string) -> i64 {
    var vowels = "aeiouAEIOU";
    var count = 0;

    for i in 0..text.length {
        if vowels.contains(text[i].toString()) {
            count += 1;
        }
    }

    return count;
}

func start() {
    Viper.Terminal.Say("Enter some text:");
    var text = Viper.Terminal.ReadLine();

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Analysis ===");
    Viper.Terminal.Say("Characters: " + text.length);
    Viper.Terminal.Say("Words: " + countWords(text));
    Viper.Terminal.Say("Vowels: " + countVowels(text));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Uppercase: " + text.upper());
    Viper.Terminal.Say("Lowercase: " + text.lower());
}
```

Sample run:
```
Enter some text:
Hello World from Viper

=== Analysis ===
Characters: 22
Words: 4
Vowels: 6

Uppercase: HELLO WORLD FROM VIPER
Lowercase: hello world from viper
```

---

## The Three Languages

**ViperLang**
```rust
var text = "Hello, World!";

// Length
Viper.Terminal.Say(text.length);

// Substring
var sub = text.substring(0, 5);

// Search
var pos = text.indexOf("World");

// Replace
var new = text.replace("World", "Viper");

// Split and join
var parts = text.split(", ");
var joined = parts.join(" - ");

// Case
var upper = text.upper();
var lower = text.lower();
```

**BASIC**
```basic
DIM text AS STRING
text = "Hello, World!"

' Length
PRINT LEN(text)

' Substring
DIM sub AS STRING
sub = MID$(text, 1, 5)  ' BASIC is 1-indexed

' Search
DIM pos AS INTEGER
pos = INSTR(text, "World")

' Replace (using a loop or REPLACE$ if available)
' Case
PRINT UCASE$(text)
PRINT LCASE$(text)

' Trim
PRINT LTRIM$(RTRIM$(text))
```

BASIC uses 1-based indexing for strings and has functions like `MID$`, `LEFT$`, `RIGHT$`, `INSTR`.

**Pascal**
```pascal
var text: string;
begin
    text := 'Hello, World!';

    { Length }
    WriteLn(Length(text));

    { Substring }
    WriteLn(Copy(text, 1, 5));  { Pascal is 1-indexed }

    { Search }
    WriteLn(Pos('World', text));

    { Case }
    WriteLn(UpperCase(text));
    WriteLn(LowerCase(text));

    { Trim }
    WriteLn(Trim(text));
end.
```

Pascal is also 1-indexed and uses `Copy`, `Pos`, and similar functions.

---

## Common Mistakes

**Off-by-one with indexing:**
```rust
var text = "Hello";
var last = text[text.length];      // Error! Index 5 doesn't exist
var last = text[text.length - 1];  // Correct: index 4 is 'o'
```

**Forgetting strings are immutable:**
```rust
var text = "Hello";
text[0] = 'J';  // Error in many languages!
var text = "J" + text.substring(1);  // Create a new string instead
```

Many languages (including ViperLang) don't let you modify characters in place. You create new strings instead.

**Comparing strings with wrong case:**
```rust
var input = "YES";
if input == "yes" {  // False! Case differs
    ...
}
if input.lower() == "yes" {  // Convert first
    ...
}
```

**Empty string vs. null:**
```rust
var empty = "";          // A string with zero characters
var text = "Hello";

if text.length == 0 {    // Check if empty
    ...
}
```

---

## Summary

- Strings are sequences of characters, indexed from 0
- `.length` gives the number of characters
- `+` concatenates strings
- `substring`, `left`, `right` extract portions
- `indexOf`, `contains`, `startsWith`, `endsWith` search
- `upper`, `lower` change case
- `trim`, `trimStart`, `trimEnd` remove whitespace
- `replace` substitutes text
- `split` breaks into arrays; `join` combines arrays
- `Viper.Parse.Int/Float` convert strings to numbers
- `Viper.Fmt.format` creates formatted strings

---

## Exercises

**Exercise 8.1**: Write a function that takes a string and returns it reversed. "hello" → "olleh"

**Exercise 8.2**: Write a function that checks if a string is a palindrome (reads the same forward and backward, like "racecar").

**Exercise 8.3**: Write a program that asks for a sentence and prints each word on a separate line.

**Exercise 8.4**: Write a function that capitalizes the first letter of each word. "hello world" → "Hello World"

**Exercise 8.5**: Write a program that counts how many times each vowel appears in a string.

**Exercise 8.6** (Challenge): Write a simple "find and replace" program that asks for text, a word to find, and a word to replace it with, then shows the result.

**Exercise 8.7** (Challenge): Write a program that takes a string and removes all duplicate spaces (so "hello   world" becomes "hello world").

---

*We can now work with text. But what about text stored in files? Next, we learn to read and write persistent data.*

*[Continue to Chapter 9: Files and Persistence →](09-files.md)*
