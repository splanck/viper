# Getting Started

Before we begin our journey, you need to set up your tools. This won't take long.

---

## What You Need

**A computer.** Windows, macOS, or Linux — any will work. Viper runs on all of them.

**A text editor.** This is where you'll write your programs. Any editor works:
- **VS Code** (free, popular, works everywhere)
- **Sublime Text** (fast, clean)
- **Notepad++** (Windows, simple)
- Or even the basic Notepad/TextEdit that came with your computer

You don't need anything fancy. Programmers have strong opinions about editors, but as a beginner, just pick one and start. You can always switch later.

**The Viper toolchain.** This is the software that turns your programs into something the computer can run.

---

## Installing Viper

### macOS

Open Terminal (find it in Applications → Utilities) and run:

```bash
# Download and build Viper
git clone https://github.com/example/viper.git
cd viper
cmake -S . -B build
cmake --build build -j
```

Add Viper to your path so you can run it from anywhere:

```bash
export PATH="$PATH:$(pwd)/build/bin"
```

### Linux

Same as macOS. Open a terminal and run the commands above.

### Windows

*Coming soon: Windows installation instructions*

---

## Verify It Works

Let's make sure everything is installed correctly. In your terminal, type:

```bash
viper --version
```

You should see something like:

```
Viper 0.1.3
```

If you see an error like "command not found," the installation didn't work. Go back and check each step.

---

## Your First File

Let's create a tiny program to make sure everything works together.

1. Open your text editor
2. Create a new file
3. Type this exactly:

```rust
module Hello;

func start() {
    Viper.Terminal.Say("It works!");
}
```

4. Save it as `hello.viper` somewhere you can find it (your Desktop is fine for now)

5. In your terminal, navigate to where you saved the file:

```bash
cd ~/Desktop  # or wherever you saved it
```

6. Run your program:

```bash
viper hello.viper
```

You should see:

```
It works!
```

If you do — congratulations! Your development environment is ready. If not, double-check that you typed the program exactly as shown, and that you're in the right directory.

---

## If Something Goes Wrong

**"command not found"** — Viper isn't in your PATH. Make sure you ran the `export PATH=...` command, or use the full path to the viper executable.

**"file not found"** — You're not in the same directory as your file. Use `ls` (macOS/Linux) or `dir` (Windows) to see what files are in your current directory.

**Syntax error** — You probably have a typo. Compare your code character-by-character with the example above. Pay attention to semicolons, braces, and quotes.

---

## A Note on Typing

You might be tempted to copy and paste the code examples. Don't.

Type them yourself, character by character. It feels slower, but it's how you learn. Your fingers need to develop muscle memory. Your brain needs to process each piece. Mistakes are good — they teach you what the error messages mean.

Professional programmers still make typos every day. The difference is they know how to read error messages and fix them quickly. You'll develop that skill too, but only through practice.

---

*Ready? Let's learn what computers actually do.*

*[Continue to Chapter 1: The Machine →](01-the-machine.md)*
