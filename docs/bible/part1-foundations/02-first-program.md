# Chapter 2: Your First Program

This is it. The moment you've been building toward. You're about to write your very first computer program.

For most people, this chapter marks one of the most significant moments in their journey with technology. You're crossing a threshold that separates people who *use* computers from people who *tell computers what to do*. After this chapter, you'll never look at software quite the same way again.

Let's make it happen.

---

## Before We Begin: Setting Up Your Workspace

Before we can write our first program, we need to make sure you have a comfortable place to work. Think of this like setting up an artist's studio before painting — having the right tools arranged properly will make everything that follows much smoother.

### Creating a Project Folder

Every programmer needs a home for their code. Let's create one:

1. **On Windows**: Open File Explorer, navigate to your Documents folder, and create a new folder called `viper-projects`

2. **On macOS**: Open Finder, go to your home folder, and create a new folder called `viper-projects`

3. **On Linux**: Open your file manager or use `mkdir ~/viper-projects` in the terminal

This will be where all your programs live as you work through this book. Keeping your code organized from the start is a habit that will serve you well.

### Setting Up Your Text Editor

If you haven't already, open the text editor you chose in the Getting Started chapter. Take a moment to:

1. **Increase the font size** if it feels small. Programming involves staring at text for extended periods — be kind to your eyes. Most editors let you increase font size with Ctrl++ (Cmd++ on Mac).

2. **Enable line numbers** if they're not already showing. This will help you find specific lines when we talk about them, and when error messages point to line numbers. In most editors, this is in the View or Editor settings.

3. **Set the theme** to whatever is comfortable for you. Many programmers prefer "dark mode" (light text on dark background) because it's easier on the eyes, but use whatever works for you.

### Opening the Terminal

You'll also need a terminal (also called "command line" or "console") to run your programs. Here's how to open it:

**On Windows:**
- Press Windows+R, type `cmd`, and press Enter
- Or search for "Command Prompt" in the Start menu
- Or (recommended) install Windows Terminal from the Microsoft Store

**On macOS:**
- Press Cmd+Space, type "Terminal", and press Enter
- Or find Terminal in Applications → Utilities

**On Linux:**
- Press Ctrl+Alt+T
- Or find Terminal in your applications menu

Once the terminal is open, navigate to your project folder:

```bash
cd ~/viper-projects
```

On Windows, the path might look like:
```bash
cd C:\Users\YourName\Documents\viper-projects
```

If you see your project folder name in the terminal prompt, you're in the right place.

### Verifying Viper Is Ready

Let's make sure Viper is properly installed. In your terminal, type:

```bash
viper --version
```

You should see something like:
```
Viper 0.1.3
```

If you see "command not found" or a similar error, go back to the Getting Started chapter and make sure you followed all the installation steps. The most common issue is that Viper isn't in your PATH — the list of places your computer looks for programs.

**Troubleshooting "command not found":**

1. **Did you run the export command?** After building Viper, you need to add it to your PATH. Run:
   ```bash
   export PATH="$PATH:/path/to/viper/build/src/tools/zia"
   ```
   Replace `/path/to/viper` with wherever you cloned the Viper repository.

2. **Are you in a new terminal window?** The `export` command only affects the current terminal session. You'll need to either run it again, or add it to your shell's startup file (like `.bashrc` or `.zshrc`).

3. **On Windows**, make sure you've added Viper to your system PATH through the Environment Variables settings.

---

## The "Hello, World!" Tradition

Before we write our program, let me tell you why we're about to write exactly what we're going to write.

In 1978, Brian Kernighan and Dennis Ritchie published "The C Programming Language" — arguably the most influential programming book ever written. In it, they introduced the tradition of starting with a program that simply displays "Hello, World!" on the screen.

But why this particular phrase? And why has almost every programming book, tutorial, and course for the past 45+ years started with the same thing?

**It's the smallest proof of success.** "Hello, World!" is the simplest possible program that *does something visible*. It proves that:
- Your tools are installed correctly
- You can create a source file
- The compiler understands your code
- You can run the resulting program
- You can see the output

That's a lot of things that could go wrong! Getting "Hello, World!" to appear means you've cleared all those hurdles. It's not just a program — it's a diagnostic test for your entire development environment.

