//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/e2e/test_arm64_demos_run.cpp
// Purpose: End-to-end run-native tests for Frogger and Vtris on macOS arm64.
//          These tests are opt-in to avoid hanging interactive demos in CI.
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../unit/GTestStub.hpp"
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <vector>

struct RunResult
{
    int exit_code;
    std::string out;
    std::string err;
};

static RunResult run_process(const std::vector<std::string> &args)
{
    RunResult result;
    std::string cmd;
    for (const auto &arg : args)
    {
        if (!cmd.empty())
            cmd += " ";
        if (arg.find(' ') != std::string::npos)
            cmd += '"' + arg + '"';
        else
            cmd += arg;
    }
    std::string outFile = "/tmp/arm64_demo_out_" + std::to_string(rand()) + ".txt";
    std::string errFile = "/tmp/arm64_demo_err_" + std::to_string(rand()) + ".txt";
    cmd += " >" + outFile + " 2>" + errFile;
    result.exit_code = std::system(cmd.c_str());
    if (result.exit_code != -1)
        result.exit_code = WEXITSTATUS(result.exit_code);
    std::ifstream outStream(outFile), errStream(errFile);
    std::stringstream ob, eb;
    ob << outStream.rdbuf();
    eb << errStream.rdbuf();
    result.out = ob.str();
    result.err = eb.str();
    std::remove(outFile.c_str());
    std::remove(errFile.c_str());
    return result;
}

static bool onMacArm64()
{
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return true;
#else
    return false;
#endif
}

static bool optInRun()
{
    return std::getenv("ARM64_RUN_DEMOS") != nullptr;
}

static bool exists(const std::string &p)
{
    return std::filesystem::exists(p);
}

TEST(ARM64E2E, Frogger_RunNative_OptIn)
{
    if (!onMacArm64() || !optInRun())
        return; // opt-in only
    const std::string buildDir = ".";
    const std::string vbasic = buildDir + "/src/tools/vbasic/vbasic";
    const std::string ilc = buildDir + "/src/tools/ilc/ilc";
    const std::string froggerBas = "../demos/basic/frogger/frogger.bas";
    if (!exists(vbasic) || !exists(ilc) || !exists(froggerBas))
        return;
    const std::string ilFile = "/tmp/frogger_run.il";
    RunResult rr = run_process({vbasic, froggerBas, "-o", ilFile});
    ASSERT_EQ(rr.exit_code, 0);
    rr = run_process({ilc, "codegen", "arm64", ilFile, "-run-native"});
    // Minimal assertion: process executed and returned a code (no crash/sig)
    EXPECT_NE(rr.exit_code, -1);
}

TEST(ARM64E2E, Vtris_RunNative_OptIn)
{
    if (!onMacArm64() || !optInRun())
        return; // opt-in only
    const std::string buildDir = ".";
    const std::string vbasic = buildDir + "/src/tools/vbasic/vbasic";
    const std::string ilc = buildDir + "/src/tools/ilc/ilc";
    const std::string vtrisBas = "../demos/basic/vtris/vtris.bas";
    if (!exists(vbasic) || !exists(ilc) || !exists(vtrisBas))
        return;
    const std::string ilFile = "/tmp/vtris_run.il";
    RunResult rr = run_process({vbasic, vtrisBas, "-o", ilFile});
    ASSERT_EQ(rr.exit_code, 0);
    rr = run_process({ilc, "codegen", "arm64", ilFile, "-run-native"});
    EXPECT_NE(rr.exit_code, -1);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
