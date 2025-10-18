// File: tests/unit/BasicOOP_Parsing.cpp
// Purpose: Validate BASIC OOP parser accepts a class with field, constructor,
//          method, and destructor without diagnostics.
// Key invariants: Parser reports zero diagnostics and produces ClassDecl with
//                 expected members when OOP is enabled.
// Ownership/Lifetime: Test owns parser, diagnostics, and resulting AST.
// Links: docs/codemap.md

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
constexpr std::string_view kClassSnippet = R"BASIC(
10 CLASS Klass
20   value AS INTEGER
30   SUB NEW()
40     LET value = 1
50   END SUB
60   SUB INC()
70     LET value = value + 1
80   END SUB
90   DESTRUCTOR
100    LET value = value
110  END DESTRUCTOR
120 END CLASS
130 END
)BASIC";

[[nodiscard]] bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        const unsigned char lc = static_cast<unsigned char>(lhs[i]);
        const unsigned char rc = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(lc) != std::tolower(rc))
            return false;
    }
    return true;
}
} // namespace

TEST(BasicOOPParsingTest, ParsesClassWithMembersWithoutDiagnostics)
{
#if VIPER_ENABLE_OOP
    SourceManager sourceManager;
    uint32_t fileId = sourceManager.addFile("basic_oop.bas");

    DiagnosticEngine engine;
    DiagnosticEmitter emitter(engine, sourceManager);
    emitter.addSource(fileId, std::string(kClassSnippet));

    Parser parser(kClassSnippet, fileId, &emitter);
    std::unique_ptr<Program> program = parser.parseProgram();

    ASSERT_TRUE(program);
    EXPECT_EQ(emitter.errorCount(), 0u);
    EXPECT_EQ(emitter.warningCount(), 0u);
    ASSERT_FALSE(program->main.empty());

    const auto *klass = dynamic_cast<const ClassDecl *>(program->main.front().get());
    ASSERT_NE(klass, nullptr);
    EXPECT_TRUE(equalsIgnoreCase(klass->name, "Klass"));
    ASSERT_EQ(klass->fields.size(), 1u);
    EXPECT_TRUE(equalsIgnoreCase(klass->fields.front().name, "value"));
    EXPECT_EQ(klass->fields.front().type, Type::I64);

    const ConstructorDecl *ctor = nullptr;
    const DestructorDecl *dtor = nullptr;
    const MethodDecl *inc = nullptr;
    for (const auto &member : klass->members)
    {
        if (!member)
            continue;
        switch (member->stmtKind())
        {
            case Stmt::Kind::ConstructorDecl:
                ctor = static_cast<const ConstructorDecl *>(member.get());
                break;
            case Stmt::Kind::DestructorDecl:
                dtor = static_cast<const DestructorDecl *>(member.get());
                break;
            case Stmt::Kind::MethodDecl:
            {
                const auto *method = static_cast<const MethodDecl *>(member.get());
                if (equalsIgnoreCase(method->name, "inc"))
                    inc = method;
                break;
            }
            default:
                break;
        }
    }

    ASSERT_NE(ctor, nullptr);
    ASSERT_NE(dtor, nullptr);
    ASSERT_NE(inc, nullptr);
    EXPECT_TRUE(ctor->params.empty());
    EXPECT_FALSE(ctor->body.empty());
    EXPECT_FALSE(dtor->body.empty());
    EXPECT_TRUE(inc->params.empty());
    EXPECT_FALSE(inc->body.empty());
#endif
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