**It's a shared experience.** Every programmer who has ever lived started with some version of "Hello, World!" When you run yours, you're participating in a tradition that connects you to millions of programmers around the world and across decades. You're joining a community.

**It's a moment to celebrate.** Your first program working is genuinely exciting. It might seem simple, but you've just commanded a machine to do something. That feeling — of typing instructions and watching a computer obey — never completely goes away. Many professional programmers still remember the joy of their first "Hello, World!"

So let's write it together.

---

## Writing the Program

Open your text editor and create a new file. Type the following code exactly as shown — every character matters:

```rust
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
```

Now save this file as `hello.zia` in your project folder.

**Important tips for saving:**
- Make sure the file extension is `.zia`, not `.zia.txt` or anything else
- Use exactly lowercase `hello.zia` — consistency matters
- Save it in your `viper-projects` folder so you can find it easily

### What If You're Not Sure You Typed It Right?

Here's the code again, character by character, with every piece called out:

```
m o d u l e   H e l l o ;
```
That's the word "module", a space, the word "Hello", and a semicolon.

```
func start() {
```
That's "func" (short for function), a space, "start", open parenthesis, close parenthesis, a space, and an open curly brace.

```
    Viper.Terminal.Say("Hello, World!");
```
That's four spaces (or one tab), then "Viper.Terminal.Say", open parenthesis, a double quote, the text "Hello, World!", a double quote, close parenthesis, and a semicolon.

```
}
```
Just a closing curly brace.

