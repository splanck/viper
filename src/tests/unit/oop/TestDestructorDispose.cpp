//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/oop/TestDestructorDispose.cpp
// Purpose: Validate parsing of DESTRUCTOR (including STATIC DESTRUCTOR) and 
// Key invariants: Parser accepts the new forms without diagnostics and builds
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/StmtExpr.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

using namespace il::frontends::basic;
using namespace il::support;

TEST(BasicOOPDestructorDisposeTest, ParsesStaticDestructorInsideClass)
{
    constexpr const char *src = R"BASIC(
CLASS K
  STATIC DESTRUCTOR
    PRINT 1
  END DESTRUCTOR
END CLASS
END
)BASIC";

    SourceManager sm;
    uint32_t fid = sm.addFile("static_dtor.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fid, std::string(src));

    Parser p(src, fid, &em);
    auto prog = p.parseProgram();
    ASSERT_TRUE(prog);
    EXPECT_EQ(em.errorCount(), 0u);
}

TEST(BasicOOPDestructorDisposeTest, DisposeParsesAsStatement)
{
    constexpr const char *src = R"BASIC(
DISPOSE obj
END
)BASIC";

    SourceManager sm;
    uint32_t fid = sm.addFile("dispose_stmt.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fid, std::string(src));

    Parser p(src, fid, &em);
    auto prog = p.parseProgram();
    ASSERT_TRUE(prog);
    ASSERT_FALSE(prog->main.empty());
    auto *del = dynamic_cast<DeleteStmt *>(prog->main.front().get());
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(em.errorCount(), 0u);
}

TEST(BasicOOPDestructorDisposeTest, DestructorRejectsAccessModifiers)
{
    constexpr const char *src = R"BASIC(
CLASS C
  PUBLIC DESTRUCTOR
    PRINT 0
  END DESTRUCTOR
END CLASS
END
)BASIC";

    SourceManager sm;
    uint32_t fid = sm.addFile("dtor_access.bas");
    DiagnosticEngine eng;
    DiagnosticEmitter em(eng, sm);
    em.addSource(fid, std::string(src));

    Parser p(src, fid, &em);
    auto prog = p.parseProgram();
    ASSERT_TRUE(prog);
    EXPECT_TRUE(em.errorCount() >= 1u);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
