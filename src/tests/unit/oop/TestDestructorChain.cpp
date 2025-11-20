// File: tests/unit/oop/TestDestructorChain.cpp
// Purpose: Verify destructor chaining order: derived body then base body.
// Invariant: After DISPOSE Derived, global g records 12 (1 then 2).

#if __has_include(<gtest/gtest.h>)
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

TEST(BasicOOPDestructorChainTest, DerivedThenBase)
{
    constexpr const char *src = R"BASIC(
CLASS B
  DESTRUCTOR
    LET g = g * 10 + 2
  END DESTRUCTOR
END CLASS

CLASS D : B
  DESTRUCTOR
    LET g = g * 10 + 1
  END DESTRUCTOR
END CLASS

DIM g AS INTEGER
DIM o AS D
LET o = NEW D()
DISPOSE o
IF g <> 12 THEN
  PRINT 1/(0)
END IF
END
)BASIC";

    SourceManager sm;
    uint32_t fileId = sm.addFile("dtor_chain.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fileId, std::string(src));

    Parser parser(src, fileId, &em);
    auto program = parser.parseProgram();
    ASSERT_TRUE(program);

    Lowerer lowerer;
    auto module = lowerer.lowerProgram(*program);

    viper::tests::VmFixture fx;
    // Should not trap when order is correct
    (void)fx.run(module);
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
