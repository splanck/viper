// File: tests/codegen/x86_64/test_diff_vm_native.cpp
// Purpose: Ensure ilc executes the same IL module via the VM and native JIT
//          paths with matching stdout and exit codes.
// Key invariants: The synthesized IL program exercises integer, floating-point,
//                 and branch instructions while producing deterministic output.
// Ownership/Lifetime: Test owns temporary files/directories and deletes them
//                      once comparisons complete.
// Links: src/tools/ilc/main.cpp

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef VIPER_TEST_ILC_PATH
#error "VIPER_TEST_ILC_PATH must be defined by the build system"
#endif

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#define VIPER_HAS_GTEST 0
#endif

namespace
{

[[nodiscard]] std::string quoteArgument(const std::string &arg)
{
    std::string quoted;
    quoted.reserve(arg.size() + 2);
    quoted.push_back('"');
    for (char ch : arg)
    {
        if (ch == '"')
        {
            quoted += "\\\"";
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

[[nodiscard]] std::string readFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] int decodeExitCode(int status)
{
    if (status == -1)
    {
        return -1;
    }
#if defined(_WIN32)
    return status;
#else
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return status;
#endif
}

struct CommandResult
{
    int exitCode{};
    std::string stdoutText;
};

[[nodiscard]] CommandResult runIlcCommand(const std::vector<std::string> &args,
                                          const std::filesystem::path &stdoutPath)
{
    std::ostringstream command;
    command << quoteArgument(VIPER_TEST_ILC_PATH);
    for (const std::string &arg : args)
    {
        command << ' ' << quoteArgument(arg);
    }
    command << " >" << quoteArgument(stdoutPath.string()) << " 2>&1";

    const int status = std::system(command.str().c_str());
    CommandResult result{};
    result.exitCode = decodeExitCode(status);
    result.stdoutText = readFile(stdoutPath);
    return result;
}

struct DiffResult
{
    bool ok{false};
    std::string message;
};

[[nodiscard]] DiffResult compareVmAndNative()
{
    constexpr std::string_view kProgram = R"(il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void

global const str @.Lnl = "\n"
global const str @.Ltrue = "branch-then"
global const str @.Lfalse = "branch-else"

func @main() -> i64 {
entry:
  %sum = iadd.ovf 4, 6
  call @rt_print_i64(%sum)
  %nl0 = const_str @.Lnl
  call @rt_print_str(%nl0)
  %mix = fadd 1.25, 2.75
  call @rt_print_f64(%mix)
  %nl1 = const_str @.Lnl
  call @rt_print_str(%nl1)
  %cond = scmp_gt %sum, 5
  cbr %cond, label then_block, label else_block

then_block:
  %msg_then = const_str @.Ltrue
  call @rt_print_str(%msg_then)
  %nl2 = const_str @.Lnl
  call @rt_print_str(%nl2)
  ret 0

else_block:
  %msg_else = const_str @.Lfalse
  call @rt_print_str(%msg_else)
  %nl3 = const_str @.Lnl
  call @rt_print_str(%nl3)
  ret 1
}
)";

    DiffResult diff{};

    std::error_code ec;
    const std::filesystem::path baseTemp = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        diff.message = "Failed to query temp directory: " + ec.message();
        return diff;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch();
    const std::string dirName = "viper-diff-" +
                                std::to_string(static_cast<long long>(timestamp.count()));
    std::filesystem::path tempDir = baseTemp / dirName;

    std::filesystem::create_directories(tempDir, ec);
    if (ec)
    {
        diff.message = "Failed to create temp directory: " + ec.message();
        return diff;
    }

    struct TempDirGuard
    {
        std::filesystem::path path;
        ~TempDirGuard()
        {
            if (!path.empty())
            {
                std::error_code removeEc;
                std::filesystem::remove_all(path, removeEc);
            }
        }
    } guard{tempDir};

    const std::filesystem::path ilPath = tempDir / "program.il";
    {
        std::ofstream ilFile(ilPath, std::ios::binary);
        ilFile << kProgram;
        if (!ilFile)
        {
            diff.message = "Failed to write IL program";
            return diff;
        }
    }

    const std::filesystem::path vmStdoutPath = tempDir / "vm.out";
    const std::filesystem::path nativeStdoutPath = tempDir / "native.out";

    const CommandResult vm = runIlcCommand({"-run", ilPath.string()}, vmStdoutPath);
    const CommandResult native = runIlcCommand({"codegen", "x64", "--run-native", ilPath.string()}, nativeStdoutPath);

    if (vm.exitCode == -1)
    {
        diff.message = "Failed to launch ilc for VM run";
        return diff;
    }
    if (native.exitCode == -1)
    {
        diff.message = "Failed to launch ilc for native run";
        return diff;
    }

    if (vm.exitCode != native.exitCode)
    {
        std::ostringstream msg;
        msg << "Exit codes differ: vm=" << vm.exitCode << " native=" << native.exitCode;
        diff.message = msg.str();
        return diff;
    }

    if (vm.stdoutText != native.stdoutText)
    {
        std::ostringstream msg;
        msg << "Stdout differs:\n-- VM --\n" << vm.stdoutText << "\n-- Native --\n" << native.stdoutText;
        diff.message = msg.str();
        return diff;
    }

    diff.ok = true;
    return diff;
}

} // namespace

#if VIPER_HAS_GTEST

TEST(CodegenX64DiffVmNativeTest, VmAndNativeOutputsMatch)
{
    const DiffResult diff = compareVmAndNative();
    ASSERT_TRUE(diff.ok) << diff.message;
}

#else

int main()
{
    const DiffResult diff = compareVmAndNative();
    if (!diff.ok)
    {
        std::cerr << diff.message << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
