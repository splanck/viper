# Viper: The Case for a New Kind of Game Development Platform

*Internal overview — business strategy, technical foundation, and investment case*
*Last updated: 2026-07-01*

**Guiding principles of this document: full disclosure and transparency.** Every claim in it is verifiable against the repository, and the case against Viper is presented with the same care as the case for it. If this document ever reads as advocacy rather than analysis, it has failed at its job. The external messaging built from it should inherit the same standard: we win by being the platform whose claims get *stronger* when a skeptic checks them.

---

## The Premise

Every mainstream game development platform is an engine built on top of someone else's everything else. Unity runs on Mono/.NET. Godot vendors over fifty third-party libraries. Unreal bundles hundreds, and both lean on external compilers and linkers. The engines are impressive. The foundations are borrowed.

Viper owns the entire stack: the languages, the compiler, the optimizer, the assembler, the linker, the bytecode VM, the native code generators, the runtime library, the graphics engine, the physics, the audio, the GUI framework, the game engine, and the IDE. There is no vendored code and no third-party directory.

Full ownership is not, by itself, something customers pay for — that point is argued honestly in the risks section below. What it *buys* is the product experience that customers do pay for: install one tool, write source files, run one command, get a native binary that runs anywhere with nothing to install. And it buys a property that matters more every year: a project that is **100% code**, with no opaque editor state, which makes it unusually legible to both humans and AI coding agents.

---

## What Has Been Built

From a standing start on August 28, 2025 — roughly ten months of development, with commits on 286 of the first 308 days.

### The Scale

Using the project's own counter (`./scripts/count_sloc.sh`, run 2026-07-01): **663,761 production SLOC** across 3,398 source files, plus 255,111 lines of tests, 165,011 lines of demo applications written in the platform's own languages, and 25,568 lines of ViperIDE. Overall: just over 1.1 million SLOC, produced across 4,976 commits — an average of roughly 490 commits per month sustained for ten months.

### How It Was Built — Full Disclosure

Viper is developed by one engineer directing AI coding agents at scale. That fact is disclosed here, will be disclosed in public messaging, and is a strength to be owned rather than a caveat to be buried. Ten months of daily architectural direction, spec authorship, review, and debugging is the engineering; the agents are the workforce. What kept a million-line AI-assisted codebase coherent is a discipline stack that most human teams don't maintain: a normative IL specification, 24 architecture decision records with implementation traceability, conventional-commit hygiene, golden-file regression tests, fuzz harnesses, and a hard rule that the VM and native backends produce identical output for every defined program. The process is itself a demonstration of the platform's own thesis: when everything is code and the specs are explicit, AI-assisted development works at scale.

### Two Complete Language Compilers

**Zia** is a modern, statically typed language in the tradition of Swift and Kotlin: classes, structs, interfaces with dynamic dispatch, generics with constraints, lambdas and closures, pattern matching with exhaustiveness checking, optional types with null-safety operators, structured exception handling, enums, properties, and modules. It is complete end-to-end: every AST expression and statement kind has a lowering implementation.

**Viper BASIC** is an OOP-extended classic BASIC — classes, interfaces, inheritance, properties, structured exception handling — that remains backward-compatible with traditional syntax (`GOTO`, `GOSUB`, `FOR...TO...STEP`, `SELECT CASE`, `ON ERROR GOTO`). It is the education on-ramp and the nostalgia bridge.

Both languages compile through the same intermediate language, share one optimizer, one VM, and the same native backends. A project can mix files from both.

### A Real Compiler Pipeline

At the center is an 83-opcode, strongly typed, SSA-form intermediate language with a normative written specification (IL v0.3.0), a thorough verifier that rejects malformed programs before they reach a backend, and a registered O1/O2 optimizer pipeline: SSA promotion, SCCP, global value numbering, LICM, loop transforms, inlining, devirtualization, and cleanup passes.

Below the IL sit two native backends — x86-64 and AArch64 — each with rule-driven lowering, a real register allocator, scheduling, and peephole optimization, and each emitting binary machine code through its own assembler.

### A Real Linker

Viper's linker is not a wrapper around the system `ld`. It reads three object formats and writes native executables for three operating systems — ELF (Linux), Mach-O (macOS, with native code signing), and PE/COFF (Windows) — with symbol resolution, relocation, section merging, and identical-code-folding. `viper build` goes from source to a runnable native binary on any supported platform without invoking a single external tool.

### The Runtime Library

