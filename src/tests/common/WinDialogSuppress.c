//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/common/WinDialogSuppress.c
// Purpose: Suppress all MSVC debug/error dialogs so tests run non-interactively.
//          This file is compiled into every test executable on Windows via CMake.
//          A CRT initializer ensures the suppression runs before main().
//===----------------------------------------------------------------------===//

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdlib.h>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

// .CRT$XIB initializers must return int (0 = success, non-zero = abort).
static int viper_suppress_win_dialogs(void)
{
    // Suppress abort() message box and Windows Error Reporting.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    // Suppress critical-error and GP-fault dialogs.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

#ifdef _DEBUG
    // Redirect CRT assertions/errors/warnings to stderr instead of dialogs.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif
    return 0;
}

// Place function pointer in CRT initializer section (.CRT$XIB) so it runs
// before main() and before C++ static constructors.
typedef int (*_viper_crt_init_fn)(void);
#pragma section(".CRT$XIB", long, read)
__declspec(allocate(".CRT$XIB"))
    static _viper_crt_init_fn viper_suppress_init_ = viper_suppress_win_dialogs;

#endif // _WIN32