Double-check your code against this. The most common mistakes are:
- Forgetting the semicolons
- Using the wrong kind of quotes (use straight quotes ", not curly quotes " ")
- Misspelling "module", "func", "start", "Viper", "Terminal", or "Say"
- Forgetting the period between "Viper", "Terminal", and "Say"

---

## Running Your First Program

With your file saved, switch to your terminal. Make sure you're in the same folder as your `hello.zia` file. If you're not sure, you can type `ls` (on macOS/Linux) or `dir` (on Windows) to see what files are in your current folder.

Now type:

```bash
zia hello.zia
```

And press Enter.

If everything worked, you'll see:

```
Hello, World!
```

**Congratulations!** You just wrote and ran a computer program. Take a moment to appreciate this. You typed some text into a file, and then told the computer to run it, and the computer did exactly what you asked.

This might seem like a small thing — just three words on a screen. But think about what just happened. You communicated with a machine using a language it understands. You gave it instructions, and it followed them. You are now, officially, a programmer.

---

## What's Really Happening: Behind the Scenes

When you typed `zia hello.zia`, a remarkable chain of events unfolded in milliseconds. Understanding this process will help you understand programming more deeply.

### Step 1: Reading Your Code

The Viper compiler opened your `hello.zia` file and read its contents — the exact characters you typed. At this point, it's just text, like this document you're reading.

### Step 2: Lexical Analysis (Tokenizing)

The compiler broke your text into meaningful pieces called *tokens*. It recognized:
- `module` as a keyword
- `Hello` as a name
- `;` as a statement terminator
- `func` as a keyword
- And so on...

This is like how you break a sentence into words before understanding it.

### Step 3: Parsing (Understanding Structure)

The compiler checked that your tokens form valid Zia structure. It verified:
- "There's a module declaration followed by a semicolon — good."
- "There's a function definition with proper braces — good."
- "Inside the function, there's a valid function call — good."

If anything was wrong (missing semicolon, misspelled keyword), this is where you'd get an error message.

### Step 4: Compilation (Translation)

The compiler translated your Zia code into machine code — the raw numbers that your computer's processor can execute. The friendly `Viper.Terminal.Say("Hello, World!")` became something more like "load this text into memory, call the operating system's output function, pass it the memory address..."

### Step 5: Execution (Running)

Finally, your computer's processor executed the machine code instructions, one by one, billions of operations per second. The processor sent signals to your terminal program, which drew the characters "Hello, World!" on your screen.

All of this — every step — happened in a fraction of a second. You typed a command, and the output appeared instantly. But underneath that instant, an incredible amount of work happened. Your program is a message that passed through multiple layers of translation before reaching the hardware that made pixels light up on your screen.

---

## Understanding Every Line

Now let's slow down and understand exactly what each line of code does and *why* it's there.

### Line 1: `module Hello;`

```rust
module Hello;
```

This line gives your program a name. In Zia, every program is organized into *modules* — self-contained units of code.

**What is a module?** Think of modules like chapters in a book. Each chapter has a title and contains related content. Your module is titled "Hello" and contains everything needed for this program.

**Why do we need this?** When programs get bigger, you'll split them into multiple modules. A game might have a `Graphics` module, a `Sound` module, a `Physics` module, and so on. Each module handles one part of the overall system. For now, we only need one module, but the structure prepares us for larger programs.

**Could we name it something else?** Absolutely! You could write `module MyFirstProgram;` or `module Test;` or `module AnythingYouWant;`. The name is for you (and other programmers) to understand what this module is about. "Hello" is a good name for a "Hello, World!" program.

**What's the semicolon for?** In Zia, semicolons mark the end of statements — complete thoughts or commands. It's like the period at the end of a sentence. The semicolon tells the compiler "this statement is complete, don't try to continue reading it onto the next line."

**What happens without this line?** Try removing it and running your program:

```rust
func start() {
    Viper.Terminal.Say("Hello, World!");
}
```

You'll get an error like:
```
hello.zia:1:1: error: expected 'module' declaration
```

Every Zia file must start by declaring what module it belongs to. There's no way around this — it's a fundamental requirement of the language.

### Line 2: (blank)

The blank line between `module Hello;` and `func start()` isn't required. The compiler ignores blank lines completely. They're there for humans — to make the code easier to read by visually separating different parts.

**Good habit:** Use blank lines to group related code together, like paragraphs in writing.

### Line 3: `func start() {`

```rust
func start() {
```

This line defines a *function* called `start`. There's a lot packed into this one line, so let's break it down:

**`func`** is a keyword that means "I'm about to define a function." A keyword is a special word that Zia reserves for specific purposes — you can't use it as a name for your own things.

**`start`** is the name of this function. You could name functions almost anything (`greet`, `doSomething`, `myFunction`), but `start` is special. It's the *entry point* — when you run a Zia program, the computer looks for a function named `start` and begins executing there.

**`()`** — the parentheses — indicate that this is a function (as opposed to a variable or something else), and that this particular function doesn't need any extra information to do its job. Later, you'll see functions that take *parameters* inside these parentheses, like `func greet(name)`.

**`{`** — the opening curly brace — marks the beginning of the function's *body*. Everything between `{` and the matching `}` is the code that runs when this function is called.

**What is a function?** A function is a named, reusable group of instructions. Think of it like a recipe: it has a name ("Chocolate Chip Cookies"), and inside it are the steps to follow. When you want cookies, you don't reinvent the process — you follow the recipe. When you want code to run, you call the function.

**Why is `start` special?** Every program needs a beginning — somewhere for the computer to start executing. In Zia, that place is the `start` function. When you run `zia hello.zia`, the runtime system finds the `start` function and begins executing its body. Without a `start` function, the computer wouldn't know where to begin.

**What happens if you name it something else?**

```rust
module Hello;

func begin() {
    Viper.Terminal.Say("Hello, World!");
}
```

Running this produces:
```
hello.zia: error: no entry point found (missing 'start' function)
```

The program compiles, but the computer doesn't know what to run. It's like having a recipe book with no table of contents — you don't know where to start reading.

### Line 4: `    Viper.Terminal.Say("Hello, World!");`

```rust
    Viper.Terminal.Say("Hello, World!");
```

This is the line that actually makes something happen. Let's examine each piece:

**The indentation** — those four spaces at the beginning — isn't required by Zia. The program would work exactly the same without them. But they're *crucial* for humans. The indentation shows visually that this line is *inside* the function, subordinate to it. When you see indented code, you know it's part of something. Consistent indentation makes code readable. Most programmers use either 2 or 4 spaces for each level of indentation.

**`Viper.Terminal`** refers to the Terminal module of Viper's standard library. The *standard library* is a collection of pre-written code that comes with Zia. You don't have to write code to display text on the screen, read files, or do math — the standard library provides these capabilities ready-made.

The dots (`.`) are how you navigate through namespaces. `Viper` is the overall namespace for the standard library. `Terminal` is the specific module for terminal/console operations. This hierarchical naming prevents confusion — if you had your own `Terminal` module, it wouldn't conflict with Viper's.

**`Say`** is a function in the Terminal module that displays text followed by a newline (moving to the next line afterward). The standard library provides this so you don't have to figure out how to communicate with the operating system's console — you just call `Say`.

**`("Hello, World!")`** — the parentheses contain what we're passing to the `Say` function. This is called an *argument*. We're telling `Say` *what* to say. The double quotes indicate that `Hello, World!` is a *string* — a piece of text. Without the quotes, the compiler would try to interpret `Hello` as a variable name and get confused.

**The semicolon** ends this statement.

**What if you forget the semicolon?**

```rust
    Viper.Terminal.Say("Hello, World!")
```

Error:
```
hello.zia:5:1: error: expected ';' before '}'
```

The compiler reached the closing brace and realized the previous statement was never properly terminated.

**What if you misspell `Say`?**

```rust
    Viper.Terminal.say("Hello, World!");  // lowercase 's'
```

Error:
```
hello.zia:4:5: error: 'say' is not a member of 'Viper.Terminal'
```

Zia is case-sensitive. `Say` and `say` are completely different names. Most of the standard library uses `PascalCase` for function names (capital letter at the start of each word).

**What if you forget the quotes around the text?**

```rust
    Viper.Terminal.Say(Hello, World!);
```

Error:
```
hello.zia:4:27: error: expected expression
hello.zia:4:28: error: unexpected token '!'
```

Without quotes, the compiler tries to interpret `Hello` as a variable and gets confused by the comma, space, and exclamation mark.

### Line 5: `}`

```rust
}
```

The closing curly brace marks the end of the `start` function. Everything between the opening `{` on line 3 and this `}` is the function body — the code that runs when the function is called.

**What if you forget it?**

```rust
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");

```

Error:
```
hello.zia:5:1: error: unexpected end of file, expected '}'
```

The compiler reached the end of the file still expecting to find the closing brace. Braces must always be balanced — every `{` needs a matching `}`.

---

## The Same Program in BASIC and Pascal

Viper supports three languages that express the same concepts differently. Seeing the same program in different languages helps you understand what's fundamental (the ideas) versus what's surface-level (the syntax).

### BASIC

```basic
PRINT "Hello, World!"
```

Just one line! BASIC was designed in 1964 specifically to be easy for beginners (BASIC stands for "Beginner's All-purpose Symbolic Instruction Code"). For simple programs, BASIC is wonderfully concise:

- No module declaration needed — the file itself is the program
- No explicit function — the code runs from top to bottom
- No semicolons — newlines separate statements
- `PRINT` is the built-in command for displaying text

This simplicity is BASIC's strength and limitation. For small programs, it's delightfully straightforward. For larger programs, the lack of structure becomes a problem.

To run this, save it as `hello.bas` and run:
```bash
vbasic hello.bas
```

### Pascal

```pascal
program Hello;
begin
    WriteLn('Hello, World!');
end.
```

Pascal (created in 1970) sits between BASIC's simplicity and Zia's modern style:

- `program Hello;` instead of `module Hello;` — same idea, different keyword
- `begin` and `end.` instead of `{` and `}` — same structure, spelled out in words
- `WriteLn` instead of `Say` — "Write Line" is more descriptive but more to type
- Single quotes (`'`) instead of double quotes (`"`) for strings
- A period (`.`) after `end` to mark the program's end

Pascal was designed for teaching programming and emphasizes clarity. The word `begin` is clearer than `{` for newcomers, though `{` becomes natural with practice.

To run this, save it as `hello.pas` and run:
```bash
vpascal hello.pas
```

### What's the Same?

Look at what all three versions share:

1. **A name for the program** — `Hello` in all cases
2. **A way to mark where code starts and ends** — braces, `begin`/`end`, or just file boundaries
3. **A function to display text** — `Say`, `PRINT`, `WriteLn`
4. **The text to display, in quotes**

These are the *concepts*. The syntax — the specific characters and words — is just how each language spells those concepts. Learning to see through syntax to the underlying ideas is a key skill in programming.

---

## Troubleshooting Guide

When things go wrong (and they will — that's normal!), error messages are your guide. Here are the most common problems beginners encounter:

### "command not found" or "viper is not recognized"

**What it means:** Your computer doesn't know where to find the Viper program.

**How to fix it:**
1. Make sure you completed the installation in Chapter 0
2. If on macOS/Linux, run the export command again: `export PATH="$PATH:/path/to/viper/build/src/tools/zia"`
3. If on Windows, check that Viper is in your system PATH
4. Try using the full path: `/full/path/to/viper/build/src/tools/zia/zia hello.zia`

### "file not found" or "No such file or directory"

**What it means:** Viper can't find your `hello.zia` file.

**How to fix it:**
1. Make sure you're in the same folder as your file. Type `ls` (or `dir` on Windows) to see what files are in your current folder.
2. Check that you saved the file with the right name — `hello.zia`, not `hello.zia.txt`
3. Use `cd` to navigate to the folder where you saved your file

### "unterminated string literal"

**What it means:** You started a string with a quote but never closed it.

**What the code probably looks like:**
```rust
Viper.Terminal.Say("Hello, World!);  // Missing closing quote
```

**How to fix it:** Make sure every opening `"` has a matching closing `"`.

### "expected ';'"

**What it means:** The compiler expected a semicolon to end a statement.

**What the code probably looks like:**
```rust
Viper.Terminal.Say("Hello, World!")  // Missing semicolon
```

**How to fix it:** Add a semicolon at the end of the line.

### "expected '}'"

**What it means:** You opened a code block with `{` but never closed it.

**How to fix it:** Count your braces. Every `{` needs a matching `}`. Check that you haven't accidentally deleted one.

### "no entry point found"

**What it means:** Your program doesn't have a `start` function, so the computer doesn't know where to begin.

**How to fix it:** Make sure you have `func start() { ... }` and that you spelled `start` correctly.

### "'xxx' is not a member of 'Viper.Terminal'"

**What it means:** You tried to use a function that doesn't exist in the Terminal module.

**What the code probably looks like:**
```rust
Viper.Terminal.say("Hello");  // Should be Say, not say
Viper.Terminal.Print("Hello");  // Print exists, but it doesn't add a newline
```

**How to fix it:** Check the spelling and capitalization. `Say`, `Print`, and other standard library functions use PascalCase.

### The general approach to errors

1. **Read the whole message.** It usually tells you the file, line number, and what went wrong.
2. **Look at the line mentioned.** Often the error points directly at the problem.
3. **Look at the line *before* the one mentioned.** Sometimes the actual error is on the previous line, but the compiler doesn't notice until the next line.
4. **Compare your code character-by-character** with working examples.
5. **Check for common mistakes:** semicolons, quotes, braces, spelling, capitalization.

---

## Experimenting: Make It Your Own

You've proven that everything works. Now let's have some fun.

### Change the Message

Edit your program to say something different:

```rust
module Hello;

func start() {
    Viper.Terminal.Say("I wrote my first program!");
}
```

Save and run it. See your new message? You're now in control of what the computer says.

Try these variations:
- Your name: `"My name is [Your Name]!"`
- Today's date: `"Today is [Date]!"`
- Anything you want: `"Computers do what I tell them!"`

Each time you save and run, you're practicing the fundamental programming cycle: edit, save, run, observe.

### Multiple Lines

A single `Say` prints one line. What if you want more?

```rust
module Hello;

func start() {
    Viper.Terminal.Say("Line one.");
    Viper.Terminal.Say("Line two.");
    Viper.Terminal.Say("Line three.");
}
```

Output:
```
Line one.
Line two.
Line three.
```

The program runs from top to bottom. Each `Say` happens in order. This is *sequential execution* — one of the fundamental patterns of programming.

### Print vs. Say

What if you want multiple things on the same line? Use `Print` instead of `Say`:

```rust
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

Here's the difference:
- `Say` displays text *and* moves to a new line afterward
- `Print` displays text but stays on the same line

The final `Say` ensures we end with a newline (otherwise your terminal prompt would appear right after "Three").

### Making Art

With what you know, you can create simple text art:

```rust
module Art;

func start() {
    Viper.Terminal.Say("  *  ");
    Viper.Terminal.Say(" *** ");
    Viper.Terminal.Say("*****");
    Viper.Terminal.Say(" *** ");
    Viper.Terminal.Say("  *  ");
}
```

Output:
```
  *
 ***
*****
 ***
  *
```

A diamond! Try creating other shapes: squares, triangles, letters, or simple faces.

---

## When Things Go Wrong: A Learning Opportunity

Let's intentionally break our program to understand error messages better. This might seem strange, but learning to read errors is one of the most important skills you'll develop.

### Missing Quote

```rust
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!);
}
```

Run it:
```
hello.zia:4:31: error: unterminated string literal
    Viper.Terminal.Say("Hello, World!);
                              ^
```

The error message tells us:
- **hello.zia** — the file with the problem
- **4** — line 4
- **31** — column 31 (the position on that line)
- **unterminated string literal** — we started a string but never ended it
- The caret (`^`) shows exactly where the compiler got confused

The compiler is surprisingly helpful once you learn to read its messages.

### Missing Semicolon

```rust
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!")
}
```

Run it:
```
hello.zia:5:1: error: expected ';' before '}'
}
^
```

Notice the error points to line 5 (the closing brace), not line 4 (where the semicolon is missing). The compiler didn't realize something was wrong until it hit the `}` and thought "wait, I was expecting a semicolon, not a brace."

This is common — errors sometimes manifest on a different line than where the actual mistake is. When an error doesn't seem to match the line mentioned, check the line before.

### Misspelled Function

```rust
module Hello;

