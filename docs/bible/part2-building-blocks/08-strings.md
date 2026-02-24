# Chapter 8: Text and Strings

Humans communicate with text. Names, messages, addresses, documents, code itself — all text. Programs that work with text are everywhere: word processors, search engines, social media, email clients, and countless more. Every time you type a search query, send a message, or fill out a form, text is being processed, validated, transformed, and stored.

In Chapter 3, we introduced strings briefly. Now we go deep. By the end of this chapter, you'll be able to slice, search, transform, and build text with confidence. You'll understand not just *how* strings work, but *why* they work the way they do — knowledge that will serve you in any programming language.

---

## What Are Strings, Really?

At the most fundamental level, a string is simply an array of characters stored consecutively in memory. When you write `"Hello"`, the computer stores five separate values — the character codes for H, e, l, l, and o — one after another.

```
Memory address:  1000  1001  1002  1003  1004
Character:         H     e     l     l     o
Code:             72   101   108   108   111
```

This might seem like a mundane detail, but understanding strings as arrays of characters explains everything about how they behave:

- **Why strings have a length property**: It counts the array elements.
- **Why you can access individual characters with `[index]`**: You're indexing into the array.
- **Why string operations often create new strings**: Arrays have fixed sizes in memory.

Think of a string like a row of post office boxes. Each box (memory location) holds exactly one character. If you want a longer string, you need to find a new row of boxes big enough to hold everything.

### Why Text Handling Is Special

You might wonder why text deserves its own chapter when it's "just an array." The answer is that text processing is *incredibly* common and *surprisingly* complex:

**Text is everywhere in programming:**
- User names, passwords, email addresses
- File paths, URLs, configuration values
- Database queries, API responses, log messages
- Every piece of data a user types or reads

**Text has special needs:**
- We need to search, replace, and transform text constantly
- We need to handle case sensitivity ("Bob" vs "bob")
- We need to deal with whitespace, punctuation, and formatting
- We need to parse structured data ("name,age,city")

Because text manipulation is so common, languages provide rich sets of string operations. Learning these operations well is one of the highest-value skills in programming.

---

## Strings Are Sequences of Characters

A string is a sequence of characters. "Hello" contains 5 characters: H, e, l, l, o. Each character has a position, numbered from 0:

```
Index:     0   1   2   3   4
String:    H   e   l   l   o
```

Why start at 0? It comes from how computers calculate memory addresses. If the string starts at memory location 1000, the first character (H) is at 1000 + 0, the second (e) is at 1000 + 1, and so on. Zero-based indexing makes this arithmetic simple.

You can access individual characters:

```rust
bind Viper.Terminal;

var word = "Hello";
Say(word[0]);  // H
Say(word[4]);  // o
Say(word.Length);  // 5
```

This should feel familiar — strings behave like arrays of characters because that's exactly what they are.

---

## Character-Level Operations

Before diving into string operations, let's master working with individual characters. This fundamental skill underlies everything else.

### Accessing Characters by Index

Every character in a string has a position (index), starting from 0:

```rust
bind Viper.Terminal;

var name = "Alice";
Say(name[0]);  // A (first character)
Say(name[1]);  // l (second character)
Say(name[4]);  // e (fifth/last character)
```

You can use any expression as an index:

```rust
bind Viper.Terminal;

var text = "Programming";
var middle = text.Length / 2;
Say(text[middle]);  // Prints the middle character

var lastIndex = text.Length - 1;
Say(text[lastIndex]);  // g (last character)
```

### Iterating Through Strings

To process every character, loop through the indices:

```rust
bind Viper.Terminal;

var text = "Viper";
for i in 0..text.Length {
    Say("Character " + i + ": " + text[i]);
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

This pattern is fundamental. Want to count uppercase letters? Loop through each character and check. Want to find all positions of a letter? Loop and record indices where it matches.

```rust
bind Viper.Terminal;

// Count uppercase letters
func countUppercase(text: String) -> Integer {
    var count = 0;
    for i in 0..text.Length {
        var c = text[i];
        if c.code() >= 'A'.code() && c.code() <= 'Z'.code() {
            count += 1;
        }
    }
    return count;
}

Say(countUppercase("Hello World"));  // 2 (H and W)
```

### Building Strings Character by Character

You can construct strings by starting empty and adding characters:

```rust
bind Viper.Terminal;

// Reverse a string
func reverse(text: String) -> String {
    var result = "";
    for i in 0..text.Length {
        // Add characters from the end to the beginning
        result = result + text[text.Length - 1 - i];
    }
    return result;
}

Say(reverse("Hello"));  // olleH
```

Or filter characters based on criteria:

```rust
bind Viper.Terminal;

// Keep only letters
func lettersOnly(text: String) -> String {
    var result = "";
    for i in 0..text.Length {
        var c = text[i];
        var code = c.code();
        var isLower = code >= 'a'.code() && code <= 'z'.code();
        var isUpper = code >= 'A'.code() && code <= 'Z'.code();
        if isLower || isUpper {
            result = result + c;
        }
    }
    return result;
}

Say(lettersOnly("Hello, World! 123"));  // HelloWorld
```

---

## String Length

The `.Length` property tells you how many characters a string contains:

```rust
bind Viper.Terminal;

Say("Hello".Length);     // 5
Say("".Length);          // 0 (empty string)
Say("Hi there!".Length); // 9 (space counts)
```

**Every character counts** — letters, numbers, spaces, punctuation, even invisible characters like tabs and newlines:

```rust
bind Viper.Terminal;

Say("A B".Length);    // 3 (A, space, B)
Say("A\tB".Length);   // 3 (A, tab, B)
Say("A\nB".Length);   // 3 (A, newline, B)
```

### Why Length Matters

Length is essential for loops:

```rust
bind Viper.Terminal;

