---
status: active
audience: public
last-verified: 2026-05-31
---

# Getting Started on Linux

This guide walks you through installing and running the Viper compiler toolchain on Linux. Commands are provided for both Debian/Ubuntu and Fedora/RHEL families.

---

## Prerequisites

| Tool | Minimum Version | Purpose |
|------|-----------------|---------|
| C++20 compiler | Clang 14+ or GCC 11+ | Compile Viper from source |
| CMake | 3.20 | Build system generator |
| Make | any | Build executor (ships with most distros) |
| Git | any | Clone the repository |

### Install the Compiler and Build Tools

The build script prefers Clang when available, but GCC works equally well.

**Debian / Ubuntu:**

```bash
sudo apt update
sudo apt install clang cmake make git
```

**Fedora / RHEL:**

```bash
sudo dnf install clang cmake make git
```

Verify your compiler version:

```bash
clang++ --version   # should show 14.0 or newer
# or
g++ --version       # should show 11 or newer
```

Verify CMake:

```bash
cmake --version     # must be 3.20 or higher
```

### Optional: Graphics and Audio Libraries

Viper's graphics and audio subsystems require platform libraries. These are optional — the core compiler, VM, and language frontends build and run without them.

**Debian / Ubuntu:**

```bash
sudo apt install libx11-dev libasound2-dev
```

**Fedora / RHEL:**

```bash
sudo dnf install libX11-devel alsa-lib-devel
```

If these packages are not installed, the build will print a warning and skip the graphics/audio libraries. Everything else builds normally.

---

## Clone and Build

Clone the repository and run the build script:

```bash
git clone https://github.com/splanck/viper.git
cd viper
./scripts/build_viper_linux.sh
```

The build script will:

1. Detect your compiler (prefers Clang, falls back to GCC)
2. Configure the project with CMake
3. Build all targets in parallel (using all available cores)
4. Run the test suite
5. Install binaries to `/usr/local` (prompts for `sudo` if not root)

A successful build ends with output similar to:

```
[100%] Built target viper
...
100% tests passed, 0 tests failed
...
Install complete.
```

---

## Verify the Installation

After building, confirm Viper is working:

```bash
viper --version
```

You should see the version string (e.g., `viper v0.2.x-dev`) followed by the IL version. If the command is not found, ensure `/usr/local/bin` is in your `PATH`:

```bash
echo $PATH | tr ':' '\n' | grep /usr/local/bin
```

If missing, add it to your shell profile:

```bash
echo 'export PATH="/usr/local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Installing a Release Package

Release `.deb` and `.rpm` toolchain packages install `viper`, headers, runtime archives,
CMake package files, manpages, and desktop/MIME associations. Install them through the
distribution package manager so runtime dependencies are resolved and developer
prerequisites are offered. CMake, `make`, a C++ compiler, and cache utilities are
recommendations rather than hard dependencies, so minimal environments are not
over-constrained:

```bash
# Debian / Ubuntu
sudo apt install ./viper_<version>_amd64.deb

# Fedora / RHEL
sudo dnf install ./viper-<version>-1.x86_64.rpm
```

Portable `.tar.gz` packages include `install.sh`, `uninstall.sh`, and an install
manifest. `install.sh` defaults to `/usr/local`, honors `PREFIX` and `DESTDIR`,
and removes stale files from the previous Viper tarball manifest before copying
the new payload in a rollback-capable transaction. A FUSE-less `.run` bundle is
also available through `viper install-package --target linux-bundle`; it verifies
its payload and reuses a content-addressed private XDG cache.

See the [installer and package release guide](../installer-release.md) for
checksums, OpenPGP release signing, and disposable-host upgrade/uninstall tests.

---

## Your First Program

Create a file called `hello.zia` with the following content:

```zia
module Hello;

bind Viper.Terminal;

func start() {
    Say("Hello, World!");
}
```

Run it:

```bash
viper run hello.zia
```

**Expected output:**

```
Hello, World!
```

**What this program does:**

- `module Hello;` declares the module name (required in every Zia file)
- `bind Viper.Terminal;` imports the Terminal module so you can call its functions directly
- `func start()` is the program entry point (like `main()` in C)
- `Say()` prints a line of text to the console with a trailing newline

---

## What to Read Next

- **[Zia Tutorial](../zia-getting-started.md)** — Learn Zia by example: variables, control flow, functions, classes, and generics
- **[Zia Reference](../zia-reference.md)** — Complete language reference
- **[BASIC Tutorial](../basic-language.md)** — Viper also ships a BASIC frontend
- **[Getting Started (general)](../getting-started.md)** — Project creation with `viper init`, the REPL, IL programs, and the full command reference

---

## Troubleshooting

### 1. "No C++ compiler found" or CMake generator error

**Symptom:** CMake exits with:

```
CMake Error: CMAKE_CXX_COMPILER not set, not able to find compiler
```

or the build script prints:

```
Error: No suitable C++ compiler found.
```

**Cause:** Neither Clang nor GCC is installed.

**Fix:**

```bash
# Debian / Ubuntu
sudo apt install clang
# or
sudo apt install g++-11

# Fedora / RHEL
sudo dnf install clang
# or
sudo dnf install gcc-c++
```

After installing, re-run the build script. Verify with `clang++ --version` or `g++ --version`.

### 2. "X11 not found" or "ALSA not found" warnings

**Symptom:** During the CMake configure step you see status lines like:

```
-- ViperGFX: disabled (X11 not found; install libx11-dev/libX11-devel or set VIPER_GRAPHICS_MODE=OFF)
-- ViperAUD: disabled (ALSA not found; install libasound2-dev/alsa-lib-devel or set VIPER_AUDIO_MODE=OFF)
```

**Cause:** The development headers for X11 or ALSA are not installed. The build continues, but the graphics and/or audio libraries are skipped.

**Fix (if you need graphics or audio):**

```bash
# Debian / Ubuntu
sudo apt install libx11-dev libasound2-dev

# Fedora / RHEL
sudo dnf install libX11-devel alsa-lib-devel
```

Then clean and rebuild:

```bash
rm -rf build
./scripts/build_viper_linux.sh
```

If you only need the compiler, VM, and language frontends, these warnings are safe to ignore.

### 3. CMake version too old

**Symptom:** CMake prints:

```
CMake Error at CMakeLists.txt:1:
  CMake 3.20 or higher is required.  You are running version 3.16.
```

**Cause:** Older LTS distributions (e.g., Ubuntu 20.04) ship CMake 3.16, which is below the 3.20 minimum.

**Fix — Option A: Install from the Kitware APT repository (Debian/Ubuntu):**

```bash
sudo apt install gpg wget
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' | sudo tee /etc/apt/sources.list.d/kitware.list
sudo apt update
sudo apt install cmake
```

**Fix — Option B: Install via snap:**

```bash
sudo snap install cmake --classic
```

**Fix — Option C (Fedora):** Fedora typically ships a recent CMake. If yours is outdated:

```bash
sudo dnf upgrade cmake
```

After upgrading, verify with `cmake --version` and re-run the build.
