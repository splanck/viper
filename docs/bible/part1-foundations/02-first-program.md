# Chapter 2: Your First Program

Every programming book starts with "Hello, World." It's tradition, dating back to 1978. But we're not going to just show you the code — we're going to understand every piece of it.

---

## The Program

Create a new file called `hello.viper` and type this:

```viper
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
```

Save it, then run it:

```bash
viper hello.viper
```

You'll see:

```
Hello, World!
```

Congratulations — you've written a program! But what did you actually write? Let's break it down.

---

## Line by Line

### `module Hello;`

This line names your program. In ViperLang, every program is a *module* — a self-contained unit of code. The name `Hello` could be anything: `MyProgram`, `Test`, `FirstTry`. The semicolon marks the end of this statement.

Think of a module like a chapter in a book. It has a title, and everything that follows belongs to that chapter. Later, when your programs get bigger, you'll split them into multiple modules. For now, one is enough.

### `func start() {`

This line defines a *function* called `start`. A function is a named sequence of instructions.

- `func` is the keyword that means "I'm defining a function"
- `start` is the name of this function
- `()` means this function doesn't need any extra information to run (we'll add things here later)
- `{` opens the body of the function — the instructions inside

The name `start` is special. When you run a ViperLang program, the computer looks for a function called `start` and runs it. It's the entry point — where your program begins.

### `    Viper.Terminal.Say("Hello, World!");`

This is the instruction that actually does something. It tells the computer to display text on the screen.

- `Viper.Terminal` is the *standard library's* terminal module — a collection of functions for working with the console
- `.Say` is a function in that module that displays text
- `("Hello, World!")` is the text we want to display, passed to the function
- The semicolon ends the statement

The indentation (four spaces) isn't required, but it's conventional. It shows visually that this line is *inside* the function.

### `}`

This closes the function. Everything between `{` and `}` is the body of the function.

---

## What Happened When You Ran It

When you typed `viper hello.viper`, here's what happened:

1. The Viper compiler read your file
2. It checked that your code was valid (no typos, correct structure)
3. It translated your code into instructions the computer understands
4. It ran those instructions
5. The `start` function executed
6. The `Say` function displayed "Hello, World!" on your screen
7. The `start` function ended
8. Your program finished

All of that happened in a fraction of a second.

---

## The Same Program in BASIC and Pascal

The same idea, different syntax:

**BASIC**
```basic
PRINT "Hello, World!"
```

BASIC is more concise for simple programs. There's no explicit module or function — the whole file is implicitly the program.

**Pascal**
```pascal
program Hello;
begin
    WriteLn('Hello, World!');
end.
```

Pascal uses `program` instead of `module`, `begin`/`end` instead of `{`/`}`, and `WriteLn` instead of `Say`.

Notice what's the same across all three:
- A name for the program
- A way to mark where code begins and ends
- A function to display text
- The text to display, in quotes

These are the concepts. The syntax is just how each language spells them.

---

## Experimenting

Change the text to something else:

```viper
Viper.Terminal.Say("I wrote this!");
```

Run it again. See your new message appear.

Now try displaying two messages:

```viper
module Hello;

func start() {
    Viper.Terminal.Say("Line one");
    Viper.Terminal.Say("Line two");
}
```

Each `Say` displays one line. The program runs them in order, top to bottom.

What if you want multiple things on the same line? Use `Print` instead of `Say`:

```viper
module Hello;

func start() {
    Viper.Terminal.Print("One ");
    Viper.Terminal.Print("Two ");
    Viper.Terminal.Say("Three");
}
```

Output:
```
One Two Three
```

`Print` outputs without a newline; `Say` adds a newline at the end.

---

## When Things Go Wrong

Let's break our program deliberately. Type this:

```viper
module Hello;

func start() {
    Viper.Terminal.Say("Oops)
}
```

We forgot the closing quote. Try to run it:

```
hello.viper:4:31: error: unterminated string literal
    Viper.Terminal.Say("Oops)
                              ^
```

The compiler caught our mistake. It tells us:
- The file: `hello.viper`
- The line: `4`
- The position: `31`
- The problem: `unterminated string literal` (a string that wasn't closed)
- It even shows the exact line and points to where things went wrong

This is your first error message. Get used to them — they're your friends. They tell you exactly what went wrong and where. Learning to read error messages is a crucial skill.

Let's try another mistake:

```viper
module Hello;

func start() {
    Viper.Terminal.Say("Hello")
}
```

We forgot the semicolon. The error might say:

```
hello.viper:5:1: error: expected ';' before '}'
}
^
```

Again, the compiler tells us what it expected and where.

---

## Key Concepts

**Modules** organize code into named units.

**Functions** are named sequences of instructions. The `start` function is special — it's where the program begins.

**Statements** are individual instructions, ending with semicolons.

**Strings** are text enclosed in quotes: `"like this"`.

**The standard library** provides ready-made functions like `Say` and `Print`.

**Error messages** tell you what's wrong and where. Read them carefully.

---

## Exercises

**Exercise 2.1**: Write a program that displays your name.

**Exercise 2.2**: Write a program that displays three lines: your name, your city, and your favorite food. Each on its own line.

**Exercise 2.3**: Write the "Hello, World!" program in BASIC. Save it as `hello.bas` and run it with `vbasic hello.bas`.

**Exercise 2.4**: Make an error on purpose (missing quote, missing semicolon, misspelled function name). Read the error message. Can you understand what it's telling you?

**Exercise 2.5** (Challenge): Using only what you've learned so far, try to display this exact output:

```
***********
*  HELLO  *
***********
```

Hint: You'll need multiple `Say` calls.

---

*You've written and run a program. But it always does the same thing. Next, we'll learn how to work with data that can change.*

*[Continue to Chapter 3: Values and Names →](03-values-and-names.md)*
