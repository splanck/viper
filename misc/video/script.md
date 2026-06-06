# Viper — Public Introduction Video (Flagship Deep-Dive)

**Angle:** systems / compiler story · **Target length:** 14–16 min ·
**AI narrative:** mentioned briefly near the end, tech-first ·
**Tone:** confident but honest (pre-alpha). VO = voiceover, SCREEN = what's on screen.

The arc escalates: *approachable → transparent internals → native payoff →
spectacle → scale reveal.* Demo commands live in `runbook.md` (all verified).

---

## ACT 0 — Cold open (0:00–0:20)
**SCREEN:** No narration. 8s montage — Game3D Showcase (terrain/water/skybox),
a flash of XENOSCAPE, the chess GUI. Hard cut to a blank dark terminal.
**VO (over the cut):** "Everything you just saw — the language it's written in,
the compiler, the optimizer, the engine that renders it — was built from scratch.
No LLVM. No game engine. No dependencies. Let me show you how deep this goes."
**SCREEN:** Title card — **VIPER**.

## ACT 1 — It's a real language (0:20–2:00)
**VO:** "Viper's flagship language is called Zia — statically typed, with classes,
generics, pattern matching. But let's start small."
**SCREEN:** `viper repl` → `Say("Hello from Viper")` → `Say(Fmt.Int(2 + 3))` → `5`.
**VO:** "Friendly enough for a REPL one-liner. Now watch what it actually does under
the hood — because *that's* the part nobody else shows you."
**SCREEN:** Open `misc/video/demo.zia` — the square-of-a-loop program. Read it once.

## ACT 2 — The magic trick: transparency (2:00–6:00)
**VO:** "When you compile a Zia program, it doesn't go straight to a black box. It
lowers to Viper IL — a typed, SSA-based intermediate language you can read."
**SCREEN:** `viper build demo.zia -O0 -o demo_O0.il`; scroll to `func @square`.
**VO:** "This is the naive translation. Every variable is a memory slot — store it,
load it, load it again, multiply. Honest, but slow. Now turn on the optimizer."
**SCREEN:** `viper build demo.zia -o demo.il`; show `square` collapsed to
`imul.ovf %t1, %t1 / ret`.
**VO:** "Twenty-four optimization passes — value numbering, loop transforms,
inlining. The function collapses to a single multiply..."
**SCREEN:** Scroll to `@main`, highlight `forin_body_1.inline.square.entry_0`.
**VO:** "...and it gets inlined straight into the loop. You can see it right there in
the block name. By the way — that `.ovf` means checked multiply. Zia is
overflow-safe by default; the language won't silently wrap an integer on you."
**VO (button):** "This readable IR is the whole design philosophy: make the
semantics explicit, and the front-end languages become interchangeable."

## ACT 3 — The payoff: native code, zero dependencies (6:00–8:30)
**VO:** "The IL can run on Viper's virtual machine. Or it can become a real
executable — and here's where it gets serious."
**SCREEN:** `viper build demo.zia -o demo_native` → `file demo_native` →
`Mach-O 64-bit executable arm64` → run it.
**VO:** "That's a native Mach-O binary. And here's the part I want to be precise
about: Viper did not call out to LLVM, or gcc, or clang, or the system linker. It
has its *own* code generator, its *own* assembler, and its *own* linker — emitting
Mach-O on macOS, ELF on Linux, PE on Windows, with real DWARF debug info. Zero
external tools in the native path."
**SCREEN:** Side by side: VM output vs native output — identical.
**VO:** "Same program, two completely different execution engines, byte-identical
output. That determinism is a guarantee, not a coincidence."

## ACT 4 — The spectacle (8:30–12:30)
**VO:** "So that's the foundation. Now — what can you build on it?"
**SCREEN:** Chess (drag-drop + AI thinking).
**VO:** "Chess, with an alpha-beta search opponent and a real GUI."
**SCREEN:** Paint (layers, undo). VipersSQL (a query running).
**VO:** "A paint program with layers and undo. A SQL database engine with an
interactive client. None of this pulls in a third-party library — it's all the
Viper runtime."
**SCREEN:** XENOSCAPE gameplay (60fps cut).
**VO:** "A Metroid-style game — bosses, abilities, save files."
**SCREEN:** Game3D Showcase — slow pan over terrain, water reflections, skybox, PBR.
**VO:** "And a full 3D runtime — terrain, water, a skybox, physically based
materials, post-processing. The same toolchain that compiled `hello world` a minute
ago is rendering this."

## ACT 5 — The scale reveal + honest status (12:30–15:30)
**SCREEN:** Architecture diagram from the README, built up layer by layer.
**VO:** "Step back and look at the whole stack. Front-end languages — Zia and a
BASIC dialect. A typed SSA intermediate language with a verifier. A 24-pass
optimizer. A bytecode VM *and* native back-ends for x86-64 and ARM64. A built-in
assembler and linker. And a runtime spanning graphics, 3D, audio, networking,
threads, a GUI, and a game engine."
**SCREEN:** Fast scroll through the source tree; "macOS · Linux · Windows" badge.
**VO:** "Hundreds of thousands of lines of code, the entire thing from scratch, and
it runs on all three major desktop platforms."
**VO (AI mention — brief, tech-first):** "And yes — this was built by one person,
working with AI assistance, over about nine months. I think that says something
about where solo engineering is heading. But I'd rather you judge the work than the
process — so go read the IL, build it, break it."
**VO (honesty):** "One thing I want to be straight about: Viper is pre-alpha. The
language, the IL, and the tooling are still moving. It's ready for experimentation
and game prototyping — not for production. That's the honest status."

## ACT 6 — Close / CTA (15:30–end)
**SCREEN:** GitHub repo, GPLv3 badge, `git clone` + `viper run` quickstart.
**VO:** "It's open source under the GPL. Clone it, run the demos, read the
intermediate language — the link's below. If you build compilers, write languages,
or just like seeing a whole stack laid bare — I think you'll find something here.
Thanks for watching."
**SCREEN:** End card — repo URL, "Viper · an IL-first toolchain & VM".

---

## Title options (systems angle)
1. **I built a programming language, compiler, AND game engine from scratch (no dependencies)**
2. **My language compiles to native code with its own assembler and linker — no LLVM**
3. **Watch source code become a native binary — the whole pipeline, nothing hidden**
4. **One person. One compiler, VM, linker, and game engine. Zero dependencies.**

## Thumbnail
Split frame: **left** = the clean IL (`imul.ovf %t1, %t1 / ret`) on a dark
terminal; **right** = the 3D Showcase. Overlay text: **"FROM SCRATCH — NO LLVM"**.
Face optional; the code/game contrast is the hook.

## Distribution
YouTube as home base, then drive traffic via: Show HN, r/programming,
r/ProgrammingLanguages, lobste.rs (`compilers` tag). Lead every post with the
zero-dependency native-pipeline angle — that's what those communities argue about.
