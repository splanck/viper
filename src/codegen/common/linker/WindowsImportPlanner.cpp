//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/WindowsImportPlanner.cpp
// Purpose: Windows DLL import planning and thunk generation for the native
//          linker.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/PlatformImportPlanner.hpp"

#include "codegen/common/linker/RelocConstants.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::linker {
namespace {

std::string stripImpPrefix(const std::string &name) {
    if (name.rfind("__imp_", 0) == 0)
        return name.substr(6);
    return name;
}

std::string stripLeadingUnderscores(const std::string &name) {
    size_t i = 0;
    while (i < name.size() && name[i] == '_')
        ++i;
    return (i > 0) ? name.substr(i) : name;
}

bool isLinuxMathSymbol(const std::string &name) {
    static const std::unordered_set<std::string> kMath = {
        "acos",  "acosf",  "asin",      "asinf",      "atan",   "atan2", "atan2f", "atanf",
        "ceil",  "ceilf",  "copysign",  "copysignf",  "cos",    "cosf",  "cosh",   "exp",
        "expf",  "fabs",   "fabsf",     "floor",      "floorf", "fmax",  "fmaxf",  "fmin",
        "fminf", "fmod",   "fmodf",     "hypot",      "ldexp",  "log",   "log10",  "log2",
        "logf",  "nan",    "pow",       "powf",       "round",  "roundf","sin",    "sinf",
        "sinh",  "sqrt",   "sqrtf",     "tan",        "tanf",   "tanh",  "trunc",  "truncf",
    };
    return kMath.count(name) != 0;
}

bool dllForImport(const std::string &name, bool debugRuntime, std::string &dllName) {
    const std::string stripped = stripLeadingUnderscores(name);

    static const std::unordered_set<std::string> kernel32 = {
        "ExitProcess","FreeLibrary","GetCurrentProcessId","GetCurrentThreadId","GetEnvironmentVariableA",
        "GetLastError","GetComputerNameA","GetComputerNameW","GetCurrentDirectoryW","GetModuleHandleW",
        "GetModuleFileNameA","GetProcAddress","GetProcessHeap","GetStartupInfoW","GetStdHandle",
        "GetSystemInfo","GetSystemTimeAsFileTime","GetTempPathA","HeapAlloc","HeapFree",
        "IsDebuggerPresent","InitOnceExecuteOnce","InitializeCriticalSection","InitializeSListHead",
        "LeaveCriticalSection","DeleteCriticalSection","EnterCriticalSection","MultiByteToWideChar",
        "OutputDebugStringA","RaiseException","SetEnvironmentVariableA","SetUnhandledExceptionFilter",
        "SetErrorMode","SetCurrentDirectoryW","SwitchToThread","WriteFile","GetTickCount",
        "GetTickCount64","AddVectoredExceptionHandler","GlobalAlloc","GlobalFree","GlobalLock",
        "GlobalUnlock","InitializeSRWLock","AcquireSRWLockExclusive","AcquireSRWLockShared",
        "ReleaseSRWLockExclusive","ReleaseSRWLockShared","QueryPerformanceCounter","VirtualQuery",
        "WideCharToMultiByte","Beep","CloseHandle","CancelIo","CreateEventA","CreateFileA",
        "CreateDirectoryW","CreatePipe","CreateProcessA","CreateThread","DeleteFileW","FindClose",
        "FindFirstFileA","FindFirstFileW","FindNextFileA","FindNextFileW","GetFileAttributesA",
        "GetFileAttributesW","GetFileSizeEx","GetConsoleMode","GetExitCodeProcess","GetFullPathNameW",
        "GetOverlappedResult","GetVersionExA","GetVersionExW","GlobalMemoryStatusEx",
        "InitializeConditionVariable","QueryPerformanceFrequency","ReadDirectoryChangesW","ReadFile",
        "MoveFileExA","MoveFileExW","RemoveDirectoryW","SetConsoleCP","SetConsoleMode",
        "SetConsoleOutputCP","ResetEvent","SetEvent","SetHandleInformation","Sleep",
        "SleepConditionVariableCS","WaitForMultipleObjects","WaitForSingleObject",
        "WakeAllConditionVariable","WakeConditionVariable",
    };
    static const std::unordered_set<std::string> user32 = {
        "AdjustWindowRect","BeginPaint","ClientToScreen","CloseClipboard","CreateWindowExW",
        "DefWindowProcW","DestroyWindow","DispatchMessageW","EmptyClipboard","EndPaint",
        "GetClipboardData","GetDC","GetKeyState","GetMonitorInfoA","GetSystemMetrics",
        "GetWindowLongA","GetWindowLongPtrA","GetWindowRect","IsClipboardFormatAvailable","IsIconic",
        "IsZoomed","LoadCursorA","MonitorFromWindow","OpenClipboard","PeekMessageW","RegisterClassExW",
        "RegisterClipboardFormatW","ReleaseDC","SetClipboardData","SetCursor","SetCursorPos",
        "SetForegroundWindow","SetWindowLongA","SetWindowLongPtrW","SetWindowPos","SetWindowTextW",
        "ShowCursor","ShowWindow","TranslateMessage","UpdateWindow",
    };
    static const std::unordered_set<std::string> gdi32 = {
        "CreateCompatibleDC","CreateDIBSection","DeleteDC","DeleteObject","GetDeviceCaps",
        "GetStockObject","SelectObject","StretchBlt",
    };
    static const std::unordered_set<std::string> shell32 = {"DragAcceptFiles","DragFinish","DragQueryFileA"};
    static const std::unordered_set<std::string> ole32 = {"CoCreateInstance","CoInitializeEx","CoUninitialize"};
    static const std::unordered_set<std::string> xinput = {"XInputGetState","XInputSetState"};
    static const std::unordered_set<std::string> advapi32 = {
        "CryptAcquireContextA","CryptAcquireContextW","CryptAcquireContext","CryptGenRandom","CryptCreateHash",
        "CryptDestroyHash","CryptDestroyKey","CryptImportPublicKeyInfo","CryptReleaseContext",
        "CryptSetHashParam","CryptVerifySignature","GetUserNameA","GetUserNameW",
    };
    static const std::unordered_set<std::string> bcrypt = {"BCryptGenRandom"};
    static const std::unordered_set<std::string> ws2_32 = {
        "WSACleanup","WSAStartup","WSAGetLastError","accept","bind","closesocket","connect","freeaddrinfo",
        "getaddrinfo","getnameinfo","getsockname","htonl","htons","inet_ntop","inet_pton","ioctlsocket",
        "getsockopt","listen","ntohl","ntohs","recv","recvfrom","select","send","sendto","setsockopt",
        "shutdown","socket",
    };
    static const std::unordered_set<std::string> iphlpapi = {"GetAdaptersAddresses"};
    static const std::unordered_set<std::string> crypt32 = {
        "CertAddEncodedCertificateToStore","CertCloseStore","CertCreateCertificateContext",
        "CertFreeCertificateChain","CertFreeCertificateContext","CertGetCertificateChain","CertOpenStore",
        "CertVerifyCertificateChainPolicy","CryptAcquireCertificatePrivateKey",
    };
    static const std::unordered_set<std::string> d3d11 = {"D3D11CreateDevice","D3D11CreateDeviceAndSwapChain"};
    static const std::unordered_set<std::string> d3dcompiler = {"D3DCompile","D3DCompile2","D3DCompileFromFile","D3DReflect"};
    static const std::unordered_set<std::string> ucrt = {
        "_Exit","_exit","__acrt_iob_func","acrt_iob_func","__local_stdio_printf_options",
        "__local_stdio_scanf_options","__stdio_common_vfprintf","stdio_common_vfprintf",
        "__stdio_common_vsprintf","stdio_common_vsprintf","__stdio_common_vsscanf","stdio_common_vsscanf",
        "_vfprintf_l","_vsscanf_l","abort","access","abs","aligned_free","aligned_malloc",
        "aligned_alloc","atexit","atof","atoi","atol","bsearch","calloc_dbg","calloc","ceil","ceilf",
        "clearerr","clock","close","cos","cosf","create_locale","dclass","dsign","dup","dup2","errno","exit",
        "fabs","fabsf","fdclass","fclose","fcntl","ferror","feof","fflush","fgetc","fgets","fileno","fmax",
        "fmaxf","fmin","fminf","floor","floorf","fmod","fmodf","fopen","fprintf","fputc","fputs","fread",
        "free","free_locale","freopen","fseek","fstat64i32","ftell","fwrite","getc","getcwd","getenv","getch",
        "getpid","hypot","isalnum","isalpha","isdigit","islower","isspace","isupper","isxdigit","isatty","kbhit",
        "llround","localeconv","log","log10","log2","logf","longjmp","lseek","malloc","memchr","memcmp","memcpy",
        "memmove","memset","nan","nearbyint","open","perror","pclose","posix_memalign","popen","pow","powf",
        "printf","putc","puts","qsort","raise","read","realloc","remove","rename","rewind","round","roundf",
        "setbuf","setenv","setjmp","setvbuf","sin","sinf","snprintf","sprintf","sqrt","sqrtf","sscanf","strcat",
        "strcat_s","strchr","strcmp","strcpy","strcpy_s","strdup","strerror","stricmp","strlen","strncat",
        "strnicmp","strncmp","strncpy","strstr","strtod","strtod_l","strtol","strtoll","strtok_s","strtoul",
        "strrchr","strftime","system","tan","tanf","time","time64","tmpfile","tmpnam","tolower","toupper","trunc",
        "truncf","ungetc","unlink","utime64","vfprintf","wcscmp","wcscpy","wcslen","wcsncmp","wcscpy_s","write",
        "_wmakepath_s","_wsplitpath_s","fseeki64","ftelli64","gmtime64_s","localtime64_s","lseeki64","mktime64",
        "set_abort_behavior","stat64i32","wassert",
    };
    static const std::unordered_set<std::string> debugOnlyUcrt = {"_CrtDbgReport","_CrtDbgReportW","CrtDbgReport"};

    if (kernel32.count(name) || kernel32.count(stripped)) { dllName = "kernel32.dll"; return true; }
    if (user32.count(name) || user32.count(stripped)) { dllName = "user32.dll"; return true; }
    if (gdi32.count(name) || gdi32.count(stripped)) { dllName = "gdi32.dll"; return true; }
    if (shell32.count(name) || shell32.count(stripped)) { dllName = "shell32.dll"; return true; }
    if (ole32.count(name) || ole32.count(stripped)) { dllName = "ole32.dll"; return true; }
    if (xinput.count(name) || xinput.count(stripped)) { dllName = "xinput1_4.dll"; return true; }
    if (advapi32.count(name) || advapi32.count(stripped)) { dllName = "advapi32.dll"; return true; }
    if (bcrypt.count(name) || bcrypt.count(stripped)) { dllName = "bcrypt.dll"; return true; }
    if (ws2_32.count(name) || ws2_32.count(stripped)) { dllName = "ws2_32.dll"; return true; }
    if (iphlpapi.count(name) || iphlpapi.count(stripped)) { dllName = "iphlpapi.dll"; return true; }
    if (crypt32.count(name) || crypt32.count(stripped)) { dllName = "crypt32.dll"; return true; }
    if (d3d11.count(name) || d3d11.count(stripped)) { dllName = "d3d11.dll"; return true; }
    if (d3dcompiler.count(name) || d3dcompiler.count(stripped)) { dllName = "d3dcompiler_47.dll"; return true; }

    if (name == "__C_specific_handler" || name == "__C_specific_handler_noexcept" ||
        name == "__current_exception" || name == "__current_exception_context" ||
        name == "_CxxThrowException" || name == "__CxxFrameHandler4" ||
        name == "__std_exception_copy" || name == "__std_exception_destroy" ||
        stripped == "__C_specific_handler" || stripped == "__C_specific_handler_noexcept" ||
        stripped == "__current_exception" || stripped == "__current_exception_context" ||
        stripped == "CxxThrowException" || stripped == "CxxFrameHandler4" ||
        stripped == "std_exception_copy" || stripped == "std_exception_destroy" ||
        name.rfind("__vcrt_", 0) == 0 || stripped.rfind("__vcrt_", 0) == 0) {
        dllName = debugRuntime ? "VCRUNTIME140D.dll" : "VCRUNTIME140.dll";
        return true;
    }

    if (name.find("@std@@") != std::string::npos || stripped.find("@std@@") != std::string::npos ||
        name.rfind("??", 0) == 0 || stripped.rfind("??", 0) == 0 ||
        name.rfind("?_", 0) == 0 || stripped.rfind("?_", 0) == 0) {
        dllName = debugRuntime ? "MSVCP140D.dll" : "MSVCP140.dll";
        return true;
    }

    if (isLinuxMathSymbol(name) || isLinuxMathSymbol(stripped) || ucrt.count(name) || ucrt.count(stripped)) {
        dllName = debugRuntime ? "ucrtbased.dll" : "ucrtbase.dll";
        return true;
    }
    if (debugRuntime && (debugOnlyUcrt.count(name) || debugOnlyUcrt.count(stripped))) {
        dllName = "ucrtbased.dll";
        return true;
    }
    return false;
}

std::string importNameForSymbol(const std::string &name) {
    static const std::unordered_map<std::string, std::string> remap = {
        {"atexit", "_crt_atexit"},
        {"close", "_close"},
        {"lseek", "_lseek"},
        {"_longjmp", "longjmp"},
        {"open", "_open"},
        {"read", "_read"},
        {"_setjmp", "setjmp"},
        {"strdup", "_strdup"},
        {"unlink", "_unlink"},
        {"write", "_write"},
    };
    auto it = remap.find(name);
    if (it != remap.end())
        return it->second;
    return name;
}

} // namespace