var text = "Viper";
for i in 0..text.Length {
    Say("Character " + i + ": " + text[i]);
}
```

And for validation:

```rust
bind Viper.Terminal;

func validatePassword(password: String) -> Boolean {
    if password.Length < 8 {
        Say("Password must be at least 8 characters");
        return false;
    }
    if password.Length > 128 {
        Say("Password too long");
        return false;
    }
    return true;
}
```

And for safe indexing:

```rust
bind Viper.Terminal;

var text = "Hello";
var index = 10;

// Always check before accessing
if index >= 0 && index < text.Length {
    Say(text[index]);
} else {
    Say("Index out of bounds");
}
```

---

## String Immutability

Here's a crucial concept that surprises many beginners: **strings are immutable** — you cannot change them after they're created.

```rust
var text = "Hello";
text[0] = 'J';  // Error! Cannot modify string characters
```

This isn't a limitation — it's a deliberate design choice with important benefits:

### Why Immutability?

**Safety**: When you pass a string to a function, you know it won't be modified unexpectedly.

```rust
bind Viper.Terminal;

var username = "Alice";
someFunction(username);  // Cannot modify our string
Say(username);  // Still "Alice", guaranteed
```

**Sharing**: The same string can be shared by multiple variables without copying.

```rust
var a = "Hello";
var b = a;  // Both point to the same string in memory
// This is safe because neither can modify it
```

**Optimization**: The compiler can make assumptions that enable faster code.

### What Happens When You "Modify" a String

When you appear to modify a string, you're actually creating a *new* string:

```rust
var text = "Hello";
text = "J" + text.Substring(1);  // Creates new string "Jello"
// The old "Hello" string still exists (until garbage collected)
```

Think of it like editing a document on paper versus on a computer. With paper (mutable), you erase and write over. With strings (immutable), you create a fresh copy with changes.

```rust
var name = "alice";
var properName = name[0].ToUpper() + name.Substring(1);
// name is still "alice"
// properName is a new string "Alice"
```

### The Implications

Because every "modification" creates a new string, repeatedly modifying strings can be slow:

```rust
// Inefficient: Creates many intermediate strings
var result = "";
for i in 0..1000 {
    result = result + "x";  // New string each iteration!
}
```

This creates 1000 intermediate strings that are immediately discarded. For building large strings, we'll learn better patterns later in this chapter.

---

## Concatenation: Joining Strings

The `+` operator joins strings together:

```rust
bind Viper.Terminal;

var first = "Hello";
var second = "World";
var greeting = first + ", " + second + "!";
Say(greeting);  // Hello, World!
```

When you "add" a string and a number, the number is converted to a string:

```rust
bind Viper.Terminal;

var score = 42;
Say("Your score: " + score);  // Your score: 42
```

### Concatenation Order Matters

Because conversion happens automatically, order can be surprising:

```rust
bind Viper.Terminal;

// String first: concatenation
Say("Score: " + 5 + 3);   // Score: 53 (string concat)

// Numbers first: arithmetic then concat
Say(5 + 3 + " points");   // 8 points (addition first)

// Use parentheses to be explicit
Say("Total: " + (5 + 3)); // Total: 8
```

The rule: operations are evaluated left to right. Once you hit a string, everything after becomes concatenation.

### Building Strings with Concatenation

Concatenation is intuitive for simple cases:

```rust
bind Viper.Terminal;

func formatName(first: String, middle: String, last: String) -> String {
    return first + " " + middle + " " + last;
}

Say(formatName("John", "Fitzgerald", "Kennedy"));
// John Fitzgerald Kennedy
```

But for many operations, it can be slow due to immutability. We'll cover better patterns later.

---

## Substrings: Extracting Parts

Often you need just part of a string. The `substring` method extracts a portion:

```rust
var text = "Hello, World!";
var hello = text.Substring(0, 5);    // "Hello" (from 0, length 5)
var world = text.Substring(7, 5);    // "World" (from 7, length 5)
```

The first argument is the starting position, the second is how many characters to take.

### Understanding Substring Parameters

Think of `substring(start, length)` as answering: "Starting at position `start`, give me `length` characters."

```rust
var text = "ABCDEFGHIJ";
//          0123456789

text.Substring(0, 3);   // "ABC" - from position 0, take 3
text.Substring(3, 4);   // "DEFG" - from position 3, take 4
text.Substring(7, 3);   // "HIJ" - from position 7, take 3
```

### What If You Request Too Many Characters?

If you request more characters than exist, you get what's available:

```rust
var text = "Hi";
text.Substring(0, 10);  // "Hi" - only 2 characters exist
text.Substring(1, 10);  // "i" - only 1 character from position 1
```

### Convenient Shortcuts

```rust
var text = "Hello, World!";

// First N characters
var first3 = text.left(3);   // "Hel"

// Last N characters
var last3 = text.right(3);   // "ld!"

// Skip first N characters
var rest = text.skip(7);     // "World!"
```

### Practical Examples

**Extract file extension:**
```rust
bind Viper.Terminal;

func getExtension(filename: String) -> String {
    var dotPos = filename.LastIndexOf(".");
    if dotPos == -1 {
        return "";  // No extension
    }
    return filename.skip(dotPos + 1);
}

Say(getExtension("report.pdf"));    // pdf
Say(getExtension("archive.tar.gz")); // gz
Say(getExtension("README"));         // (empty)
```

**Extract domain from email:**
```rust
bind Viper.Terminal;

func getEmailDomain(email: String) -> String {
    var atPos = email.IndexOf("@");
    if atPos == -1 {
        return "";  // Not a valid email
    }
    return email.skip(atPos + 1);
}