func start() {
    Viper.Terminal.Sya("Hello, World!");
}
```

Run it:
```
hello.zia:4:5: error: 'Sya' is not a member of 'Viper.Terminal'
    Viper.Terminal.Sya("Hello, World!");
                   ^~~
```

The compiler knows `Terminal` exists but doesn't recognize `Sya`. This kind of error often comes from typos. The squiggles (`^~~`) highlight the problematic part.

---

## Summary

You've written and run your first program — and in doing so, you've crossed a threshold. You now have a working development environment, understand how to write and execute Zia code, and can read error messages when things go wrong. Every complex program starts exactly where you are now.

Here are the key concepts from this chapter:

- **Modules** organize code into named units. Every Zia program needs at least one module, declared with `module Name;`
- **Functions** are named groups of instructions. The `start` function is special — it's where your program begins execution
- **The standard library** provides ready-made functions like `Say` and `Print` so you don't have to write everything from scratch
- **Statements** are individual commands, ending with semicolons
- **Strings** are text in double quotes: `"like this"`
- **Error messages** tell you what's wrong and where. Read them carefully — they'll guide you to the fix

---

## Exercises

**Exercise 2.1**: Write a program that displays your name on the screen.

**Exercise 2.2**: Write a program that displays three lines: your name, your city, and your favorite food. Each on its own line.

**Exercise 2.3**: Using `Print` and `Say`, make a program that outputs this on a single line: `Hello, my name is [your name].`

**Exercise 2.4**: Write the "Hello, World!" program in BASIC. Save it as `hello.bas` and run it with `vbasic hello.bas`. Then write it in Pascal, save it as `hello.pas`, and run it with `vpascal hello.pas`.

**Exercise 2.5**: Make each of these errors deliberately, then read and understand the error message:
- Missing closing quote
- Missing semicolon
- Missing closing brace
- Misspelled `Say` as `say`
- Misspelled `start` as `Start`

**Exercise 2.6** (Challenge): Using only what you've learned so far, display this exact output:

```
***********
*  HELLO  *
***********
```

Hint: You'll need multiple `Say` calls, and you'll need to count your spaces carefully.

**Exercise 2.7** (Challenge): Create your own text art — your initials, a simple picture, or a pattern. The only tools you have are `Say` and `Print`, but you'd be surprised what you can create with them.

---

## What Comes Next

You've written a program, but it always does the same thing. Every time you run it, you see the same output. Real programs are dynamic — they work with data that changes, they remember things, they calculate.

In the next chapter, you'll learn about *values* and *variables*: how to store information, give it names, and work with it. You'll build programs that respond to input and produce different outputs depending on what they're given.

The journey is just beginning.

---

*You did it. You wrote and ran your first program. That moment — seeing your words appear on screen because you told the computer to put them there — is something every programmer remembers. Welcome to the craft.*

*[Continue to Chapter 3: Values and Names →](03-values-and-names.md)*