bool generateWindowsImports(LinkArch arch,
                            const std::unordered_set<std::string> &dynamicSyms,
                            bool debugRuntime,
                            WindowsImportPlan &plan,
                            std::ostream &err) {
    plan.obj.name = (arch == LinkArch::AArch64) ? "<winarm64-imports>" : "<win64-imports>";
    plan.obj.format = ObjFileFormat::COFF;
    plan.obj.is64bit = true;
    plan.obj.isLittleEndian = true;
    plan.obj.machine = (arch == LinkArch::AArch64) ? 0xAA64 : 0x8664;
    plan.obj.sections.push_back(ObjSection{});
    plan.obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    std::unordered_map<std::string, std::vector<std::string>> dllToFuncs;
    std::unordered_set<std::string> seenFuncs;
    for (const auto &sym : dynamicSyms) {
        if (sym == "__ImageBase")
            continue;
        const std::string base = stripImpPrefix(sym);
        if (isWindowsLinkerHelperSymbol(sym) || isWindowsLinkerHelperSymbol(base))
            continue;
        if (base.rfind("rt_", 0) == 0)
            continue;
        if (!seenFuncs.insert(base).second)
            continue;
        std::string dllName;
        if (!dllForImport(base, debugRuntime, dllName)) {
            err << "error: Windows import symbol '" << base
                << "' is unresolved but has no DLL mapping\n";
            return false;
        }
        dllToFuncs[dllName].push_back(base);
    }

    std::vector<std::string> dllNames;
    dllNames.reserve(dllToFuncs.size());
    for (const auto &[dll, _] : dllToFuncs)
        dllNames.push_back(dll);
    std::sort(dllNames.begin(), dllNames.end());

    for (const auto &dll : dllNames) {
        auto funcs = dllToFuncs[dll];
        std::sort(funcs.begin(), funcs.end());
        DllImport dllImport;
        dllImport.dllName = dll;
        dllImport.functions = funcs;
        for (const auto &fn : funcs) {
            const std::string importName = importNameForSymbol(fn);
            if (importName != fn)
                dllImport.importNames.emplace(fn, importName);
        }
        plan.imports.push_back(std::move(dllImport));

        for (const auto &fn : funcs) {
            const size_t slotOff = dataSec.data.size();
            dataSec.data.resize(slotOff + 8, 0);

            ObjSymbol slotSym;
            slotSym.name = "__imp_" + fn;
            slotSym.binding = ObjSymbol::Global;
            slotSym.sectionIndex = 2;
            slotSym.offset = slotOff;
            const uint32_t slotSymIdx = static_cast<uint32_t>(plan.obj.symbols.size());
            plan.obj.symbols.push_back(std::move(slotSym));

            const size_t stubOff = textSec.data.size();
            if (arch == LinkArch::AArch64) {
                textSec.data.insert(textSec.data.end(),
                                    {0x10, 0x00, 0x00, 0x90, 0x10, 0x02, 0x40, 0xF9, 0x00, 0x02, 0x1F, 0xD6});
            } else {
                textSec.data.insert(textSec.data.end(), {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});
            }

            ObjSymbol stubSym;
            stubSym.name = fn;
            stubSym.binding = ObjSymbol::Global;
            stubSym.sectionIndex = 1;
            stubSym.offset = stubOff;
            plan.obj.symbols.push_back(std::move(stubSym));

            if (arch == LinkArch::AArch64) {
                ObjReloc pageReloc;
                pageReloc.offset = stubOff + 0;
                pageReloc.type = coff_a64::kPageRel21;
                pageReloc.symIndex = slotSymIdx;
                pageReloc.addend = 0;
                textSec.relocs.push_back(pageReloc);

                ObjReloc loadReloc;
                loadReloc.offset = stubOff + 4;
                loadReloc.type = coff_a64::kPageOff12L;
                loadReloc.symIndex = slotSymIdx;
                loadReloc.addend = 0;
                textSec.relocs.push_back(loadReloc);
            } else {
                ObjReloc reloc;
                reloc.offset = stubOff + 2;
                reloc.type = coff_x64::kRel32;
                reloc.symIndex = slotSymIdx;
                reloc.addend = 0;
                textSec.relocs.push_back(reloc);
            }
        }

        dataSec.data.resize(dataSec.data.size() + 8, 0);
    }

    if (!textSec.data.empty())
        plan.obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (plan.obj.sections.size() == 1)
            plan.obj.sections.push_back(ObjSection{});
        plan.obj.sections.push_back(std::move(dataSec));
    }

    return true;
}

} // namespace viper::codegen::linker
