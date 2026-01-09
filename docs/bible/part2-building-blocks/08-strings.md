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
var word = "Hello";
Viper.Terminal.Say(word[0]);  // H
Viper.Terminal.Say(word[4]);  // o
Viper.Terminal.Say(word.length);  // 5
```

This should feel familiar — strings behave like arrays of characters because that's exactly what they are.

---

## Character-Level Operations

Before diving into string operations, let's master working with individual characters. This fundamental skill underlies everything else.

### Accessing Characters by Index

Every character in a string has a position (index), starting from 0:

```rust
var name = "Alice";
Viper.Terminal.Say(name[0]);  // A (first character)
Viper.Terminal.Say(name[1]);  // l (second character)
Viper.Terminal.Say(name[4]);  // e (fifth/last character)
```

You can use any expression as an index:

```rust
var text = "Programming";
var middle = text.length / 2;
Viper.Terminal.Say(text[middle]);  // Prints the middle character

var lastIndex = text.length - 1;
Viper.Terminal.Say(text[lastIndex]);  // g (last character)
```

### Iterating Through Strings

To process every character, loop through the indices:

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

This pattern is fundamental. Want to count uppercase letters? Loop through each character and check. Want to find all positions of a letter? Loop and record indices where it matches.

```rust
// Count uppercase letters
func countUppercase(text: string) -> i64 {
    var count = 0;
    for i in 0..text.length {
        var c = text[i];
        if c.code() >= 'A'.code() && c.code() <= 'Z'.code() {
            count += 1;
        }
    }
    return count;
}

Viper.Terminal.Say(countUppercase("Hello World"));  // 2 (H and W)
```

### Building Strings Character by Character

You can construct strings by starting empty and adding characters:

```rust
// Reverse a string
func reverse(text: string) -> string {
    var result = "";
    for i in 0..text.length {
        // Add characters from the end to the beginning
        result = result + text[text.length - 1 - i];
    }
    return result;
}