The runtime is the largest single component of the platform and the heart of its practical value: collections, strings and text processing, structured data (JSON/XML/CSV/TOML), a regex engine, networking (HTTP, HTTPS, WebSocket, TCP, UDP, DNS), 2D and 3D graphics with four rendering backends, a physics engine, pathfinding, skeletal animation, an asset pipeline with glTF/FBX import, audio with spatial mixing and effect chains, a 47-widget GUI framework, localization, threading, and file I/O. All of it implemented in-tree, all of it callable identically from both languages, and all of it inventoried live via `viper --dump-runtime-api`.

### Engineering Quality — Verifiable

- **1,740 tests** across 70 categories: unit, golden-file, end-to-end, conformance, and differential tests that assert VM output equals native output.
- **Determinism as a spec-level guarantee**: one program produces identical results on the tree-walking VM, the bytecode engines, and native builds.
- **24 ADRs** documenting architectural decisions with links to the implementing code and tests.
- **11 tagged releases** in ten months, each with detailed, metrics-backed release notes.
- Machine-readable tooling designed for editors and AI agents: `viper check`, `viper eval`, `viper explain`, LSP and MCP servers for both languages.

### Real Software, Not Toys

The examples tree contains a full IDE (with debugger, git integration, and an integrated terminal), a PostgreSQL-compatible SQL database engine written in ~85,000 lines of Zia, a drawing application, a multi-threaded web server, and 18 games — from an AI chess engine to a 21,000-line Metroidvania (XENOSCAPE) to 3D titles including bowling and an open-world rendering showcase. These are the artifacts that make a skeptic pause, and every one of them builds and runs from the public tree.

---

## Why Viper Is Different

Honesty about the competitive landscape requires naming *both* groups of competitors, because they attack from opposite directions.

### Versus the editor-centric engines (Unity, Unreal, Godot)

Most of a game built in these engines is not code: scene files, prefabs, serialized component hierarchies, inspector-configured values living in editor-managed formats. Viper games are 100% code — every scene, entity, behavior, and configuration value is a Zia or BASIC source file. A human can read the whole project; so can an AI agent, with no blind spots and no editor-state opacity. Viper ships LSP and MCP servers today, so the AI-assisted workflow is present infrastructure, not a roadmap item.

Stated with appropriate care: this is a **head start, not a law of physics**. Godot's scene format is already text; every incumbent is racing to become AI-legible; agents are improving at serialized formats. The durable claim is that Viper was designed code-first from day one and will never carry editor-state legacy — not that competitors can never respond.

### Versus the code-first frameworks (Bevy, MonoGame, LÖVE, raylib)

This is the honest competitive neighborhood, and the previous version of this document failed to name it. These frameworks already offer code-first game development, they are free, and they have mature communities. Viper's differentiation against them is different in kind:

- **They are libraries; Viper is a platform.** A Bevy or MonoGame project begins with toolchain assembly: install a compiler ecosystem, manage a dependency graph, wire up asset handling, choose and integrate libraries for audio, networking, UI, and physics. Viper is one install: language, compiler, engine, IDE, debugger, and a batteries-included runtime, with `viper build` producing a self-contained native binary. The zero-setup, zero-ceremony experience is the product.
- **An integrated IDE with a VM debugger** — step, inspect locals, evaluate watch expressions — which none of the code-first frameworks provide as a first-party experience.
- **Two languages on one runtime**, including the only serious modern BASIC with a real 3D engine behind it — a genuinely unoccupied niche with proven nostalgic and educational demand (the QB64/BlitzBasic/Pico-8 lineage).

### The one-binary story

For an independent developer, a Viper game ships as a single self-contained executable per platform: no runtime to install, no DLLs to bundle, no packaging matrix. For anyone who has shipped a game off the beaten path of the big engines, this is a felt, daily-life advantage — and it is the *customer-facing* expression of the zero-dependency architecture.

---

## The Case Against Viper — Risks, Stated Plainly

Full disclosure means this section carries the same weight as everything above it. These are the reasons Viper could fail as a business, in rough order of severity.

**1. The new-language barrier is the tallest wall.** Adopting Viper means learning Zia — leaving behind an existing language, its libraries, its Stack Overflow corpus, and its asset ecosystems. Historically, this is what killed most engine-with-its-own-language businesses. The honest mitigations: BASIC as a familiar on-ramp for one audience segment; the reality that AI agents flatten new-language learning curves (an agent with the LSP/MCP servers and the machine-readable diagnostics is fluent in Zia immediately, and so, therefore, is its user); and a deliberately small, conventional language design. These mitigations are real but unproven at market scale. This risk must be addressed head-on in messaging, not hidden.

**2. Bus factor of one.** The platform is maintained by a single person. Prospective adopters betting a commercial project on Viper will ask what happens if the maintainer stops. Mitigations: the GPL guarantees the code can never be taken away from the community; the specification, ADR, and test discipline make the codebase unusually maintainable by others; and building a second maintainer and a contributor base is an explicit near-term goal. But today, this is a fair and serious objection.

