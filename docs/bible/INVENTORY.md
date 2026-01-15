# Bible Content Inventory

This document maps existing Viper documentation and code examples to the Bible's teaching structure.

---

## Vision

**The Viper Bible teaches programming from zero to mastery.**

- Target audience: Complete beginners who have never written code
- Approach: Narrative, explanatory, concept-first
- Primary language: Zia (with BASIC and Pascal for comparison)
- Goal: Readers can build sophisticated applications by the end

---

## Source Material Summary

| Category | Files | Lines | Bible Usage |
|----------|-------|-------|-------------|
| Existing docs | 144 | ~28,000 | Appendices, reference validation |
| BASIC examples | 50+ | ~400 | Exercise inspiration, code patterns |
| Pascal examples | 6 | ~320 | Exercise inspiration |
| Zia examples | 1 | ~5 | Needs expansion |
| BASIC demos | 7 games | ~9,600 | Part IV projects (esp. Frogger) |
| Pascal demos | 4 games | ~2,550 | Alternative implementations |
| Zia demos | 1 game | ~1,800 | Module system showcase |
| Runtime headers | 80+ | varies | Part II/III API teaching |

---

## Part I: Foundations (Chapters 1-7)

*From zero knowledge to writing useful programs.*

| Chapter | Source Material | Content Needs |
|---------|-----------------|---------------|
| 0. Getting Started | `/docs/getting-started.md` | Installation, verify setup |
| 1. The Machine | New | Mental model, what computers do |
| 2. First Program | `hello.zia`, `hello.bas`, `hello.pas` | Hello World, anatomy of a program |
| 3. Values and Names | Basic examples | Variables, types, literals |
| 4. Making Decisions | `ex1_hello_cond.bas`, `ex4_if_elseif.bas` | if/else, conditions, booleans |
| 5. Repetition | `ex3_for_table.bas`, loop examples | for, while, loop patterns |
| 6. Collections | `ex6_array_sum.bas` | Arrays, indexing |
| 7. Breaking It Down | `fib.bas`, `fact.bas` | Functions, parameters, return |

### Writing Status

- [x] Chapter 0: Getting Started (complete)
- [x] Chapter 1: The Machine (complete)
- [x] Chapter 2: Your First Program (complete)
- [ ] Chapter 3: Values and Names
- [ ] Chapter 4: Making Decisions
- [ ] Chapter 5: Repetition
- [ ] Chapter 6: Collections
- [ ] Chapter 7: Breaking It Down

---

## Part II: Building Blocks (Chapters 8-13)

*The techniques that make real programs possible.*

| Chapter | Source Material | Content Needs |
|---------|-----------------|---------------|
| 8. Text and Strings | String examples (12 files), `rt_string.h` | String operations, formatting |
| 9. Files and Persistence | File examples, `rt_file.h` | Reading/writing files |
| 10. Errors and Recovery | `/docs/devdocs/specs/errors.md` | Try/catch, error handling |
| 11. Structures | OOP examples | Records, grouping data |
| 12. Modules | Zia Frogger demo | Import, export, organization |
| 13. The Standard Library | `/docs/viperlib/` | Survey of available APIs |

---

## Part III: Thinking in Objects (Chapters 14-18)

*Modeling the world with objects and types.*

| Chapter | Source Material | Content Needs |
|---------|-----------------|---------------|
| 14. Objects and Classes | `oop_shapes.pas`, OOP examples | Creating types, methods |
| 15. Inheritance | `oop_shapes.pas` | Extending types |
| 16. Interfaces | Language references | Contracts, polymorphism |
| 17. Polymorphism | Demo games (enemy types, etc.) | Practical OOP patterns |
| 18. Design Patterns | Chess, Pacman demos | Common solutions |

---

## Part IV: Real Applications (Chapters 19-24)

*Putting it all together to build things that matter.*

| Chapter | Source Material | Content Needs |
|---------|-----------------|---------------|
| 19. Graphics and Games | `/docs/graphics-library.md`, demos | Canvas, drawing, animation |
| 20. User Input | `rt_input.h`, demo games | Keyboard, mouse, gamepad |
| 21. Building a Game | `/demos/basic/frogger/` | Complete project walkthrough |
| 22. Networking | `/docs/viperlib/network.md`, Chess demo | TCP, UDP, protocols |
| 23. Data Formats | `/docs/viperlib/text.md` | CSV, serialization |
| 24. Concurrency | `/docs/viperlib/threads.md` | Threading basics |

### The Frogger Project

Chapter 21 will walk through building Frogger from scratch, teaching:
- Game loop architecture
- Sprite/entity management
- Collision detection
- State machines
- Score and lives
- Progressive difficulty