Say(getEmailDomain("user@example.com"));  // example.com
```

**Truncate with ellipsis:**
```rust
bind Viper.Terminal;

func truncate(text: String, maxLength: Integer) -> String {
    if text.Length <= maxLength {
        return text;
    }
    return text.left(maxLength - 3) + "...";
}

Say(truncate("Hello, World!", 10));  // Hello, ...
Say(truncate("Hi", 10));             // Hi
```

---

## Searching in Strings

Finding text within text is one of the most common string operations.

### indexOf: Finding Position

`indexOf` returns the position of the first occurrence, or -1 if not found:

```rust
var text = "Hello, World!";
var pos = text.IndexOf("World");  // 7
var notFound = text.IndexOf("xyz");  // -1 (not found)
```

Why -1 for "not found"? Because 0 is a valid position (the start of the string), we need a different value to indicate failure. Negative numbers can't be valid positions, so -1 is the universal convention.

### Using indexOf Results

Always check if the search succeeded:

```rust
bind Viper.Terminal;

var email = "user@example.com";
var atPos = email.IndexOf("@");

if atPos == -1 {
    Say("Invalid email: no @ symbol");
} else {
    var username = email.left(atPos);
    var domain = email.skip(atPos + 1);
    Say("Username: " + username);  // user
    Say("Domain: " + domain);      // example.com
}
```

### lastIndexOf: Finding the Last Occurrence

When a substring appears multiple times, `lastIndexOf` finds the last one:

```rust
var path = "/home/user/documents/file.txt";
var lastSlash = path.LastIndexOf("/");  // 20
var filename = path.skip(lastSlash + 1);  // file.txt
```

### contains: Simple Presence Check

When you only care whether something exists, not where:

```rust
bind Viper.Terminal;

var text = "The quick brown fox";

if text.Contains("quick") {
    Say("Found it!");
}
```

This is cleaner than checking `indexOf(...) != -1`.

### startsWith and endsWith

Check the beginning or end of a string:

```rust
bind Viper.Terminal;

var filename = "report.pdf";

if filename.EndsWith(".pdf") {
    Say("It's a PDF file");
}

if filename.StartsWith("report") {
    Say("It's a report");
}
```

**File type detection:**
```rust
func getFileType(filename: String) -> String {
    if filename.EndsWith(".jpg") || filename.EndsWith(".png") {
        return "image";
    }
    if filename.EndsWith(".mp3") || filename.EndsWith(".wav") {
        return "audio";
    }
    if filename.EndsWith(".txt") || filename.EndsWith(".md") {
        return "text";
    }
    return "unknown";
}
```

**URL validation:**
```rust
func isSecureUrl(url: String) -> Boolean {
    return url.StartsWith("https://");
}
```

### Finding All Occurrences

`indexOf` only finds the first occurrence. To find all:

```rust
func findAll(text: String, search: String) -> [Integer] {
    var positions = [];
    var pos = 0;

    while pos < text.Length {
        var found = text.IndexOf(search, pos);  // Start search from pos
        if found == -1 {
            break;
        }
        positions.Push(found);
        pos = found + 1;  // Continue after this occurrence
    }

    return positions;
}

var positions = findAll("abracadabra", "a");
// [0, 3, 5, 7, 10] - all positions of 'a'
```

---

## String Comparison

Comparing strings is essential for sorting, searching, and validation — but it's full of subtleties.

### Basic Equality

```rust
bind Viper.Terminal;

var a = "hello";
var b = "hello";
var c = "Hello";

Say(a == b);  // true - exact match
Say(a == c);  // false - case differs!
```

### Case Sensitivity

String comparison is **case-sensitive** by default. "Hello" and "hello" are different strings:

```rust
bind Viper.Terminal;

var input = "YES";
if input == "yes" {  // false!
    Say("Match");
}
```

For case-insensitive comparison, convert to the same case first:

```rust
bind Viper.Terminal;

var input = "YES";
if input.ToLower() == "yes" {  // true!
    Say("User said yes");
}
```

**Common pattern for user input:**
```rust
bind Viper.Terminal;

func normalizeInput(text: String) -> String {
    return text.Trim().ToLower();
}

var input = "  YES  ";
if normalizeInput(input) == "yes" {
    Say("Confirmed");
}
```

### Alphabetical Ordering

Strings can be compared alphabetically using `<`, `>`, `<=`, `>=`:

```rust
bind Viper.Terminal;

Say("apple" < "banana");  // true (a before b)
Say("cat" < "car");       // false (t after r)
Say("apple" < "apricot"); // true (p before r)
```

This is called **lexicographic ordering** — comparing character by character until a difference is found.

### The ASCII Trap

Alphabetical comparison uses character codes, which leads to surprises:

```rust
bind Viper.Terminal;

// All uppercase letters come BEFORE all lowercase
Say("Z" < "a");     // true! Z (90) < a (97)
Say("Apple" < "banana"); // true (A < b)
Say("apple" < "Banana"); // false (a > B)
```

For true alphabetical sorting, convert to the same case:

```rust
bind Viper.Terminal;

func alphabeticallyBefore(a: String, b: String) -> Boolean {
    return a.ToLower() < b.ToLower();
}

Say(alphabeticallyBefore("apple", "Banana"));  // true
```

### Numeric Strings Don't Sort Numerically

Another common trap:

```rust
bind Viper.Terminal;

Say("9" < "10");   // false! '9' (57) > '1' (49)
Say("9" < "100");  // false! Same reason
```

For numeric comparison, convert to numbers:

```rust
bind Viper.Terminal;
bind Viper.Convert as Convert;

var a = "9";
var b = "10";
if Convert.ToInt64(a) < Convert.ToInt64(b) {
    Say("9 is less than 10");  // Correct!
}
```

### Comparing Empty Strings

```rust
bind Viper.Terminal;

