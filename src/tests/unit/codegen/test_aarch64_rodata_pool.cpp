//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_rodata_pool.cpp
// Purpose: Validate AArch64 rodata pooling: dedup and label emission.
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <sstream>
#include <string>

#include "codegen/aarch64/RodataPool.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

using zanna::codegen::aarch64::darwinTarget;
using zanna::codegen::aarch64::linuxTarget;
using zanna::codegen::aarch64::RodataPool;
using zanna::codegen::aarch64::windowsTarget;

TEST(AArch64Rodata, DedupAndEmit) {
    il::core::Module m;
    m.globals.push_back({"@.L0", il::core::Type(il::core::Type::Kind::Str), std::string("Hello")});
    m.globals.push_back({"@.L1", il::core::Type(il::core::Type::Kind::Str), std::string("Hello")});
    m.globals.push_back(
        {"@.L2", il::core::Type(il::core::Type::Kind::Str), std::string("World\n")});

    RodataPool pool;
    pool.buildFromModule(m);

    std::ostringstream os;
    pool.emit(os, darwinTarget());
    const std::string text = os.str();

    EXPECT_NE(text.find(".section __TEXT,__const\n"), std::string::npos);
    // Expect two labels only (deduped "Hello")
    EXPECT_NE(text.find("L.str.0:"), std::string::npos);
    EXPECT_NE(text.find("L.str.1:"), std::string::npos);
    // Payloads
    EXPECT_NE(text.find("  .asciz \"Hello\"\n"), std::string::npos);
    EXPECT_NE(text.find("  .asciz \"World\\n\"\n"), std::string::npos);
}

TEST(AArch64Rodata, TargetSpecificSectionsAndFixedWidthOctalEscapes) {
    il::core::Module m;
    m.globals.push_back({"@.L0",
                         il::core::Type(il::core::Type::Kind::Str),
                         std::string("A\x01"
                                     "B\x7f")});

    RodataPool pool;
    pool.buildFromModule(m);

    std::ostringstream linux;
    pool.emit(linux, linuxTarget());
    EXPECT_NE(linux.str().find(".section .rodata\n"), std::string::npos);
    EXPECT_NE(linux.str().find("A\\001B\\177"), std::string::npos);

    std::ostringstream windows;
    pool.emit(windows, windowsTarget());
    EXPECT_NE(windows.str().find(".section .rdata,\"dr\"\n"), std::string::npos);
    EXPECT_NE(windows.str().find("A\\001B\\177"), std::string::npos);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, &argv);
    return zanna_test::run_all_tests();
}