Viper.Terminal.Say(reverse("Hello"));  // olleH
```

Or filter characters based on criteria:

```rust
// Keep only letters
func lettersOnly(text: string) -> string {
    var result = "";
    for i in 0..text.length {
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

Viper.Terminal.Say(lettersOnly("Hello, World! 123"));  // HelloWorld
```

---

## String Length

The `.length` property tells you how many characters a string contains:

```rust
Viper.Terminal.Say("Hello".length);     // 5
Viper.Terminal.Say("".length);          // 0 (empty string)
Viper.Terminal.Say("Hi there!".length); // 9 (space counts)
```

**Every character counts** — letters, numbers, spaces, punctuation, even invisible characters like tabs and newlines:

```rust
Viper.Terminal.Say("A B".length);    // 3 (A, space, B)
Viper.Terminal.Say("A\tB".length);   // 3 (A, tab, B)
Viper.Terminal.Say("A\nB".length);   // 3 (A, newline, B)
```

### Why Length Matters

Length is essential for loops:

```rust
var text = "Viper";
for i in 0..text.length {
    Viper.Terminal.Say("Character " + i + ": " + text[i]);
}
```

And for validation:

```rust
func validatePassword(password: string) -> bool {
    if password.length < 8 {
        Viper.Terminal.Say("Password must be at least 8 characters");
        return false;
    }
    if password.length > 128 {
        Viper.Terminal.Say("Password too long");
        return false;
    }
    return true;
}
```

And for safe indexing:

```rust
var text = "Hello";
var index = 10;

// Always check before accessing
if index >= 0 && index < text.length {
    Viper.Terminal.Say(text[index]);
} else {
    Viper.Terminal.Say("Index out of bounds");
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
var username = "Alice";
someFunction(username);  // Cannot modify our string
Viper.Terminal.Say(username);  // Still "Alice", guaranteed
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
text = "J" + text.substring(1);  // Creates new string "Jello"
// The old "Hello" string still exists (until garbage collected)
```

Think of it like editing a document on paper versus on a computer. With paper (mutable), you erase and write over. With strings (immutable), you create a fresh copy with changes.

```rust
var name = "alice";
var properName = name[0].upper() + name.substring(1);
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

### Concatenation Order Matters

Because conversion happens automatically, order can be surprising:

```rust
// String first: concatenation
Viper.Terminal.Say("Score: " + 5 + 3);   // Score: 53 (string concat)

// Numbers first: arithmetic then concat
Viper.Terminal.Say(5 + 3 + " points");   // 8 points (addition first)

// Use parentheses to be explicit
Viper.Terminal.Say("Total: " + (5 + 3)); // Total: 8
```

The rule: operations are evaluated left to right. Once you hit a string, everything after becomes concatenation.

### Building Strings with Concatenation

Concatenation is intuitive for simple cases:

```rust
func formatName(first: string, middle: string, last: string) -> string {
    return first + " " + middle + " " + last;
}

Viper.Terminal.Say(formatName("John", "Fitzgerald", "Kennedy"));
// John Fitzgerald Kennedy
```

But for many operations, it can be slow due to immutability. We'll cover better patterns later.

---

## Substrings: Extracting Parts

Often you need just part of a string. The `substring` method extracts a portion:

```rust
var text = "Hello, World!";
var hello = text.substring(0, 5);    // "Hello" (from 0, length 5)
var world = text.substring(7, 5);    // "World" (from 7, length 5)
```

The first argument is the starting position, the second is how many characters to take.

### Understanding Substring Parameters

Think of `substring(start, length)` as answering: "Starting at position `start`, give me `length` characters."

```rust
var text = "ABCDEFGHIJ";
//          0123456789

text.substring(0, 3);   // "ABC" - from position 0, take 3
text.substring(3, 4);   // "DEFG" - from position 3, take 4
text.substring(7, 3);   // "HIJ" - from position 7, take 3
```

### What If You Request Too Many Characters?

If you request more characters than exist, you get what's available:

```rust
var text = "Hi";
text.substring(0, 10);  // "Hi" - only 2 characters exist
text.substring(1, 10);  // "i" - only 1 character from position 1
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
func getExtension(filename: string) -> string {
    var dotPos = filename.lastIndexOf(".");
    if dotPos == -1 {
        return "";  // No extension
    }
    return filename.skip(dotPos + 1);
}

Viper.Terminal.Say(getExtension("report.pdf"));    // pdf
Viper.Terminal.Say(getExtension("archive.tar.gz")); // gz
Viper.Terminal.Say(getExtension("README"));         // (empty)
```

**Extract domain from email:**
```rust
func getEmailDomain(email: string) -> string {
    var atPos = email.indexOf("@");
    if atPos == -1 {
        return "";  // Not a valid email
    }
    return email.skip(atPos + 1);
}

Viper.Terminal.Say(getEmailDomain("user@example.com"));  // example.com
```

**Truncate with ellipsis:**
```rust
func truncate(text: string, maxLength: i64) -> string {
    if text.length <= maxLength {
        return text;
    }
    return text.left(maxLength - 3) + "...";
}

Viper.Terminal.Say(truncate("Hello, World!", 10));  // Hello, ...
Viper.Terminal.Say(truncate("Hi", 10));             // Hi
```

---

## Searching in Strings

Finding text within text is one of the most common string operations.

### indexOf: Finding Position

`indexOf` returns the position of the first occurrence, or -1 if not found:

```rust
var text = "Hello, World!";
var pos = text.indexOf("World");  // 7
var notFound = text.indexOf("xyz");  // -1 (not found)
```

Why -1 for "not found"? Because 0 is a valid position (the start of the string), we need a different value to indicate failure. Negative numbers can't be valid positions, so -1 is the universal convention.

### Using indexOf Results

Always check if the search succeeded:

```rust
var email = "user@example.com";
var atPos = email.indexOf("@");

if atPos == -1 {
    Viper.Terminal.Say("Invalid email: no @ symbol");
} else {
    var username = email.left(atPos);
    var domain = email.skip(atPos + 1);
    Viper.Terminal.Say("Username: " + username);  // user
    Viper.Terminal.Say("Domain: " + domain);      // example.com
}
```

### lastIndexOf: Finding the Last Occurrence

When a substring appears multiple times, `lastIndexOf` finds the last one:

```rust
var path = "/home/user/documents/file.txt";
var lastSlash = path.lastIndexOf("/");  // 20
var filename = path.skip(lastSlash + 1);  // file.txt
```

### contains: Simple Presence Check

When you only care whether something exists, not where:

```rust
var text = "The quick brown fox";

if text.contains("quick") {
    Viper.Terminal.Say("Found it!");
}
```

This is cleaner than checking `indexOf(...) != -1`.

### startsWith and endsWith

Check the beginning or end of a string:

```rust
var filename = "report.pdf";

if filename.endsWith(".pdf") {
    Viper.Terminal.Say("It's a PDF file");
}

if filename.startsWith("report") {
    Viper.Terminal.Say("It's a report");
}
```

**File type detection:**
```rust
func getFileType(filename: string) -> string {
    if filename.endsWith(".jpg") || filename.endsWith(".png") {
        return "image";
    }
    if filename.endsWith(".mp3") || filename.endsWith(".wav") {
        return "audio";
    }
    if filename.endsWith(".txt") || filename.endsWith(".md") {
        return "text";
    }
    return "unknown";
}
```

**URL validation:**
```rust
func isSecureUrl(url: string) -> bool {
    return url.startsWith("https://");
}
```

### Finding All Occurrences

`indexOf` only finds the first occurrence. To find all:

```rust
func findAll(text: string, search: string) -> array<i64> {
    var positions = [];
    var pos = 0;

    while pos < text.length {
        var found = text.indexOf(search, pos);  // Start search from pos
        if found == -1 {
            break;
        }
        positions.push(found);
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
var a = "hello";
var b = "hello";
var c = "Hello";

Viper.Terminal.Say(a == b);  // true - exact match
Viper.Terminal.Say(a == c);  // false - case differs!
```

### Case Sensitivity

String comparison is **case-sensitive** by default. "Hello" and "hello" are different strings:

```rust
var input = "YES";
if input == "yes" {  // false!
    Viper.Terminal.Say("Match");
}
```

For case-insensitive comparison, convert to the same case first:

```rust
var input = "YES";
if input.lower() == "yes" {  // true!
    Viper.Terminal.Say("User said yes");
}
```

**Common pattern for user input:**
```rust
func normalizeInput(text: string) -> string {
    return text.trim().lower();
}

var input = "  YES  ";
if normalizeInput(input) == "yes" {
    Viper.Terminal.Say("Confirmed");
}
```

### Alphabetical Ordering

Strings can be compared alphabetically using `<`, `>`, `<=`, `>=`:

```rust
Viper.Terminal.Say("apple" < "banana");  // true (a before b)
Viper.Terminal.Say("cat" < "car");       // false (t after r)
Viper.Terminal.Say("apple" < "apricot"); // true (p before r)
```

This is called **lexicographic ordering** — comparing character by character until a difference is found.

### The ASCII Trap

Alphabetical comparison uses character codes, which leads to surprises:

```rust
// All uppercase letters come BEFORE all lowercase
Viper.Terminal.Say("Z" < "a");     // true! Z (90) < a (97)
Viper.Terminal.Say("Apple" < "banana"); // true (A < b)
Viper.Terminal.Say("apple" < "Banana"); // false (a > B)
```

For true alphabetical sorting, convert to the same case:

```rust
func alphabeticallyBefore(a: string, b: string) -> bool {
    return a.lower() < b.lower();
}

Viper.Terminal.Say(alphabeticallyBefore("apple", "Banana"));  // true
```

### Numeric Strings Don't Sort Numerically

Another common trap:

```rust
Viper.Terminal.Say("9" < "10");   // false! '9' (57) > '1' (49)
Viper.Terminal.Say("9" < "100");  // false! Same reason
```

For numeric comparison, convert to numbers:

```rust
var a = "9";
var b = "10";
if Viper.Parse.Int(a) < Viper.Parse.Int(b) {
    Viper.Terminal.Say("9 is less than 10");  // Correct!
}
```

### Comparing Empty Strings

```rust
var empty = "";
var space = " ";

Viper.Terminal.Say(empty == "");     // true
Viper.Terminal.Say(empty == space);  // false (space is a character)
Viper.Terminal.Say(empty.length == 0); // true
Viper.Terminal.Say(space.length == 0); // false
```

---

## Case Conversion

Strings can be converted to all uppercase or all lowercase:

```rust
var text = "Hello, World!";
Viper.Terminal.Say(text.upper());  // HELLO, WORLD!
Viper.Terminal.Say(text.lower());  // hello, world!
```

Remember: the original string is unchanged (immutability):

```rust
var name = "Alice";
Viper.Terminal.Say(name.upper());  // ALICE
Viper.Terminal.Say(name);          // Alice (still)
```

### Case-Insensitive Comparisons

The most common use of case conversion:

```rust
var input = "YES";

if input.lower() == "yes" {
    Viper.Terminal.Say("User said yes!");
}
```

Without `.lower()`, "YES", "Yes", "yes", and "yEs" would all be different. Converting to lowercase first makes them equal.

### Capitalizing Names

Combine case conversion with substrings:

```rust
func capitalize(text: string) -> string {
    if text.length == 0 {
        return "";
    }
    return text[0].upper() + text.skip(1).lower();
}

Viper.Terminal.Say(capitalize("jOHN"));   // John
Viper.Terminal.Say(capitalize("ALICE"));  // Alice
```

**Title case (capitalize each word):**
```rust
func titleCase(text: string) -> string {
    var words = text.split(" ");
    var result = [];

    for word in words {
        result.push(capitalize(word));
    }

    return result.join(" ");
}

Viper.Terminal.Say(titleCase("the quick brown fox"));
// The Quick Brown Fox
```

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

### What Counts as Whitespace?

- Spaces (' ')
- Tabs ('\t')
- Newlines ('\n')
- Carriage returns ('\r')

```rust
var messy = "\t  Hello World  \n";
Viper.Terminal.Say("[" + messy.trim() + "]");  // [Hello World]
```

### Essential for User Input

Always trim user input before processing:

```rust
Viper.Terminal.Say("Enter your name:");
var name = Viper.Terminal.ReadLine().trim();

if name.length == 0 {
    Viper.Terminal.Say("Name cannot be empty");
}
```

Without trimming, a user who types "   " (just spaces) would pass a length check but cause problems later.

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

### Common Use Cases

**Sanitizing input:**
```rust
func sanitize(text: string) -> string {
    var result = text;
    result = result.replace("<", "&lt;");
    result = result.replace(">", "&gt;");
    result = result.replace("&", "&amp;");
    return result;
}
```

**Normalizing data:**
```rust
// Remove common variations in phone numbers
func normalizePhone(phone: string) -> string {
    var result = phone;
    result = result.replace(" ", "");
    result = result.replace("-", "");
    result = result.replace("(", "");
    result = result.replace(")", "");
    result = result.replace("+", "");
    return result;
}

Viper.Terminal.Say(normalizePhone("+1 (555) 123-4567"));  // 15551234567
```

**Template substitution:**
```rust
func greet(template: string, name: string, time: string) -> string {
    var result = template;
    result = result.replace("{name}", name);
    result = result.replace("{time}", time);
    return result;
}

var template = "Good {time}, {name}! Welcome back.";
Viper.Terminal.Say(greet(template, "Alice", "morning"));
// Good morning, Alice! Welcome back.
```

### Replace Is Case-Sensitive

```rust
var text = "Hello hello HELLO";
Viper.Terminal.Say(text.replace("hello", "hi"));
// Hello hi HELLO (only exact match replaced)
```

For case-insensitive replace, you need a more complex approach:

```rust
func replaceIgnoreCase(text: string, search: string, replacement: string) -> string {
    var result = "";
    var i = 0;
    var searchLower = search.lower();

    while i < text.length {
        // Check if search string matches at current position
        if i + search.length <= text.length {
            var segment = text.substring(i, search.length);
            if segment.lower() == searchLower {
                result = result + replacement;
                i = i + search.length;
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

### Understanding Split

Split finds every occurrence of the delimiter and breaks the string there:

```rust
var text = "one::two::three";
var parts = text.split("::");  // ["one", "two", "three"]
```

The delimiter itself is removed — you get everything *between* the delimiters.

### Empty Strings in Split Results

Watch out for edge cases:

```rust
var text = "a,,b";
var parts = text.split(",");  // ["a", "", "b"]
// The empty string between the two commas is preserved
```

```rust
var text = ",a,b,";
var parts = text.split(",");  // ["", "a", "b", ""]
// Leading/trailing delimiters create empty strings
```

### Common Split Patterns

**Split by space (words):**
```rust
var sentence = "The quick brown fox";
var words = sentence.split(" ");  // ["The", "quick", "brown", "fox"]
```

**Split by newline (lines):**
```rust
var text = "line one\nline two\nline three";
var lines = text.split("\n");
```

**Split by multiple characters:**
```rust
var data = "name=Alice&age=30&city=Boston";
var pairs = data.split("&");  // ["name=Alice", "age=30", "city=Boston"]
```

### Parsing CSV Data

CSV (Comma-Separated Values) is extremely common:

```rust
func parseCSVLine(line: string) -> array<string> {
    return line.split(",");
}

var line = "Alice,30,Engineer,Boston";
var fields = parseCSVLine(line);

Viper.Terminal.Say("Name: " + fields[0]);       // Alice
Viper.Terminal.Say("Age: " + fields[1]);        // 30
Viper.Terminal.Say("Job: " + fields[2]);        // Engineer
Viper.Terminal.Say("City: " + fields[3]);       // Boston
```

**Processing multiple lines:**
```rust
var csvData = "Alice,30,Boston\nBob,25,Seattle\nCarol,35,Denver";
var lines = csvData.split("\n");

for line in lines {
    var fields = line.split(",");
    var name = fields[0];
    var age = fields[1];
    var city = fields[2];
    Viper.Terminal.Say(name + " is " + age + " years old, lives in " + city);
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

### Split and Join Together

Split and join are complementary operations:

```rust
var text = "Hello, World!";
var parts = text.split(", ");  // ["Hello", "World!"]
var rejoined = parts.join(", "); // "Hello, World!"
```

**Transform and rejoin:**
```rust
// Capitalize each word
func titleCase(text: string) -> string {
    var words = text.split(" ");
    var capitalized = [];

    for word in words {
        if word.length > 0 {
            var cap = word[0].upper() + word.skip(1).lower();
            capitalized.push(cap);
        }
    }

    return capitalized.join(" ");
}

Viper.Terminal.Say(titleCase("the QUICK brown FOX"));
// The Quick Brown Fox
```

**Change delimiter:**
```rust
// Convert paths between systems
func windowsToUnix(path: string) -> string {
    return path.split("\\").join("/");
}

Viper.Terminal.Say(windowsToUnix("C:\\Users\\Alice\\Documents"));
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
var builder = Viper.StringBuilder.new();

for i in 0..1000 {
    builder.append("x");
}

var result = builder.toString();
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
func generateReport(data: array<Record>) -> string {
    var sb = Viper.StringBuilder.new();

    sb.append("=== Report ===\n");
    sb.append("Generated: " + Viper.Time.now() + "\n");
    sb.append("\n");

    for record in data {
        sb.append("Name: " + record.name + "\n");
        sb.append("Score: " + record.score + "\n");
        sb.append("---\n");
    }

    sb.append("\nTotal records: " + data.length);

    return sb.toString();
}
```

### Alternative: Collect and Join

Another efficient pattern is to collect pieces in an array and join at the end:

```rust
func buildList(items: array<string>) -> string {
    var lines = [];

    for i in 0..items.length {
        lines.push((i + 1) + ". " + items[i]);
    }

    return lines.join("\n");
}

var items = ["Apple", "Banana", "Cherry"];
Viper.Terminal.Say(buildList(items));
// 1. Apple
// 2. Banana
// 3. Cherry
```

This avoids repeated concatenation and is often cleaner than StringBuilder.

---

## Converting Between Strings and Numbers

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

### The Classic Trap

Be careful — `"5" + 3` is `"53"`, not `8`:

```rust
var input = "5";
Viper.Terminal.Say(input + 3);  // "53" (string concatenation!)
Viper.Terminal.Say(Viper.Parse.Int(input) + 3);  // 8 (arithmetic)
```

If you want arithmetic, convert to numbers first.

### Handling Invalid Input

What if the string isn't a valid number?

```rust
var result = Viper.Parse.Int("abc");  // Error or NaN

// Safe approach with validation
func safeParseInt(text: string) -> i64 {
    // Check if string contains only digits
    for i in 0..text.length {
        var c = text[i];
        if c.code() < '0'.code() || c.code() > '9'.code() {
            return -1;  // Invalid
        }
    }
    return Viper.Parse.Int(text);
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
func isDigit(c: char) -> bool {
    return c.code() >= '0'.code() && c.code() <= '9'.code();
}

func isUppercase(c: char) -> bool {
    return c.code() >= 'A'.code() && c.code() <= 'Z'.code();
}

func isLowercase(c: char) -> bool {
    return c.code() >= 'a'.code() && c.code() <= 'z'.code();
}

func isLetter(c: char) -> bool {
    return isUppercase(c) || isLowercase(c);
}

func isAlphanumeric(c: char) -> bool {
    return isLetter(c) || isDigit(c);
}
```

### Character Conversion

```rust
func toUpper(c: char) -> char {
    if c.code() >= 'a'.code() && c.code() <= 'z'.code() {
        return Char.fromCode(c.code() - 32);
    }
    return c;
}

func toLower(c: char) -> char {
    if c.code() >= 'A'.code() && c.code() <= 'Z'.code() {
        return Char.fromCode(c.code() + 32);
    }
    return c;
}
```

### Digit Values

```rust
// Get numeric value of a digit character
func digitValue(c: char) -> i64 {
    if c.code() >= '0'.code() && c.code() <= '9'.code() {
        return c.code() - '0'.code();
    }
    return -1;  // Not a digit
}

Viper.Terminal.Say(digitValue('7'));  // 7
Viper.Terminal.Say(digitValue('a'));  // -1
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

### Format Specifiers

You can control formatting:

```rust
var pi = 3.14159265;
Viper.Terminal.Say(Viper.Fmt.format("Pi is approximately {:.2}", pi));
// "Pi is approximately 3.14"
```

The `:.2` means "2 decimal places." We'll explore formatting options more in Chapter 13 when we cover the standard library in depth.

### Padding and Alignment

```rust
// Right-align in 10 characters
Viper.Fmt.format("{:>10}", "hi");  // "        hi"

// Left-align in 10 characters
Viper.Fmt.format("{:<10}", "hi");  // "hi        "

// Center in 10 characters
Viper.Fmt.format("{:^10}", "hi");  // "    hi    "

// Pad with zeros
Viper.Fmt.format("{:05}", 42);     // "00042"
```

---

## Practical Text Processing

Let's apply everything to real-world problems.

### Validating Email Addresses

A basic email validator:

```rust
func isValidEmail(email: string) -> bool {
    var trimmed = email.trim();

    // Must contain exactly one @
    var atPos = trimmed.indexOf("@");
    if atPos == -1 {
        return false;  // No @
    }
    if trimmed.lastIndexOf("@") != atPos {
        return false;  // Multiple @s
    }

    // @ cannot be first or last
    if atPos == 0 || atPos == trimmed.length - 1 {
        return false;
    }

    // Must have a dot after @
    var domain = trimmed.skip(atPos + 1);
    var dotPos = domain.indexOf(".");
    if dotPos == -1 || dotPos == 0 || dotPos == domain.length - 1 {
        return false;
    }

    return true;
}

Viper.Terminal.Say(isValidEmail("user@example.com"));   // true
Viper.Terminal.Say(isValidEmail("invalid"));            // false
Viper.Terminal.Say(isValidEmail("no@dots"));            // false
Viper.Terminal.Say(isValidEmail("two@@signs.com"));     // false
```

### Formatting Names

```rust
func formatName(fullName: string) -> string {
    var trimmed = fullName.trim();

    // Handle empty input
    if trimmed.length == 0 {
        return "";
    }

    // Split into parts
    var parts = trimmed.split(" ");

    // Filter out empty parts (multiple spaces)
    var cleanParts = [];
    for part in parts {
        if part.length > 0 {
            cleanParts.push(part);
        }
    }

    // Capitalize each part
    var formatted = [];
    for part in cleanParts {
        var cap = part[0].upper() + part.skip(1).lower();
        formatted.push(cap);
    }

    return formatted.join(" ");
}

Viper.Terminal.Say(formatName("  john   SMITH  "));  // John Smith
Viper.Terminal.Say(formatName("ALICE"));             // Alice
Viper.Terminal.Say(formatName("bob jones jr"));      // Bob Jones Jr
```

### Parsing Key-Value Data

```rust
func parseKeyValue(text: string) -> Map<string, string> {
    var result = Map.new();
    var pairs = text.split("&");

    for pair in pairs {
        var eqPos = pair.indexOf("=");
        if eqPos != -1 {
            var key = pair.left(eqPos).trim();
            var value = pair.skip(eqPos + 1).trim();
            result.set(key, value);
        }
    }

    return result;
}

var data = "name=Alice&age=30&city=Boston";
var parsed = parseKeyValue(data);
Viper.Terminal.Say(parsed.get("name"));  // Alice
Viper.Terminal.Say(parsed.get("age"));   // 30
```

### Cleaning User Input

```rust
func cleanInput(text: string) -> string {
    var result = text.trim();

    // Normalize whitespace (collapse multiple spaces)
    while result.contains("  ") {
        result = result.replace("  ", " ");
    }

    return result;
}

Viper.Terminal.Say(cleanInput("  hello    world  "));  // "hello world"
```

### Password Strength Checker

```rust
func checkPasswordStrength(password: string) -> string {
    var score = 0;
    var feedback = [];

    // Length check
    if password.length >= 8 {
        score += 1;
    } else {
        feedback.push("at least 8 characters");
    }

    if password.length >= 12 {
        score += 1;
    }

    // Character type checks
    var hasUpper = false;
    var hasLower = false;
    var hasDigit = false;
    var hasSpecial = false;

    for i in 0..password.length {
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
        feedback.push("uppercase letter");
    }

    if hasLower {
        score += 1;
    } else {
        feedback.push("lowercase letter");
    }

    if hasDigit {
        score += 1;
    } else {
        feedback.push("digit");
    }

    if hasSpecial {
        score += 1;
    } else {
        feedback.push("special character");
    }

    // Generate result
    if score >= 5 {
        return "Strong";
    } else if score >= 3 {
        return "Moderate - needs: " + feedback.join(", ");
    } else {
        return "Weak - needs: " + feedback.join(", ");
    }
}

Viper.Terminal.Say(checkPasswordStrength("abc"));
// Weak - needs: at least 8 characters, uppercase letter, digit, special character

Viper.Terminal.Say(checkPasswordStrength("MyPassword123!"));
// Strong
```

---

## Debugging String Problems

Strings often cause subtle bugs. Here are common issues and how to find them.

### Invisible Characters

Sometimes strings look identical but aren't:

```rust
var a = "hello";
var b = "hello ";  // Trailing space!

if a == b {
    Viper.Terminal.Say("Equal");
} else {
    Viper.Terminal.Say("Different!");  // This runs
}
```

**Debugging technique:** Print with visible boundaries:

```rust
func debugString(text: string) {
    Viper.Terminal.Say("[" + text + "]");
    Viper.Terminal.Say("Length: " + text.length);

    // Show character codes
    for i in 0..text.length {
        Viper.Terminal.Say("  [" + i + "]: '" + text[i] + "' = " + text[i].code());
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
var text = "Hello";
// text[5] does NOT exist - indices are 0-4

// Wrong:
for i in 0..text.length + 1 {  // Goes to index 5!
    Viper.Terminal.Say(text[i]);
}

// Right:
for i in 0..text.length {      // Goes to index 4
    Viper.Terminal.Say(text[i]);
}
```

**Key insight:** Length is 5, but the last valid index is 4. This is because we start counting at 0.

```rust
// The last character is always at index (length - 1)
var lastChar = text[text.length - 1];
```

### Empty String Handling

Empty strings cause many bugs:

```rust
var text = "";

// This will fail:
var first = text[0];  // Error! No characters

// Always check first:
if text.length > 0 {
    var first = text[0];
}
```

**Safe first character:**
```rust
func firstChar(text: string) -> string {
    if text.length == 0 {
        return "";
    }
    return text[0].toString();
}
```

### Case Sensitivity Bugs

```rust
var filename = "Report.PDF";

// This fails - case doesn't match:
if filename.endsWith(".pdf") {
    // Won't execute for "Report.PDF"
}

// Fix with case normalization:
if filename.lower().endsWith(".pdf") {
    // Now it works
}
```

### Substring Index Confusion

Remember: `substring(start, length)`, not `substring(start, end)`:

```rust
var text = "Hello, World!";

// Get "World":
text.substring(7, 5);    // Correct: start at 7, take 5 characters
// NOT text.substring(7, 12)  // This would be start 7, take 12 characters!
```

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

**Confusing string concatenation with arithmetic:**
```rust
var a = "5";
var b = "3";
Viper.Terminal.Say(a + b);  // "53", not 8!

// Convert to numbers first for math:
Viper.Terminal.Say(Viper.Parse.Int(a) + Viper.Parse.Int(b));  // 8
```

**Not trimming user input:**
```rust
var input = Viper.Terminal.ReadLine();
// User might type "  yes  " with spaces

if input == "yes" {  // Fails!
    ...
}

if input.trim() == "yes" {  // Works
    ...
}
```

---

## Summary

- **Strings are arrays of characters** — this explains all their behavior
- **Strings are immutable** — "modifying" creates new strings
- `.length` gives the number of characters
- `+` concatenates strings (but use StringBuilder for loops)
- `substring`, `left`, `right`, `skip` extract portions
- `indexOf`, `lastIndexOf`, `contains`, `startsWith`, `endsWith` search
- `upper`, `lower` change case (essential for comparisons)
- `trim`, `trimStart`, `trimEnd` remove whitespace
- `replace` substitutes text
- `split` breaks into arrays; `join` combines arrays
- String comparison is case-sensitive and uses character codes
- `Viper.Parse.Int/Float` convert strings to numbers
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