var empty = "";
var space = " ";

Say(empty == "");     // true
Say(empty == space);  // false (space is a character)
Say(empty.Length == 0); // true
Say(space.Length == 0); // false
```

---

## Case Conversion

Strings can be converted to all uppercase or all lowercase:

```rust
bind Viper.Terminal;

var text = "Hello, World!";
Say(text.ToUpper());  // HELLO, WORLD!
Say(text.ToLower());  // hello, world!
```

Remember: the original string is unchanged (immutability):

```rust
bind Viper.Terminal;

var name = "Alice";
Say(name.ToUpper());  // ALICE
Say(name);          // Alice (still)
```

### Case-Insensitive Comparisons

The most common use of case conversion:

```rust
bind Viper.Terminal;

var input = "YES";

if input.ToLower() == "yes" {
    Say("User said yes!");
}
```

Without `.ToLower()`, "YES", "Yes", "yes", and "yEs" would all be different. Converting to lowercase first makes them equal.

### Capitalizing Names

Combine case conversion with substrings:

```rust
bind Viper.Terminal;

func capitalize(text: String) -> String {
    if text.Length == 0 {
        return "";
    }
    return text[0].ToUpper() + text.skip(1).ToLower();
}

Say(capitalize("jOHN"));   // John
Say(capitalize("ALICE"));  // Alice
```

**Title case (capitalize each word):**
```rust
bind Viper.Terminal;

func titleCase(text: String) -> String {
    var words = text.Split(" ");
    var result = [];

    for word in words {
        result.Push(capitalize(word));
    }

    return result.Join(" ");
}

Say(titleCase("the quick brown fox"));
// The Quick Brown Fox
```

---

## Trimming Whitespace

User input often has extra spaces. The `trim` method removes whitespace from both ends:

```rust
bind Viper.Terminal;

var input = "   hello   ";
Say("[" + input.Trim() + "]");  // [hello]
```

Variations:

```rust
var text = "   hello   ";
text.TrimStart();  // "hello   " (left only)
text.TrimEnd();    // "   hello" (right only)
text.Trim();       // "hello" (both sides)
```

### What Counts as Whitespace?

- Spaces (' ')
- Tabs ('\t')
- Newlines ('\n')
- Carriage returns ('\r')

```rust
bind Viper.Terminal;

var messy = "\t  Hello World  \n";
Say("[" + messy.Trim() + "]");  // [Hello World]
```

### Essential for User Input

Always trim user input before processing:

```rust
bind Viper.Terminal;

Say("Enter your name:");
var name = ReadLine().Trim();

if name.Length == 0 {
    Say("Name cannot be empty");
}
```

Without trimming, a user who types "   " (just spaces) would pass a length check but cause problems later.

---

## Replacing Text

To replace occurrences of one string with another:

```rust
bind Viper.Terminal;

var text = "Hello, World!";
var result = text.Replace("World", "Viper");
Say(result);  // Hello, Viper!
```

By default, this replaces all occurrences:

```rust
var text = "one fish, two fish, red fish, blue fish";
var result = text.Replace("fish", "bird");
// "one bird, two bird, red bird, blue bird"
```

### Common Use Cases

**Sanitizing input:**
```rust
func sanitize(text: String) -> String {
    var result = text;
    result = result.Replace("<", "&lt;");
    result = result.Replace(">", "&gt;");
    result = result.Replace("&", "&amp;");
    return result;
}
```

**Normalizing data:**
```rust
bind Viper.Terminal;

// Remove common variations in phone numbers
func normalizePhone(phone: String) -> String {
    var result = phone;
    result = result.Replace(" ", "");
    result = result.Replace("-", "");
    result = result.Replace("(", "");
    result = result.Replace(")", "");
    result = result.Replace("+", "");
    return result;
}

Say(normalizePhone("+1 (555) 123-4567"));  // 15551234567
```

**Template substitution:**
```rust
bind Viper.Terminal;

func greet(template: String, name: String, time: String) -> String {
    var result = template;
    result = result.Replace("{name}", name);
    result = result.Replace("{time}", time);
    return result;
}

var template = "Good {time}, {name}! Welcome back.";
Say(greet(template, "Alice", "morning"));
// Good morning, Alice! Welcome back.
```

### Replace Is Case-Sensitive

```rust
bind Viper.Terminal;

var text = "Hello hello HELLO";
Say(text.Replace("hello", "hi"));
// Hello hi HELLO (only exact match replaced)
```

For case-insensitive replace, you need a more complex approach:

```rust
func replaceIgnoreCase(text: String, search: String, replacement: String) -> String {
    var result = "";
    var i = 0;
    var searchLower = search.ToLower();

    while i < text.Length {
        // Check if search string matches at current position
        if i + search.Length <= text.Length {
            var segment = text.Substring(i, search.Length);
            if segment.ToLower() == searchLower {
                result = result + replacement;
                i = i + search.Length;
                continue;
            }
        }
        result = result + text[i];
        i = i + 1;
    }

    return result;
}
```

---

## Splitting Strings

Splitting breaks a string into an array of pieces at each occurrence of a delimiter:

```rust
bind Viper.Terminal;

var csv = "apple,banana,cherry";
var fruits = csv.Split(",");

