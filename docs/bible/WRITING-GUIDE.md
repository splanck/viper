# Viper Bible Writing Guide

This guide defines the tone, structure, and approach for writing the Viper Bible.

---

## Philosophy

**The Viper Bible teaches programming from zero.** Our reader has never written a line of code. They're intelligent and motivated, but everything is new to them.

We are not writing documentation. We are writing a *book* — one that someone could sit down and read cover to cover, learning to program along the way.

### Core Principles

1. **Explain the "why" before the "what"**
   - Every concept exists to solve a problem
   - Introduce the problem first, then the solution
   - Don't just show syntax — show *purpose*

2. **Build incrementally**
   - Each chapter builds on previous chapters
   - No forward references to unexplained concepts
   - Revisit and deepen understanding over time

3. **Narrative over reference**
   - Write prose, not bullet points
   - Tell stories, give examples, make analogies
   - The appendices are for reference; chapters are for learning

4. **Show, then explain**
   - Lead with working code
   - Walk through what happens, line by line
   - Then generalize to the concept

5. **All three languages, ViperLang first**
   - Primary examples in ViperLang
   - Follow with BASIC and Pascal equivalents
   - Use comparison to illuminate, not confuse

---

## Chapter Structure

Every chapter follows this pattern:

### 1. Opening Hook (1-2 paragraphs)
A compelling reason to care about this topic. What problem does it solve? What can you do after learning it that you couldn't do before?

### 2. The Concept (narrative)
Explain the idea in plain language. Use analogies to everyday life. Build intuition before showing code.

### 3. First Example
A simple, complete, working program that demonstrates the concept. Walk through it line by line.

### 4. Deeper Exploration (multiple sections)
Build complexity gradually. Each section introduces one new aspect. More examples, more explanation.

### 5. The Three Languages
Show how the concept works in ViperLang, BASIC, and Pascal. Highlight similarities and differences.

### 6. Common Mistakes
What goes wrong? Show broken code, explain why it's broken, show the fix.

### 7. Exercises
Practical problems for the reader to solve. Start easy, get harder. Include solutions in a collapsible section or separate file.

### 8. Summary
Brief recap of key points. Link forward to the next chapter.

---

## Voice and Tone

### Do

- Write in first person plural ("we") or second person ("you")
- Be conversational but not sloppy
- Use concrete examples over abstract descriptions
- Admit when something is tricky or surprising
- Celebrate progress ("Now you can...")

### Don't

- Use jargon without explanation
- Assume prior knowledge
- Be condescending ("simply do X")
- Rush through difficult concepts
- Leave the reader confused

### Example: Good

> Variables are names for values. When you write `var age = 25`, you're telling Viper: "Remember the number 25, and when I say `age`, I mean that number."
>
> Why bother with names? Because programs work with lots of values, and you need a way to refer to them. It's like giving someone your phone number — the digits don't change, but having a name ("Mom's cell") makes them easier to use.

### Example: Bad

> Variables store values in memory locations. Use `var` for variable declarations. Variables have types inferred by the compiler.

---

## Code Examples

### Format

Always show complete, runnable programs when possible. Use syntax highlighting with the language name:

~~~markdown
```viper
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
```
~~~

### The Three Languages Pattern

When showing the same concept in all three languages, use this structure:

```markdown
**ViperLang**
```viper
// ViperLang code here
```

**BASIC**
```basic
' BASIC code here
```

**Pascal**
```pascal
{ Pascal code here }
```
```

### Line-by-Line Walkthrough

For important examples, explain each line:

```markdown
```viper
var count = 0;        // Create a variable named 'count', starting at 0
while count < 5 {     // Keep going as long as count is less than 5
    Say(count);       // Print the current value
    count = count + 1; // Add 1 to count
}
```
```

---

## Analogies Library

Use these recurring analogies for consistency:

| Concept | Analogy |
|---------|---------|
| Variables | Named boxes that hold values |
| Functions | Recipes — ingredients in, dish out |
| Arrays | Numbered mailboxes in a row |
| Objects | Forms with fields to fill in |
| Classes | Templates/blueprints for objects |
| Interfaces | Job descriptions (requirements, not implementation) |
| Modules | Chapters in a book |
| Loops | Assembly lines, repetitive tasks |
| Conditionals | Forks in a road |
| Types | Categories (a dog is an animal) |

---

## Exercise Design

### Progression
1. **Mimic**: Modify the chapter's example slightly
2. **Extend**: Add a new feature to the example
3. **Create**: Build something new using the concept
4. **Challenge**: Harder problems for motivated readers

### Example Exercise Set

> **Exercise 5.1**: Modify the loop to count from 10 down to 1 instead of 1 to 10.
>
> **Exercise 5.2**: Write a program that prints all even numbers from 2 to 20.
>
> **Exercise 5.3**: Write a program that asks for a number and prints that many stars (*).
>
> **Exercise 5.4** (Challenge): Write a program that prints the first 20 numbers of the Fibonacci sequence.

---

## Cross-References

Link to other chapters when referencing concepts:

> We learned about functions in [Chapter 7](../part1-foundations/07-functions.md). Now we'll see how to group functions into modules.

Link to appendices for reference details:

> For a complete list of string functions, see [Appendix D: Runtime Library](../appendices/runtime-library.md#strings).

---

## Content from Existing Docs

The existing documentation should inform but not constrain the Bible. Use it as:

| Existing Doc | Bible Usage |
|--------------|-------------|
| Language references | Appendix source material |
| viperlib/ docs | Appendix D and chapter examples |
| Demos (Frogger, etc.) | Chapter 21 game project, Part IV examples |
| Examples | Exercise inspiration, code snippets |
| IL guide | Chapter 25 (How Viper Works) |
| Architecture | Chapter 25, 28 |

---

## File Naming

- Use lowercase with hyphens: `01-the-machine.md`
- Number chapters within each part: `01`, `02`, etc.
- Keep the global chapter number in the title for reference

---

## Checklist for Each Chapter

Before considering a chapter complete:

- [ ] Opens with a compelling hook
- [ ] Explains the "why" before showing code
- [ ] Shows complete, working examples
- [ ] Walks through code line-by-line for key examples
- [ ] Demonstrates concept in all three languages
- [ ] Addresses common mistakes
- [ ] Includes 4+ exercises with increasing difficulty
- [ ] Links to next chapter
- [ ] No unexplained jargon
- [ ] A beginner could read and understand it