Source: `/demos/basic/frogger/` (1,200+ LOC) and `/demos/zia/frogger/` (16 modules)

---

## Part V: Mastery (Chapters 25-28)

*Deep understanding and advanced techniques.*

| Chapter | Source Material | Content Needs |
|---------|-----------------|---------------|
| 25. How Viper Works | `/docs/il-guide.md`, `/docs/vm.md` | Compilation, IL, runtime |
| 26. Performance | `/docs/devdocs/vm-optimizations.md` | Profiling, optimization |
| 27. Testing | `/docs/testing.md` | Test strategies |
| 28. Architecture | `/docs/frontend-howto.md` | Large system design |

---

## Appendices

*Reference material extracted from existing docs.*

| Appendix | Source | Notes |
|----------|--------|-------|
| A. Zia Reference | `/docs/zia-reference.md` | Reformatted |
| B. BASIC Reference | `/docs/basic-reference.md` | Reformatted |
| C. Pascal Reference | `/docs/pascal-reference.md` | Reformatted |
| D. Runtime Library | `/docs/viperlib/*.md` | Consolidated |
| E. Error Messages | `/docs/devdocs/specs/errors.md` | Expanded with solutions |
| F. Glossary | New | Terms from all chapters |

---

## Code Example Inventory

### By Concept (for teaching)

| Concept | Zia | BASIC | Pascal |
|---------|-----------|-------|--------|
| Hello World | `hello.zia` | many | `hello.pas` |
| Variables | needs creation | `ex5_input_echo.bas` | examples |
| Loops | needs creation | `ex3_for_table.bas` | `fibonacci.pas` |
| Arrays | needs creation | `ex6_array_sum.bas` | examples |
| Functions | needs creation | `fib.bas`, `fact.bas` | `factorial.pas` |
| Strings | needs creation | 12 string examples | examples |
| File I/O | needs creation | file examples | examples |
| OOP | needs creation | OOP demos | `oop_shapes.pas` |
| Graphics | needs creation | Particles demo | demos |
| Games | Frogger demo | 7 game demos | 4 game demos |

### Gap: Zia Examples

The Zia frontend has excellent demos but lacks small teaching examples. Need to create:
- [ ] Basic variable examples
- [ ] Control flow examples
- [ ] Function examples
- [ ] String manipulation examples
- [ ] File I/O examples
- [ ] Simple OOP examples

---

## Builtin Functions for Teaching

### Commonly Used (teach early)

| Function | Zia | BASIC | Pascal |
|----------|-----------|-------|--------|
| Print text | `Viper.Terminal.Say()` | `PRINT` | `WriteLn` |
| Read input | `Viper.Terminal.Ask()` | `INPUT` | `ReadLn` |
| String length | `.Length` | `LEN()` | `Length()` |
| Substring | `.Substring()` | `MID$()` | `Copy()` |
| Number to string | `ToString()` | `STR$()` | `IntToStr()` |
| String to number | `Parse()` | `VAL()` | `StrToInt()` |
| Random number | `Viper.Math.Random()` | `RND` | `Random()` |
| Absolute value | `Viper.Math.Abs()` | `ABS()` | `Abs()` |

### By Chapter

- **Ch 2-3**: Say, Print, basic I/O
- **Ch 5**: Loop-related (nothing special needed)
- **Ch 6**: Array operations
- **Ch 7**: Math functions for examples
- **Ch 8**: All string functions
- **Ch 9**: File functions
- **Ch 13**: Survey of entire library

---

## Effort Estimate (Revised)

| Part | Chapters | Est. Pages | Effort | Notes |
|------|----------|------------|--------|-------|
| I. Foundations | 7 (+getting started) | 80 | High | Core teaching, must be excellent |
| II. Building Blocks | 6 | 60 | Medium | Uses existing runtime docs |
| III. Objects | 5 | 50 | Medium | Strong OOP examples exist |
| IV. Applications | 6 | 70 | High | Frogger project is substantial |
| V. Mastery | 4 | 30 | Medium | Existing architecture docs |
| Appendices | 6 | 40 | Low | Mostly reformatting |
| **Total** | **34** | **~330 pages** | | |

---

## Priority Order

1. **Part I (Foundations)** — Must be finished first; establishes voice and foundation
2. **Part IV, Ch 21 (Game Project)** — Most compelling demonstration of what's possible
3. **Part II (Building Blocks)** — Practical skills
4. **Part III (Objects)** — Conceptual depth
5. **Appendices** — Reference material (can extract from existing docs)
6. **Part V (Mastery)** — Advanced topics
7. **Part IV (rest)** — Specialized applications