for fruit in fruits {
    Say(fruit);
}
```

Output:
```
apple
banana
cherry
```

### Understanding Split

Split finds every occurrence of the delimiter and breaks the string there:

```rust
var text = "one::two::three";
var parts = text.Split("::");  // ["one", "two", "three"]
```

The delimiter itself is removed — you get everything *between* the delimiters.

### Empty Strings in Split Results

Watch out for edge cases:

```rust
var text = "a,,b";
var parts = text.Split(",");  // ["a", "", "b"]
// The empty string between the two commas is preserved
```

```rust
var text = ",a,b,";
var parts = text.Split(",");  // ["", "a", "b", ""]
// Leading/trailing delimiters create empty strings
```

### Common Split Patterns

**Split by space (words):**
```rust
var sentence = "The quick brown fox";
var words = sentence.Split(" ");  // ["The", "quick", "brown", "fox"]
```

**Split by newline (lines):**
```rust
var text = "line one\nline two\nline three";
var lines = text.Split("\n");
```

**Split by multiple characters:**
```rust
var data = "name=Alice&age=30&city=Boston";
var pairs = data.Split("&");  // ["name=Alice", "age=30", "city=Boston"]
```

### Parsing CSV Data

CSV (Comma-Separated Values) is extremely common:

```rust
bind Viper.Terminal;

func parseCSVLine(line: String) -> [String] {
    return line.Split(",");
}

var line = "Alice,30,Engineer,Boston";
var fields = parseCSVLine(line);

Say("Name: " + fields[0]);       // Alice
Say("Age: " + fields[1]);        // 30
Say("Job: " + fields[2]);        // Engineer
Say("City: " + fields[3]);       // Boston
```

**Processing multiple lines:**
```rust
bind Viper.Terminal;

var csvData = "Alice,30,Boston\nBob,25,Seattle\nCarol,35,Denver";
var lines = csvData.Split("\n");

for line in lines {
    var fields = line.Split(",");
    var name = fields[0];
    var age = fields[1];
    var city = fields[2];
    Say(name + " is " + age + " years old, lives in " + city);
}
```

Output:
```
Alice is 30 years old, lives in Boston
Bob is 25 years old, lives in Seattle
Carol is 35 years old, lives in Denver
```

---

## Joining Arrays into Strings

The reverse of splitting — combining an array into a single string:

```rust
bind Viper.Terminal;

var words = ["Hello", "World"];
var sentence = words.Join(" ");
Say(sentence);  // Hello World
```

The argument is what to put between elements:

```rust
bind Viper.Terminal;

var numbers = ["1", "2", "3"];
Say(numbers.Join(", "));  // 1, 2, 3
Say(numbers.Join("-"));   // 1-2-3
Say(numbers.Join(""));    // 123
```

### Split and Join Together

Split and join are complementary operations:

```rust
var text = "Hello, World!";
var parts = text.Split(", ");  // ["Hello", "World!"]
var rejoined = parts.Join(", "); // "Hello, World!"
```

**Transform and rejoin:**
```rust
bind Viper.Terminal;

// Capitalize each word
func titleCase(text: String) -> String {
    var words = text.Split(" ");
    var capitalized = [];

    for word in words {
        if word.Length > 0 {
            var cap = word[0].ToUpper() + word.skip(1).ToLower();
            capitalized.Push(cap);
        }
    }

    return capitalized.Join(" ");
}

Say(titleCase("the QUICK brown FOX"));
// The Quick Brown Fox
```

**Change delimiter:**
```rust
bind Viper.Terminal;

// Convert paths between systems
func windowsToUnix(path: String) -> String {
    return path.Split("\\").Join("/");
}

Say(windowsToUnix("C:\\Users\\Alice\\Documents"));
// C:/Users/Alice/Documents
```

---

## String Building Patterns

Because strings are immutable, building strings efficiently requires thought.

### The Problem with Repeated Concatenation

```rust
// Inefficient: Creates a new string every iteration
var result = "";
for i in 0..1000 {
    result = result + "x";
}
```

This looks innocent but is slow. Each `+` creates an entirely new string:
- First iteration: create string of length 1
- Second iteration: create string of length 2
- Third iteration: create string of length 3
- ...and so on

You end up copying characters repeatedly.

### Solution: Use StringBuilder

For building large strings, use `StringBuilder`:

```rust
bind Viper.Text.StringBuilder as SB;

var builder = new SB();

for i in 0..1000 {
    builder.Append("x");
}

var result = builder.ToString();
```

`StringBuilder` collects pieces efficiently and only creates the final string once.

### When to Use What

**Use concatenation for:**
- Simple, one-time joins: `firstName + " " + lastName`
- Building small strings
- Clarity is more important than performance

**Use StringBuilder for:**
- Loops that add to a string
- Building large strings piece by piece
- Performance-critical code

### Building Complex Output

```rust
bind Viper.Text.StringBuilder as SB;
bind Viper.Time;
bind Viper.Fmt as Fmt;

func generateReport(data: [Record]) -> String {
    var sb = new SB();

    sb.Append("=== Report ===\n");
    var ts = Time.DateTime.Now();
    sb.Append("Generated: " + Fmt.Int(Time.DateTime.Year(ts)) + "-" +
              Fmt.IntPad(Time.DateTime.Month(ts), 2, "0") + "-" +
              Fmt.IntPad(Time.DateTime.Day(ts), 2, "0") + "\n");
    sb.Append("\n");

    for record in data {
        sb.Append("Name: " + record.name + "\n");
        sb.Append("Score: " + Fmt.Int(record.score) + "\n");
        sb.Append("---\n");
    }

    sb.Append("\nTotal records: " + Fmt.Int(data.Length));

    return sb.ToString();
}
```

### Alternative: Collect and Join

Another efficient pattern is to collect pieces in an array and join at the end:

```rust
bind Viper.Terminal;
bind Viper.String as Str;

func buildList(items: [String]) -> String {
    var lines = [];

    for i in 0..items.Length {
        lines.Push((i + 1) + ". " + items[i]);
    }

    return Str.Join("\n", lines);
}

var items = ["Apple", "Banana", "Cherry"];
Say(buildList(items));
// 1. Apple
// 2. Banana
// 3. Cherry
```

This avoids repeated concatenation and is often cleaner than StringBuilder.

---

## Converting Between Strings and Numbers

**String to number:**
```rust
bind Viper.Convert as Convert;

