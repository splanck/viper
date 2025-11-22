//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_rodata_pool.cpp
// Purpose: Validate AArch64 rodata pooling: dedup and label emission.
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/RodataPool.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

using viper::codegen::aarch64::RodataPool;

TEST(AArch64Rodata, DedupAndEmit)
{
    il::core::Module m;
    m.globals.push_back({"@.L0", il::core::Type(il::core::Type::Kind::Str), std::string("Hello")});
    m.globals.push_back({"@.L1", il::core::Type(il::core::Type::Kind::Str), std::string("Hello")});
    m.globals.push_back({"@.L2", il::core::Type(il::core::Type::Kind::Str), std::string("World\n")});

    RodataPool pool;
    pool.buildFromModule(m);

    std::ostringstream os;
    pool.emit(os);
    const std::string text = os.str();

#if defined(__APPLE__)
    EXPECT_NE(text.find(".section __TEXT,__const\n"), std::string::npos);
#else
    EXPECT_NE(text.find(".section .rodata\n"), std::string::npos);
#endif
    // Expect two labels only (deduped "Hello")
    EXPECT_NE(text.find("L.str.0:"), std::string::npos);
    EXPECT_NE(text.find("L.str.1:"), std::string::npos);
    // Paylods
    EXPECT_NE(text.find("  .asciz \"Hello\"\n"), std::string::npos);
    EXPECT_NE(text.find("  .asciz \"World\\n\"\n"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

