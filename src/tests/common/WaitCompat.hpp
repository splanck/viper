//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/common/WaitCompat.hpp
// Purpose: Cross-platform compatibility for process status macros.
//          Include this instead of <sys/wait.h> in test files.
//===----------------------------------------------------------------------===//
#pragma once

#ifdef _WIN32
// On Windows, system() returns the exit code directly without encoding.
// Provide stub macros for compatibility with POSIX tests.
#define WIFEXITED(status) true
#define WEXITSTATUS(status) (status)
#define WIFSIGNALED(status) false
#define WTERMSIG(status) 0
#else
#include <sys/wait.h>
#endif