var text = "42";
var num = Convert.ToInt64(text);     // 42 (integer)
var pi = Convert.ToDouble("3.14");   // 3.14 (float)
```

**Number to string:**
```rust
bind Viper.Fmt as Fmt;

var num = 42;
var text = Fmt.Int(num);  // "42"
```

Conversion happens automatically in concatenation:

```rust
var result = "Answer: " + 42;  // "Answer: 42"
```

### The Classic Trap

Be careful — `"5" + 3` is `"53"`, not `8`:

```rust
bind Viper.Terminal;
bind Viper.Convert as Convert;

var input = "5";
Say(input + 3);  // "53" (string concatenation!)
Say(Convert.ToInt64(input) + 3);  // 8 (arithmetic)
```

If you want arithmetic, convert to numbers first.

### Handling Invalid Input

What if the string isn't a valid number?

```rust
bind Viper.Convert as Convert;

var result = Convert.ToInt64("abc");  // Error or NaN

// Safe approach with validation
func safeParseInt(text: String) -> Integer {
    // Check if string contains only digits
    for i in 0..text.Length {
        var c = text[i];
        if c.code() < '0'.code() || c.code() > '9'.code() {
            return -1;  // Invalid
        }
    }
    return Convert.ToInt64(text);
}
```

---

## Character Codes

Every character has a numeric code. The common encoding is ASCII/UTF-8, where 'A' is 65, 'a' is 97, '0' is 48, and so on.

```rust
var code = 'A'.code();        // 65
var char = Char.fromCode(65); // 'A'
```

### Essential ASCII Values

```
'0' = 48, '1' = 49, ... '9' = 57
'A' = 65, 'B' = 66, ... 'Z' = 90
'a' = 97, 'b' = 98, ... 'z' = 122
' ' = 32 (space)
```

Notice the patterns:
- Digits are 48-57 (contiguous)
- Uppercase letters are 65-90 (contiguous)
- Lowercase letters are 97-122 (contiguous)
- Uppercase to lowercase: add 32

### Character Classification

```rust
func isDigit(c: Char) -> Boolean {
    return c.code() >= '0'.code() && c.code() <= '9'.code();
}

func isUppercase(c: Char) -> Boolean {
    return c.code() >= 'A'.code() && c.code() <= 'Z'.code();
}

func isLowercase(c: Char) -> Boolean {
    return c.code() >= 'a'.code() && c.code() <= 'z'.code();
}

func isLetter(c: Char) -> Boolean {
    return isUppercase(c) || isLowercase(c);
}

func isAlphanumeric(c: Char) -> Boolean {
    return isLetter(c) || isDigit(c);
}
```

### Character Conversion

```rust
func toUpper(c: Char) -> Char {
    if c.code() >= 'a'.code() && c.code() <= 'z'.code() {
        return Char.fromCode(c.code() - 32);
    }
    return c;
}

func toLower(c: Char) -> Char {
    if c.code() >= 'A'.code() && c.code() <= 'Z'.code() {
        return Char.fromCode(c.code() + 32);
    }
    return c;
}
```

### Digit Values

```rust
// Get numeric value of a digit character
func digitValue(c: Char) -> Integer {
    if c.code() >= '0'.code() && c.code() <= '9'.code() {
        return c.code() - '0'.code();
    }
    return -1;  // Not a digit
}

bind Viper.Terminal;

Say(digitValue('7'));  // 7
Say(digitValue('a'));  // -1
```

---

## String Formatting

For complex output, string formatting is cleaner than concatenation:

```rust
bind Viper.Fmt;

var name = "Alice";
var score = 95;
var message = format("Player {} scored {} points!", name, score);
// "Player Alice scored 95 points!"
```

The `{}` placeholders are replaced by the arguments in order.

### Format Specifiers

You can control formatting:

```rust
bind Viper.Terminal;
bind Viper.Fmt;

var pi = 3.14159265;
Say(format("Pi is approximately {:.2}", pi));
// "Pi is approximately 3.14"
```

The `:.2` means "2 decimal places." We'll explore formatting options more in Chapter 13 when we cover the standard library in depth.

### Padding and Alignment

```rust
bind Viper.Fmt;

// Right-align in 10 characters
format("{:>10}", "hi");  // "        hi"

// Left-align in 10 characters
format("{:<10}", "hi");  // "hi        "

// Center in 10 characters
format("{:^10}", "hi");  // "    hi    "

// Pad with zeros
format("{:05}", 42);     // "00042"
```

---

## Practical Text Processing

Let's apply everything to real-world problems.

### Validating Email Addresses

A basic email validator:

```rust
bind Viper.Terminal;

func isValidEmail(email: String) -> Boolean {
    var trimmed = email.Trim();

    // Must contain exactly one @
    var atPos = trimmed.IndexOf("@");
    if atPos == -1 {
        return false;  // No @
    }
    if trimmed.LastIndexOf("@") != atPos {
        return false;  // Multiple @s
    }

    // @ cannot be first or last
    if atPos == 0 || atPos == trimmed.Length - 1 {
        return false;
    }

    // Must have a dot after @
    var domain = trimmed.skip(atPos + 1);
    var dotPos = domain.IndexOf(".");
    if dotPos == -1 || dotPos == 0 || dotPos == domain.Length - 1 {
        return false;
    }

    return true;
}

Say(isValidEmail("user@example.com"));   // true
Say(isValidEmail("invalid"));            // false
Say(isValidEmail("no@dots"));            // false
Say(isValidEmail("two@@signs.com"));     // false
```

### Formatting Names

```rust
bind Viper.Terminal;

