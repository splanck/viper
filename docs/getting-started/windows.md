---
status: active
audience: public
last-verified: 2026-03-05
---

# Getting Started on Windows

This guide walks you through installing and running the Viper compiler toolchain on Windows 10/11. All commands use **PowerShell**.

---

## Prerequisites

| Tool | Minimum Version | Purpose |
|------|-----------------|---------|
| Visual Studio 2019+ (or Build Tools) | with "Desktop development with C++" workload | Provides MSVC compiler, linker, and Windows SDK |
| CMake | 3.20 | Build system generator |
| Git | any | Clone the repository |

### Install Visual Studio Build Tools

You need either the full Visual Studio IDE or the standalone Build Tools. The critical requirement is the **"Desktop development with C++"** workload, which provides the MSVC compiler and the Windows SDK.

**Option A — Install via `winget` (Build Tools only, no IDE):**

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools
```

After installation, open **Visual Studio Installer**, click **Modify** on your Build Tools installation, and ensure **"Desktop development with C++"** is checked.

**Option B — Install Visual Studio Community (free, includes IDE):**

```powershell
winget install Microsoft.VisualStudio.2022.Community
```

During installation (or via Modify), select the **"Desktop development with C++"** workload.

### Install CMake

CMake is bundled with the Visual Studio C++ workload. To verify it is available, open a **Developer PowerShell for VS** and run:

```powershell
cmake --version
```

If CMake is not found or the version is below 3.20, install it separately:

```powershell
winget install Kitware.CMake
```

Close and reopen your terminal after installing so the `PATH` update takes effect.

### Install Git

```powershell
winget install Git.Git
```

Close and reopen your terminal, then verify:

```powershell
git --version
```

### Optional: Install LLVM/Clang

The build script prefers `clang-cl` (the MSVC-compatible Clang driver) when available. This is optional — MSVC alone is sufficient.

```powershell
winget install LLVM.LLVM
```

No additional libraries are required. The Windows SDK (included with the C++ workload) provides GDI, WASAPI, and all other system APIs that Viper uses.

---

## Clone and Build

Open a **Developer PowerShell for VS** (this ensures the compiler and CMake are in your `PATH`) and run:

```powershell
git clone https://github.com/splanck/viper.git
cd viper
.\scripts\build_viper.cmd
```

The build script will:

1. Detect your compiler (prefers `clang-cl` if installed, otherwise MSVC)
2. Configure the project with CMake
3. Build all targets in **Debug** configuration
4. Run the test suite

A successful build ends with output similar to:

```
Build succeeded.
...
100% tests passed, 0 tests failed
```

> **Note:** Unlike macOS and Linux, the Windows build script does **not** install binaries system-wide. The built executables remain in the `build\` directory.

---

## Verify the Installation

After building, run the Viper binary directly from the build directory:

```powershell
.\build\src\tools\viper\Debug\viper.exe --version
```

You should see the version string (e.g., `viper 0.2.3-snapshot`).

To make `viper` available from any directory in your current session, add it to your `PATH`:

```powershell
$env:PATH += ";$PWD\build\src\tools\viper\Debug"
viper --version
```

To make this permanent, add the full path to your system `PATH` via **Settings > System > About > Advanced system settings > Environment Variables**, or run:

```powershell
[Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";C:\path\to\viper\build\src\tools\viper\Debug", "User")
```

Replace `C:\path\to\viper` with your actual clone location.

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

```powershell
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

### 1. "No CMAKE_CXX_COMPILER could be found"

**Symptom:** CMake exits with:

```
CMake Error at CMakeLists.txt:
  No CMAKE_CXX_COMPILER could be found.
```

**Cause:** The **"Desktop development with C++"** workload is not installed in Visual Studio, or you are not running from a Developer Command Prompt.

**Fix:**

1. Open **Visual Studio Installer**.
2. Click **Modify** on your VS or Build Tools installation.
3. Check **"Desktop development with C++"** and click **Modify** to install.
4. After installation, open a fresh **Developer PowerShell for VS** and re-run the build.

If you have the workload installed but CMake still cannot find the compiler, ensure you are running from a Developer prompt (not a plain PowerShell window). You can launch one from the Start menu by searching for **"Developer PowerShell for VS"**.

### 2. `cmake` is not recognized as a command

**Symptom:**

```
cmake : The term 'cmake' is not recognized as the name of a cmdlet
```

**Cause:** CMake is not in your `PATH`. This happens when using a regular PowerShell window instead of the Developer prompt, or when CMake was not installed.

**Fix — Option A:** Open **Developer PowerShell for VS** from the Start menu. This environment automatically includes CMake in the `PATH`.

**Fix — Option B:** Install CMake standalone and restart your terminal:

```powershell
winget install Kitware.CMake
```

Close and reopen PowerShell, then verify:

```powershell
cmake --version
```

### 3. Build succeeds but `viper` command is not found

**Symptom:** The build completes successfully, but running `viper --version` produces:

```
viper : The term 'viper' is not recognized as the name of a cmdlet
```

**Cause:** On Windows the build script does not install binaries to a system directory. The `viper.exe` binary is located inside the build tree.

**Fix:** Add the build output directory to your `PATH` for the current session:

```powershell
$env:PATH += ";$PWD\build\src\tools\viper\Debug"
viper --version
```

Or run the binary directly:

```powershell
.\build\src\tools\viper\Debug\viper.exe --version
```

To make this permanent, add the full path to your user `PATH` environment variable (see the [Verify the Installation](#verify-the-installation) section above).
