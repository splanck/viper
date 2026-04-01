//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_native_linker.cpp
// Purpose: Coverage for top-level native linker target gating and diagnostics.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/NativeLinker.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

int main() {
    NativeLinkerOptions opts;
    opts.platform = LinkPlatform::Windows;
    opts.arch = LinkArch::AArch64;
    opts.objPath = "does-not-matter.obj";
    opts.exePath = "does-not-matter.exe";

    std::ostringstream out;
    std::ostringstream err;
    const int rc = nativeLink(opts, out, err);

    CHECK(rc != 0);
    CHECK(err.str().find("Windows ARM64 executable linking is not implemented yet") !=
          std::string::npos);

    if (gFail == 0) {
        std::cout << "All NativeLinker tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " NativeLinker test(s) FAILED.\n";
    return EXIT_FAILURE;
}
