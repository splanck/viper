//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/oop/TestDispose.cpp
// Purpose: Validate DISPOSE null is no-op and double DISPOSE traps (debug). 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "tests/common/VmFixture.hpp"

using namespace il::frontends::basic;
using namespace il::support;

TEST(BasicDisposeTest, DisposeNullNoTrap)
{
    constexpr const char *src = R"BASIC(
CLASS C
  DESTRUCTOR
  END DESTRUCTOR
END CLASS

DIM o AS C
DISPOSE o  ' null; should be no-op
END
)BASIC";

    SourceManager sm;
    uint32_t fid = sm.addFile("dispose_null.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fid, std::string(src));

    Parser p(src, fid, &em);
    auto prog = p.parseProgram();
    ASSERT_TRUE(prog);

    Lowerer lowerer;
    auto module = lowerer.lowerProgram(*prog);

    viper::tests::VmFixture fx;
    (void)fx.run(module);
}

TEST(BasicDisposeTest, DoubleDisposeTriggersTrap)
{
    constexpr const char *src = R"BASIC(
CLASS C
  DESTRUCTOR
  END DESTRUCTOR
END CLASS

DIM o AS C
LET o = NEW C()
DISPOSE o
DISPOSE o  ' second dispose should trap in debug via disposed-guard
END
)BASIC";

    SourceManager sm;
    uint32_t fid = sm.addFile("dispose_double.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fid, std::string(src));

    Parser p(src, fid, &em);
    auto prog = p.parseProgram();
    ASSERT_TRUE(prog);

    Lowerer lowerer;
    auto module = lowerer.lowerProgram(*prog);

    viper::tests::VmFixture fx;
    auto trap = fx.runExpectingTrap(module);
    EXPECT_TRUE(trap.exited);
}

int main(int argc, char **argv)
{
#if __has_include(<gtest/gtest.h>)
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
#else
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
#endif
}
