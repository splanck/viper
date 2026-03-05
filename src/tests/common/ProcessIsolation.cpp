//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/ProcessIsolation.cpp
// Purpose: Implement cross-platform process isolation for trap tests.
//          POSIX: fork + pipe + waitpid.
//          Windows: CreateProcess self-relaunch + Job Object + pipe.
// Key invariants:
//   - runIsolated() and runModuleIsolated() always return to the parent.
//   - Windows child processes are killed when the Job Object handle closes.
//   - IL module serialization round-trips exactly through Serializer/Parser.
// Ownership/Lifetime:
//   - Temp files created for module transfer are cleaned up by the parent.
//   - All OS handles (pipes, job objects, process handles) are RAII-closed.
// Links: tests/common/ProcessIsolation.hpp
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Must come after windows.h
#include <io.h>
#include <process.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace viper::tests
{

// ─── Shared state for Windows child dispatch ────────────────────────────────

static std::function<void()> g_childFunction;
static const char *const kChildRunFlag = "--viper-child-run";
static const char *const kChildILFlag = "--viper-child-il=";

void setChildFunction(std::function<void()> fn)
{
    g_childFunction = std::move(fn);
}

// ─── Windows implementation ─────────────────────────────────────────────────

#if defined(_WIN32)

/// Suppress all Windows debug/error dialogs in the child process.
static void suppressDialogs()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif
}

/// RAII wrapper for Windows HANDLEs.
struct HandleGuard
{
    HANDLE h = INVALID_HANDLE_VALUE;
    ~HandleGuard()
    {
        if (h != INVALID_HANDLE_VALUE && h != nullptr)
            CloseHandle(h);
    }
    HandleGuard() = default;
    explicit HandleGuard(HANDLE handle) : h(handle) {}
    HandleGuard(const HandleGuard &) = delete;
    HandleGuard &operator=(const HandleGuard &) = delete;
    HandleGuard(HandleGuard &&o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    HandleGuard &operator=(HandleGuard &&o) noexcept
    {
        if (this != &o)
        {
            if (h != INVALID_HANDLE_VALUE && h != nullptr)
                CloseHandle(h);
            h = o.h;
            o.h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return h; }
    HANDLE release() noexcept
    {
        HANDLE tmp = h;
        h = INVALID_HANDLE_VALUE;
        return tmp;
    }
};

/// Launch the current executable as a child process with the given extra
/// argument, capturing stderr. The child is sandboxed in a Job Object.
static ChildResult launchChild(const std::string &extraArg, unsigned timeoutMs)
{
    ChildResult result;

    // Get path to current executable
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0)
    {
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: GetModuleFileNameA failed";
        return result;
    }

    // Build command line: "exePath" extraArg
    std::string cmdLine = std::string("\"") + exePath + "\" " + extraArg;

    // Create pipe for stderr capture
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE pipeRead = INVALID_HANDLE_VALUE;
    HANDLE pipeWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&pipeRead, &pipeWrite, &sa, 0))
    {
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: CreatePipe failed";
        return result;
    }
    HandleGuard readGuard(pipeRead);
    HandleGuard writeGuard(pipeWrite);

    // Ensure the read end is NOT inherited by the child
    SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0);

    // Create Job Object with kill-on-close
    HandleGuard job(CreateJobObjectA(nullptr, nullptr));
    if (job.get() == nullptr)
    {
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: CreateJobObjectA failed";
        return result;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
    jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));

    // Set up child STARTUPINFO with stderr redirected to pipe
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = pipeWrite;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr,                          // lpApplicationName
                        const_cast<char *>(cmdLine.c_str()), // lpCommandLine
                        nullptr,                          // lpProcessAttributes
                        nullptr,                          // lpThreadAttributes
                        TRUE,                             // bInheritHandles
                        CREATE_SUSPENDED,                 // dwCreationFlags
                        nullptr,                          // lpEnvironment
                        nullptr,                          // lpCurrentDirectory
                        &si,                              // lpStartupInfo
                        &pi))                             // lpProcessInformation
    {
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: CreateProcessA failed (error " +
                            std::to_string(GetLastError()) + ")";
        return result;
    }
    HandleGuard processGuard(pi.hProcess);
    HandleGuard threadGuard(pi.hThread);

    // Assign child to Job Object before resuming
    AssignProcessToJobObject(job.get(), pi.hProcess);
    ResumeThread(pi.hThread);

    // Close the write end of the pipe in the parent so reads see EOF
    writeGuard = HandleGuard();

    // Read stderr from child
    std::string buffer;
    std::array<char, 512> temp{};
    DWORD bytesRead = 0;
    while (ReadFile(pipeRead, temp.data(), static_cast<DWORD>(temp.size()), &bytesRead, nullptr) &&
           bytesRead > 0)
    {
        buffer.append(temp.data(), static_cast<std::size_t>(bytesRead));
    }

    // Wait for child to exit
    DWORD waitResult =
        WaitForSingleObject(pi.hProcess, timeoutMs == 0 ? INFINITE : static_cast<DWORD>(timeoutMs));

    if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: child timed out after " +
                            std::to_string(timeoutMs) + "ms\n" + buffer;
        return result;
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    result.stderrText = std::move(buffer);
    result.exited = true;
    result.exitCode = static_cast<int>(exitCode);
    return result;
}

