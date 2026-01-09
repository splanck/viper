# Chapter 1: The Machine

Before you write a single line of code, let's talk about what you're actually doing when you program a computer. This isn't just background information — it's the mental model that will make everything else make sense.

---

## What Is a Computer, Really?

At its heart, a computer is a machine that follows instructions. That's it. It's not magic, it's not thinking, it's not creative. It just does exactly what you tell it, one instruction at a time, very, very fast.

Imagine the world's most obedient but literal-minded assistant. If you say "go to the store and buy milk," a human understands what you mean. A computer would need you to specify:
- Stand up from the chair
- Walk forward 10 steps
- Turn right
- Open the door
- Walk through the door
- Close the door
- ...

Every. Single. Step.

This is both the frustration and the power of programming. The computer does exactly what you say — no more, no less. When it does something wrong, it's because you told it to. When it does something amazing, it's because you told it to do that too.

---

## The Parts That Matter

A computer has many parts, but for programming, you only need to think about a few:

**Memory** is where the computer stores information while working. Think of it as a vast wall of numbered boxes. Each box can hold a number or a letter. The computer can read from any box or write to any box, instantly. When you turn off the computer, memory is erased.

**Storage** (like your hard drive) is where information lives permanently. Unlike memory, it survives when you turn off the computer. But it's much slower to access.

**The Processor** (CPU) is the part that actually does things. It reads instructions, fetches values from memory, does math, makes decisions, and writes results back to memory. Modern processors do this billions of times per second.

When you run a program, here's what happens:
1. Your program is loaded from storage into memory
2. The processor reads the first instruction
3. It does what the instruction says
4. It moves to the next instruction
5. Repeat until the program ends

---

## What Is a Program?

A program is a list of instructions for the computer to follow. That's the simple answer.

The more interesting answer is that a program is a way to teach the computer something new. The computer doesn't know how to edit photos or play music or browse the web. Programs teach it how.

Right now, your computer is running dozens of programs at once: the operating system that manages everything, the program showing you this text, programs checking for updates in the background. Each one is just a list of instructions.

You're about to learn how to write those instructions.

---

## Why Programming Languages?

The processor only understands numbers. Literally. Every instruction is a number. Every piece of data is a number. This is called *machine code*, and it looks like this:

```
48 89 e5 48 83 ec 10 c7 45 fc 00 00 00 00
```

Nobody wants to write that. Nobody wants to read that. So we invented programming languages.

A programming language is a way to write instructions that humans can read and write, which a special program (called a *compiler*) translates into the numbers the computer understands.

Here's the same thing in ViperLang:

```viper
var x = 0;
```

This means "create a variable named x and give it the value 0." The compiler turns this into those numbers the processor needs.

Programming languages are a gift from programmers to other programmers (including future you). They let us express ideas clearly, catch mistakes early, and build on each other's work.

---

## Why Three Languages?

Viper gives you three languages: ViperLang, BASIC, and Pascal. Why?

They're different ways to express the same ideas. Like how you can give directions by listing turns ("left, right, straight, left") or by giving landmarks ("past the church, toward the lake"). Same destination, different paths.

Some people find one style more natural than another. Some languages are better suited for certain tasks. And understanding multiple languages helps you see the underlying concepts more clearly — the things that stay the same no matter how you express them.

Here's "create a variable x with value 0" in all three:

**ViperLang**
```viper
var x = 0;
```

**BASIC**
```basic
DIM x AS INTEGER
x = 0
```

**Pascal**
```pascal
var x: Integer;
x := 0;
```

Different words, same idea. All three compile to the same machine code. All three produce the same result.

This book teaches primarily in ViperLang because it's modern and clean. But you'll see BASIC and Pascal examples throughout, and you're free to use whichever you prefer.

---

## What Programs Actually Do

Every program, no matter how complex, does some combination of these things:

1. **Takes input** — from the keyboard, from files, from the network, from sensors
2. **Stores and retrieves data** — keeping track of information
3. **Does calculations** — adding numbers, comparing values, transforming data
4. **Makes decisions** — doing different things based on conditions
5. **Repeats actions** — doing something many times
6. **Produces output** — showing text, saving files, sending data

A video game does all of these: it takes input from your controller, stores your score and position, calculates where enemies should be, decides if you've been hit, repeats 60 times per second to create animation, and outputs graphics to your screen.

A text editor does all of these too: input from your keyboard, stores your document, calculates line breaks, decides whether to show autocomplete, repeats to keep the screen updated, outputs text to the display.

You're going to learn each of these capabilities, building up from simple to complex.

---

## The Mental Model

Here's the key insight: **a program is a description of a process.**

When you write a program, you're not directly controlling the computer. You're writing a recipe — a set of steps — that the computer will follow later, when you run the program.

This is why typos matter: you're writing down instructions that will be followed precisely. A typo is like writing "add 2 cups of salt" when you meant "1/4 teaspoon." The cook follows the recipe exactly, and dinner is ruined.

It's also why testing matters: you can't see the process happening just by reading the code. You have to actually run it to see if it does what you intended.

And it's why thinking clearly matters: a confused recipe produces confused results. The clearer you are about what you want to happen, the easier it is to write code that makes it happen.

---

## Summary

- Computers follow instructions exactly, very fast
- Memory holds information temporarily; storage holds it permanently
- Programs are lists of instructions written in a programming language
- A compiler translates your code into numbers the computer can execute
- All programs do some combination of: input, storage, calculation, decisions, repetition, output
- A program describes a process; you write it now, the computer follows it later

---

## Exercises

These exercises don't involve code yet — they're about building the mental model.

**Exercise 1.1**: Think of something you do routinely (making coffee, getting ready for bed). Write down the steps as if you were explaining to someone who has never done it before. How detailed do you need to be?

**Exercise 1.2**: Consider a simple app you use (calculator, timer, notes). List what inputs it takes, what data it stores, what calculations it does, what decisions it makes, and what output it produces.

**Exercise 1.3**: You want to teach a computer to decide if a number is even or odd. Describe in plain English what steps the computer should follow. (Hint: what does it mean for a number to be even?)

---

*Now that you understand what computers do, let's write your first real program.*

*[Continue to Chapter 2: Your First Program →](02-first-program.md)*