func formatName(fullName: String) -> String {
    var trimmed = fullName.Trim();

    // Handle empty input
    if trimmed.Length == 0 {
        return "";
    }

    // Split into parts
    var parts = trimmed.Split(" ");

    // Filter out empty parts (multiple spaces)
    var cleanParts = [];
    for part in parts {
        if part.Length > 0 {
            cleanParts.Push(part);
        }
    }

    // Capitalize each part
    var formatted = [];
    for part in cleanParts {
        var cap = part[0].ToUpper() + part.skip(1).ToLower();
        formatted.Push(cap);
    }

    return formatted.Join(" ");
}

Say(formatName("  john   SMITH  "));  // John Smith
Say(formatName("ALICE"));             // Alice
Say(formatName("bob jones jr"));      // Bob Jones Jr
```

### Parsing Key-Value Data

```rust
func parseKeyValue(text: String) -> Map[String, String] {
    var result = new Map();
    var pairs = text.Split("&");

    for pair in pairs {
        var eqPos = pair.IndexOf("=");
        if eqPos != -1 {
            var key = pair.left(eqPos).Trim();
            var value = pair.skip(eqPos + 1).Trim();
            result.Set(key, value);
        }
    }

    return result;
}

bind Viper.Terminal;

var data = "name=Alice&age=30&city=Boston";
var parsed = parseKeyValue(data);
Say(parsed.Get("name"));  // Alice
Say(parsed.Get("age"));   // 30
```

### Cleaning User Input

```rust
bind Viper.Terminal;

func cleanInput(text: String) -> String {
    var result = text.Trim();

    // Normalize whitespace (collapse multiple spaces)
    while result.Contains("  ") {
        result = result.Replace("  ", " ");
    }

    return result;
}

Say(cleanInput("  hello    world  "));  // "hello world"
```

### Password Strength Checker

```rust
func checkPasswordStrength(password: String) -> String {
    var score = 0;
    var feedback = [];

    // Length check
    if password.Length >= 8 {
        score += 1;
    } else {
        feedback.Push("at least 8 characters");
    }

    if password.Length >= 12 {
        score += 1;
    }

    // Character type checks
    var hasUpper = false;
    var hasLower = false;
    var hasDigit = false;
    var hasSpecial = false;

    for i in 0..password.Length {
        var c = password[i];
        var code = c.code();

        if code >= 'A'.code() && code <= 'Z'.code() {
            hasUpper = true;
        } else if code >= 'a'.code() && code <= 'z'.code() {
            hasLower = true;
        } else if code >= '0'.code() && code <= '9'.code() {
            hasDigit = true;
        } else {
            hasSpecial = true;
        }
    }

    if hasUpper {
        score += 1;
    } else {
        feedback.Push("uppercase letter");
    }

    if hasLower {
        score += 1;
    } else {
        feedback.Push("lowercase letter");
    }

    if hasDigit {
        score += 1;
    } else {
        feedback.Push("digit");
    }

    if hasSpecial {
        score += 1;
    } else {
        feedback.Push("special character");
    }

    // Generate result
    if score >= 5 {
        return "Strong";
    } else if score >= 3 {
        return "Moderate - needs: " + feedback.Join(", ");
    } else {
        return "Weak - needs: " + feedback.Join(", ");
    }
}

bind Viper.Terminal;

Say(checkPasswordStrength("abc"));
// Weak - needs: at least 8 characters, uppercase letter, digit, special character

Say(checkPasswordStrength("MyPassword123!"));
// Strong
```

---

## Debugging String Problems

Strings often cause subtle bugs. Here are common issues and how to find them.

### Invisible Characters

Sometimes strings look identical but aren't:

```rust
bind Viper.Terminal;

var a = "hello";
var b = "hello ";  // Trailing space!

if a == b {
    Say("Equal");
} else {
    Say("Different!");  // This runs
}
```

**Debugging technique:** Print with visible boundaries:

```rust
bind Viper.Terminal;

func debugString(text: String) {
    Say("[" + text + "]");
    Say("Length: " + text.Length);

    // Show character codes
    for i in 0..text.Length {
        Say("  [" + i + "]: '" + text[i] + "' = " + text[i].code());
    }
}

debugString("hello ");
// [hello ]
// Length: 6
//   [0]: 'h' = 104
//   [1]: 'e' = 101
//   [2]: 'l' = 108
//   [3]: 'l' = 108
//   [4]: 'o' = 111
//   [5]: ' ' = 32     <- There's the extra space!
```

### Common Invisible Characters

- Space (32): ` `
- Tab (9): `\t`
- Newline (10): `\n`
- Carriage return (13): `\r`
- Non-breaking space (160): ` ` (looks like space but isn't!)

### Off-by-One Errors

The most common string bug:

```rust
bind Viper.Terminal;

var text = "Hello";
// text[5] does NOT exist - indices are 0-4

// Wrong:
for i in 0..text.Length + 1 {  // Goes to index 5!
    Say(text[i]);
}

// Right:
for i in 0..text.Length {      // Goes to index 4
    Say(text[i]);
}
```

**Key insight:** Length is 5, but the last valid index is 4. This is because we start counting at 0.

```rust
// The last character is always at index (length - 1)
var lastChar = text[text.Length - 1];
```

### Empty String Handling

Empty strings cause many bugs:

```rust
var text = "";

// This will fail:
var first = text[0];  // Error! No characters

// Always check first:
if text.Length > 0 {
    var first = text[0];
}
```

**Safe first character:**
```rust
func firstChar(text: String) -> String {
    if text.Length == 0 {
        return "";
    }
    return text.Substring(0, 1);
}
```

### Case Sensitivity Bugs

```rust
var filename = "Report.PDF";