ChildResult runIsolated(std::function<void()> childFn, unsigned timeoutMs)
{
    setChildFunction(std::move(childFn));
    return launchChild(kChildRunFlag, timeoutMs);
}

ChildResult runModuleIsolated(il::core::Module &module, unsigned timeoutMs)
{
    // Serialize the module to a temp file
    char tempPath[MAX_PATH];
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    GetTempFileNameA(tempDir, "vpr", 0, tempPath);

    {
        std::ofstream ofs(tempPath);
        if (!ofs)
        {
            ChildResult r;
            r.exited = true;
            r.exitCode = -1;
            r.stderrText = "ProcessIsolation: failed to create temp file";
            return r;
        }
        il::io::Serializer::write(module, ofs, il::io::Serializer::Mode::Canonical);
    }

    std::string arg = std::string(kChildILFlag) + tempPath;
    ChildResult result = launchChild(arg, timeoutMs);

    // Clean up temp file
    DeleteFileA(tempPath);
    return result;
}

bool dispatchChild(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], kChildRunFlag) == 0)
        {
            suppressDialogs();
            if (g_childFunction)
            {
                g_childFunction();
            }
            _exit(0);
        }
        if (std::strncmp(argv[i], kChildILFlag, std::strlen(kChildILFlag)) == 0)
        {
            suppressDialogs();
            const char *path = argv[i] + std::strlen(kChildILFlag);
            std::ifstream ifs(path);
            if (!ifs)
            {
                std::fprintf(stderr, "ProcessIsolation: cannot open IL file: %s\n", path);
                _exit(2);
            }
            il::core::Module m;
            auto parseResult = il::io::Parser::parse(ifs, m);
            if (!parseResult)
            {
                std::fprintf(stderr, "ProcessIsolation: IL parse failed: %s\n", path);
                _exit(2);
            }
            il::vm::VM vm(m);
            vm.run();
            _exit(0);
        }
    }
    return false;
}

// ─── POSIX implementation ───────────────────────────────────────────────────

#else

ChildResult runIsolated(std::function<void()> childFn, unsigned timeoutMs)
{
    (void)timeoutMs; // POSIX: waitpid blocks; timeout not implemented

    ChildResult result;

    std::array<int, 2> fds{};
    if (::pipe(fds.data()) != 0)
    {
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: pipe() failed";
        return result;
    }

    // Flush parent stdio before fork to prevent duplicate buffered output
    std::fflush(stdout);
    std::fflush(stderr);

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        result.exited = true;
        result.exitCode = -1;
        result.stderrText = "ProcessIsolation: fork() failed";
        return result;
    }

    if (pid == 0)
    {
        // Child: redirect stderr to pipe, run the function, exit
        ::close(fds[0]);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);
        childFn();
        _exit(0);
    }

    // Parent: read stderr from child, wait for exit
    ::close(fds[1]);
    std::string buffer;
    std::array<char, 512> temp{};
    while (true)
    {
        const ssize_t count = ::read(fds[0], temp.data(), temp.size());
        if (count <= 0)
            break;
        buffer.append(temp.data(), static_cast<std::size_t>(count));
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    result.stderrText = std::move(buffer);
    result.exited = WIFEXITED(status);
    result.signaled = WIFSIGNALED(status);
    if (result.exited)
        result.exitCode = WEXITSTATUS(status);
    else if (result.signaled)
        result.exitCode = 128 + WTERMSIG(status);

    return result;
}

ChildResult runModuleIsolated(il::core::Module &module, unsigned timeoutMs)
{
    return runIsolated(
        [&module]()
        {
            il::vm::VM vm(module);
            vm.run();
        },
        timeoutMs);
}

bool dispatchChild([[maybe_unused]] int argc, [[maybe_unused]] char *argv[])
{
    // On POSIX, fork-based isolation doesn't need self-relaunch dispatch.
    return false;
}

#endif

} // namespace viper::tests
