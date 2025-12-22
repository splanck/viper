# The Viper Bible

**A complete guide to programming — from zero to mastery.**

This book teaches you to program computers. You don't need any prior experience. By the end, you'll be building sophisticated applications: games, tools, networked systems, and more.

We teach primarily in **ViperLang**, a modern language designed for clarity and power. Every concept is also shown in **BASIC** and **Pascal** for those who prefer different styles or need to work with existing code.

---

## How This Book Works

The Viper Bible is organized as a journey, not a dictionary. Each chapter builds on the last. We don't just show you syntax — we explain *why* things work the way they do, what problems they solve, and how to think about programming.

**Read it in order.** Do the exercises. Type the code yourself. Make mistakes and fix them. That's how learning happens.

---

## The Journey

### Part I: Foundations
*What is programming? How do computers work? Your first programs.*

| Chapter | You Will Learn |
|---------|----------------|
| [1. The Machine](part1-foundations/01-the-machine.md) | What computers actually do, how programs work |
| [2. Your First Program](part1-foundations/02-first-program.md) | Writing, running, and understanding "Hello, World" |
| [3. Values and Names](part1-foundations/03-values-and-names.md) | Numbers, text, variables — the atoms of programs |
| [4. Making Decisions](part1-foundations/04-decisions.md) | If/else, conditions, boolean logic |
| [5. Repetition](part1-foundations/05-repetition.md) | Loops — doing things many times |
| [6. Collections](part1-foundations/06-collections.md) | Arrays and lists — working with groups of things |
| [7. Breaking It Down](part1-foundations/07-functions.md) | Functions — organizing code into reusable pieces |

### Part II: Building Blocks
*The techniques that make real programs possible.*

| Chapter | You Will Learn |
|---------|----------------|
| [8. Text and Strings](part2-building-blocks/08-strings.md) | Working with text, formatting, parsing |
| [9. Files and Persistence](part2-building-blocks/09-files.md) | Reading and writing files, saving data |
| [10. Errors and Recovery](part2-building-blocks/10-errors.md) | When things go wrong, handling failures gracefully |
| [11. Structures](part2-building-blocks/11-structures.md) | Grouping related data together |
| [12. Modules](part2-building-blocks/12-modules.md) | Organizing code across files |
| [13. The Standard Library](part2-building-blocks/13-stdlib.md) | What Viper gives you for free |

### Part III: Thinking in Objects
*Modeling the world with objects and types.*

| Chapter | You Will Learn |
|---------|----------------|
| [14. Objects and Classes](part3-objects/14-objects.md) | Creating your own types |
| [15. Inheritance](part3-objects/15-inheritance.md) | Building on existing types |
| [16. Interfaces](part3-objects/16-interfaces.md) | Defining contracts between components |
| [17. Polymorphism](part3-objects/17-polymorphism.md) | Writing code that works with many types |
| [18. Design Patterns](part3-objects/18-patterns.md) | Common solutions to common problems |

### Part IV: Real Applications
*Putting it all together to build things that matter.*

| Chapter | You Will Learn |
|---------|----------------|
| [19. Graphics and Games](part4-applications/19-graphics.md) | Drawing, animation, game loops |
| [20. User Input](part4-applications/20-input.md) | Keyboard, mouse, controllers |
| [21. Building a Game](part4-applications/21-game-project.md) | Complete walkthrough: Frogger from scratch |
| [22. Networking](part4-applications/22-networking.md) | TCP, UDP, building connected applications |
| [23. Data Formats](part4-applications/23-data-formats.md) | JSON, CSV, serialization |
| [24. Concurrency](part4-applications/24-concurrency.md) | Doing multiple things at once |

### Part V: Mastery
*Deep understanding and advanced techniques.*

| Chapter | You Will Learn |
|---------|----------------|
| [25. How Viper Works](part5-mastery/25-how-viper-works.md) | The compiler, IL, and runtime |
| [26. Performance](part5-mastery/26-performance.md) | Making programs fast |
| [27. Testing](part5-mastery/27-testing.md) | Ensuring your code works |
| [28. Architecture](part5-mastery/28-architecture.md) | Designing large systems |

### Appendices
*Reference material for when you need to look things up.*

| Appendix | Contents |
|----------|----------|
| [A. ViperLang Reference](appendices/a-viperlang-reference.md) | Complete syntax and semantics |
| [B. BASIC Reference](appendices/b-basic-reference.md) | Complete syntax and semantics |
| [C. Pascal Reference](appendices/c-pascal-reference.md) | Complete syntax and semantics |
| [D. Runtime Library](appendices/d-runtime-reference.md) | All built-in functions and types |
| [E. Error Messages](appendices/e-error-messages.md) | What they mean and how to fix them |
| [F. Glossary](appendices/f-glossary.md) | Programming terms explained |

---

## The Three Languages

Viper supports three languages that all compile to the same underlying system. This book emphasizes ViperLang but shows all three:

**ViperLang** — Modern, clean, C-like syntax. Our recommended choice for new projects.
```viper
func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name + "!");
}
```

**BASIC** — Classic, beginner-friendly, keyword-based. Great for learning and rapid prototyping.
```basic
SUB Greet(name AS STRING)
    PRINT "Hello, "; name; "!"
END SUB
```

**Pascal** — Structured, explicit, educational heritage. Familiar to many programmers.
```pascal
procedure Greet(name: string);
begin
    WriteLn('Hello, ', name, '!');
end;
```

All three are equally powerful. They access the same runtime library. Code written in one can call code written in another. Choose the style that feels right to you.

---

## Before You Begin

You'll need:
- A computer (Windows, macOS, or Linux)
- The Viper toolchain installed (see [Getting Started](part1-foundations/00-getting-started.md))
- A text editor (any will do)
- Curiosity and patience

Let's begin.

*[Continue to Chapter 1: The Machine →](part1-foundations/01-the-machine.md)*