// This fails - case doesn't match:
if filename.EndsWith(".pdf") {
    // Won't execute for "Report.PDF"
}

// Fix with case normalization:
if filename.ToLower().EndsWith(".pdf") {
    // Now it works
}
```

### Substring Index Confusion

Remember: `substring(start, length)`, not `substring(start, end)`:

```rust
var text = "Hello, World!";

// Get "World":
text.Substring(7, 5);    // Correct: start at 7, take 5 characters
// NOT text.Substring(7, 12)  // This would be start 7, take 12 characters!
```

---

## A Complete Example: Word Counter

Let's build a program that analyzes text:

```rust
module WordCounter;

bind Viper.Terminal;

func countWords(text: String) -> Integer {
    var words = text.Split(" ");
    var count = 0;

    for word in words {
        if word.Trim().Length > 0 {
            count += 1;
        }
    }

    return count;
}

func countVowels(text: String) -> Integer {
    var vowels = "aeiouAEIOU";
    var count = 0;

    for i in 0..text.Length {
        if vowels.Contains(text.Substring(i, 1)) {
            count += 1;
        }
    }

    return count;
}

func start() {
    Say("Enter some text:");
    var text = ReadLine();

    Say("");
    Say("=== Analysis ===");
    Say("Characters: " + text.Length);
    Say("Words: " + countWords(text));
    Say("Vowels: " + countVowels(text));

    Say("");
    Say("Uppercase: " + text.ToUpper());
    Say("Lowercase: " + text.ToLower());
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

## The Two Languages

**Zia**
```rust
bind Viper.Terminal;

var text = "Hello, World!";

// Length
Say(text.Length);

// Substring
var sub = text.Substring(0, 5);

// Search
var pos = text.IndexOf("World");

// Replace
var new = text.Replace("World", "Viper");

// Split and join
var parts = text.Split(", ");
var joined = parts.Join(" - ");

// Case
var upper = text.ToUpper();
var lower = text.ToLower();
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

---

## Common Mistakes

**Off-by-one with indexing:**
```rust
var text = "Hello";
var last = text[text.Length];      // Error! Index 5 doesn't exist
var last = text[text.Length - 1];  // Correct: index 4 is 'o'
```

**Forgetting strings are immutable:**
```rust
var text = "Hello";
text[0] = 'J';  // Error in many languages!
var text = "J" + text.Substring(1);  // Create a new string instead
```

Many languages (including Zia) don't let you modify characters in place. You create new strings instead.

**Comparing strings with wrong case:**
```rust
var input = "YES";
if input == "yes" {  // False! Case differs
    ...
}
if input.ToLower() == "yes" {  // Convert first
    ...
}
```

**Empty string vs. null:**
```rust
var empty = "";          // A string with zero characters
var text = "Hello";

if text.Length == 0 {    // Check if empty
    ...
}
```

**Confusing string concatenation with arithmetic:**
```rust
bind Viper.Terminal;
bind Viper.Convert as Convert;

var a = "5";
var b = "3";
Say(a + b);  // "53", not 8!

// Convert to numbers first for math:
Say(Convert.ToInt64(a) + Convert.ToInt64(b));  // 8
```

**Not trimming user input:**
```rust
bind Viper.Terminal;

var input = ReadLine();
// User might type "  yes  " with spaces

if input == "yes" {  // Fails!
    ...
}

if input.Trim() == "yes" {  // Works
    ...
}
```

---

## Summary

- **Strings are arrays of characters** — this explains all their behavior
- **Strings are immutable** — "modifying" creates new strings
- `.Length` gives the number of characters
- `+` concatenates strings (but use StringBuilder for loops)
- `substring`, `left`, `right`, `skip` extract portions
- `indexOf`, `lastIndexOf`, `contains`, `startsWith`, `endsWith` search
- `upper`, `lower` change case (essential for comparisons)
- `trim`, `trimStart`, `trimEnd` remove whitespace
- `replace` substitutes text
- `split` breaks into arrays; `join` combines arrays
- String comparison is case-sensitive and uses character codes
- `Viper.Core.Convert.ToInt64/ToDouble` convert strings to numbers
- `Viper.Fmt.format` creates formatted strings
- Always check for empty strings and off-by-one errors
- Use `trim()` on user input before processing

---

## Exercises

**Exercise 8.1**: Write a function that takes a string and returns it reversed. "hello" -> "olleh"

**Exercise 8.2**: Write a function that checks if a string is a palindrome (reads the same forward and backward, like "racecar"). Make it case-insensitive and ignore spaces.

**Exercise 8.3**: Write a program that asks for a sentence and prints each word on a separate line, with word numbers.

**Exercise 8.4**: Write a function that capitalizes the first letter of each word. "hello world" -> "Hello World"

**Exercise 8.5**: Write a program that counts how many times each vowel appears in a string.

**Exercise 8.6**: Write a function that validates a username: must be 3-20 characters, only letters, numbers, and underscores, cannot start with a number.

**Exercise 8.7** (Challenge): Write a simple "find and replace" program that asks for text, a word to find, and a word to replace it with, then shows the result. Make it case-insensitive.

**Exercise 8.8** (Challenge): Write a program that takes a string and removes all duplicate spaces (so "hello   world" becomes "hello world").

**Exercise 8.9** (Challenge): Write a function that formats a phone number. Given "5551234567", return "(555) 123-4567". Handle input with various formats (spaces, dashes, etc.).

**Exercise 8.10** (Challenge): Write a CSV parser that handles quoted fields. The string `'Alice,"Smith, Jr.",30'` should split into three fields: `["Alice", "Smith, Jr.", "30"]`.

---

*We can now work with text. But what about text stored in files? Next, we learn to read and write persistent data.*

*[Continue to Chapter 9: Files and Persistence ->](09-files.md)*