**3. Desktop-only targets.** Viper compiles for Linux, macOS, and Windows. There is no console support (Switch/PlayStation/Xbox require proprietary NDA'd toolchains that conflict with the zero-dependency philosophy and are, at minimum, distant work) and no mobile support. A large share of indie revenue lives on exactly those platforms. Viper's honest near-term market is desktop — Steam and itch.io — and messaging must not imply otherwise.

**4. Ecosystem cold start.** No asset store, no third-party tutorials, no library ecosystem, no answers corpus, no hiring pool. Every platform starts here, but incumbents' network effects are the single hardest moat to cross. The dogfooding games, the free documentation book, and the education channel are the plan for seeding an ecosystem — and that plan takes years, not quarters.

**5. Zero dependencies is a permanent tax as well as a story.** Owning the whole stack means every bug in every subsystem is ours alone, forever, with no upstream maintainers sharing the load. The same property that eliminates upstream churn also forgoes upstream help. This is a deliberate trade, and the discipline record so far (1,740 tests, differential VM-vs-native checking, 24 ADRs) is the evidence it can be carried — but the cost compounds with every capability added, and it should temper enthusiasm for further runtime breadth.

**6. The market segment most likely to switch is price-sensitive.** The developers who left Unity in 2023 predominantly went to Godot — which is free. Viper's paid tiers bet that a meaningful slice of this market will pay modest, predictable prices for a more integrated, more trustworthy experience. That bet is plausible (Pico-8 and the Blitz lineage proved paid niche tooling works) but it is a bet, and the free tier must be genuinely excellent for the paid tiers to feel fair rather than extractive.

**7. Support burden arrives with the first paying customer.** A platform business is a support business. At solo scale, paid licenses create obligations that compete directly with development time. Sequencing (community first, paid tiers only when support is sustainable) and a second person on community/support are the plan; until then, this constrains how fast revenue should be turned on.

**8. Pre-1.0 surface instability.** APIs, diagnostics, and IL rules are still evolving (the tree is 0.2.x). Early adopters will experience breaking changes. The version-cadence commitment below is the mitigation, but it only takes effect at v1.0 — and declaring v1.0 prematurely would be worse than waiting.

**9. AI-authorship perception.** Some of the developer audience is reflexively skeptical of AI-built software. The mitigation is this document's founding principle: disclose it, explain the discipline stack that makes it trustworthy, and let the verifiable artifacts — the tests, the shipped games, the reproducible builds — carry the argument. Transparency converts this from a scandal-in-waiting into a differentiator; concealment would do the opposite.

---

## License Model

Viper is licensed under the **GNU GPL v3 for everyone**, with a **commercial license available for closed-source distribution**. This is classic dual licensing, and its mechanics deserve a transparent explanation, because the model only builds trust if it is explained precisely and honestly:

- **Everything is GPL-3, always.** The compiler, the runtime, the IDE — full source, forever. This is already the repository's license; nothing is being taken away, relicensed, or moved behind a wall. The free tier is not a limited edition; it is the entire platform.
- **Compiled programs statically link Viper's runtime.** Under the GPL, a distributed game containing the GPL runtime must itself be licensed GPL-3 — source available to its players. Important honesty note: the GPL does **not** forbid selling. Anyone may sell a GPL'd game commercially; what they cannot do is keep its source closed, and anyone may redistribute it. Open-source game developers therefore need nothing from us, ever.
- **The commercial license is a runtime-linking exception** that permits distributing closed-source programs built with Viper. That is the entire product: hobbyists, learners, educators, and open-source developers use everything free; developers shipping proprietary games buy a license.

Two structural prerequisites, both non-negotiable before launch:

- **A contributor license agreement from the first outside contribution.** Dual licensing is only possible while the copyright is unified. Today one person owns 100% of it; a single merged community patch without a CLA would permanently compromise the ability to grant commercial exceptions. The CLA must exist before the community does.
- **Professional legal review of the exception wording** before it is announced. Runtime-exception language is subtle, and retrofitting it after customers exist is far more expensive than getting it right once.

### Tiers

- **Open / Community** — free for everyone, forever, full platform, GPL terms. Games built on it are GPL and may be sold or given away.
- **Indie** — one-time purchase per major version (anchor: **$69**), grants the closed-source distribution exception for a solo developer. Upgrades to the next major version at roughly half price. One purchase decision, no subscription — a deliberate contrast with the model that broke trust at Unity.
- **Studio** — annual license for small teams: 3 seats at $500/year; Studio+ at $1,200/year for 10 seats.
- **Education** — 30 classroom seats at $200/year, priced so an institution can approve it without escalation. (Classroom use rarely requires the exception at all — this tier sells support, curriculum alignment, and institutional legitimacy.)
- **Enterprise** — negotiated, for publishers or companies embedding the platform.

### The cadence commitment

The indie model rests entirely on buyers knowing what they bought. A publicly committed major-version cadence of **18–24 months**, with all minor and patch releases free within a major version, converts the one-time purchase from an ambiguous promise into a concrete value proposition. This commitment is a product feature and belongs in the launch messaging — and it should only be made when v1.0 is genuinely ready to carry it.

---

## The Business Strategy

### Positioning

Viper enters as a **game development platform**, not another engine: one integrated, fully-owned stack where everything is code, one command produces one binary, and the whole system is legible to humans and AI agents alike. The pitch leads with the developer experience; full stack ownership is the explanation, not the headline. The platform's general-purpose depth — the database engine, networking, GUI — is a discovery that deepens commitment, not the opening argument.

### The Dogfooding Games

The most important marketing artifact is not a video or a blog post; it is a shipped commercial game. A **baseball manager simulation** — simulation-heavy, data-heavy, modest art budget, deep systems — will be developed entirely in Viper and sold on Steam as a real commercial product with private source. Its genre is chosen to exercise the runtime comprehensively (collections, serialization, AI simulation, UI) within a solo-buildable scope. Every production problem it surfaces is fixed before external developers ever hit it — a loop already proven by the 3D showcase demo, whose development log converted directly into compiler and runtime fixes.

Alongside it, a separate **openly developed showcase game** demonstrates the AI-assisted workflow in public: transparent commits, visible agent collaboration, documented process. Its job is not gameplay excellence; it is to show concretely what development looks like when an agent can participate in 100% of the work because 100% of the work is code.

### Growth Path

The initial audience is independent desktop game developers, reached where they already are: game-dev Reddit, itch.io, Discord communities, and game jams. The entry artifact is the demo reel — gameplay first, then the source that produced it, then the one-command build. **Show the game first. Show the code second. Show the stack third.**

The broader systems-programming community (Hacker News, language and compiler circles) finds the from-scratch story intrinsically interesting and becomes the amplifier channel. The education market is reached through BASIC plus the IDE plus native compilation — a complete, motivating pipeline from first line of code to a shipped playable game, priced for frictionless institutional purchase. The free documentation book creates the SEO and credibility foundation; a game jam, once installation is zero-friction, is the community capstone.

### The Moat, Stated Honestly

Three compounding advantages, each with its honest limit:

- **Integration.** The one-install, one-command, batteries-included experience is years of work to replicate and is the hardest thing for both library-style frameworks and editor-centric engines to match. Limit: integration is a moat only while the pieces stay excellent; it decays if breadth outruns maintenance.
- **Code-first + AI legibility.** A structural head start that compounds as agent tooling improves. Limit: incumbents are actively closing this gap; the advantage has a window, not a guarantee.
- **Shipped games.** Each commercial title built on the platform is compounding credibility no marketing can buy. Limit: this moat exists only after the games ship, and shipping good games is its own hard business.

---

## The Opportunity — Sized From the Bottom Up

The honest math, replacing the top-down market hand-wave:

- At the **$69** indie anchor, 1,000 licenses ≈ $69K; education at $200/year needs institutional counts in the dozens to matter; studio tiers require a trust level that follows, not precedes, a v1.0 and shipped games.
- A **modest success** — an engaged niche community, several hundred indie licenses a year, a handful of education customers, plus revenue from the dogfooded games — is a five-figure annual side business that fully funds the project's costs and proves the model.
- A **solid success** — thousands of cumulative licenses, a real education footprint, early studio adoption — is a low-six-figure business that supports full-time work and a small team.
- A **seven-figure outcome** almost certainly requires one of two events: a hit game built on the platform (the path by which most engines historically earned their standing), or meaningful studio/enterprise adoption post-1.0. It is the tail outcome, planned for but not depended on. Every stage of this ladder is worth reaching even if the next rung never arrives — that is what makes the plan survivable.

The platform is further along than any external observer would expect from ten months of development, and the remaining distance to v1.0 is product work, not research: freezing the API surface, committing to the cadence, shipping the baseball game, polishing installation to zero friction, and establishing the launch narrative — in our own words, with full disclosure, before anyone else frames it for us.

The platform exists. The code is real. The games run. The claims survive inspection. The work now is going to market — transparently.

---

*SLOC figures from `./scripts/count_sloc.sh` run 2026-07-01 (production 663,761 / test 255,111 / demo 165,011 / ViperIDE 25,568 / source files 3,398). Commit and cadence figures from the repository history (4,976 commits, 2025-08-28 through 2026-07-01). Opcode, test, ADR, and release counts verified by direct inspection.*
