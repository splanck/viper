//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_enums.cpp
// Purpose: Unit tests for BASIC enum declarations, OOP index population,
//          and lowering to IL constants.
// Key invariants:
//   - ENUM declarations parse with END ENUM terminator
//   - Enum variants are auto-incrementing or explicitly valued
//   - Enum variant access (Tint.RED) lowers to ConstInt
//   - BASIC lexer uppercases all identifiers; assertions use uppercase names
// Ownership/Lifetime:
//   - Test-only file
// Links: frontends/basic/ast/StmtDecl.hpp, frontends/basic/OopIndex.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/Semantic_OOP.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

namespace {

struct ParseResult {
    std::unique_ptr<Program> program;
    size_t errors = 0;
    std::vector<std::string> messages;
};

ParseResult parseSource(const std::string &src) {
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();

    ParseResult result;
    result.program = std::move(program);
    result.errors = de.errorCount();
    for (const auto &d : de.diagnostics())
        result.messages.push_back(d.message);
    return result;
}

// ===== Parsing =====

TEST(BasicEnums, ParseEnumDecl) {
    auto result = parseSource("ENUM Shade\n"
                              "  RED\n"
                              "  GREEN\n"
                              "  BLUE\n"
                              "END ENUM\n"
                              "PRINT 0\n"
                              "END\n");

    EXPECT_TRUE(result.program != nullptr);
    if (result.errors > 0) {
        for (const auto &msg : result.messages)
            std::cerr << "  [DIAG] " << msg << "\n";
    }
    EXPECT_EQ(result.errors, 0u);

    // Find the enum decl in the program (should be in main)
    bool foundEnum = false;
    for (const auto &stmt : result.program->main) {
        if (stmt && stmt->stmtKind() == Stmt::Kind::EnumDecl) {
            const auto &ed = static_cast<const EnumDecl &>(*stmt);
            EXPECT_EQ(ed.name, "SHADE");
            EXPECT_EQ(ed.members.size(), 3u);
            EXPECT_EQ(ed.members[0].name, "RED");
            EXPECT_EQ(ed.members[1].name, "GREEN");
            EXPECT_EQ(ed.members[2].name, "BLUE");
            foundEnum = true;
        }
    }
    EXPECT_TRUE(foundEnum);
}

TEST(BasicEnums, ParseEnumExplicitValues) {
    auto result = parseSource("ENUM HttpStatus\n"
                              "  OK = 200\n"
                              "  NOT_FOUND = 404\n"
                              "  SERVER_ERROR = 500\n"
                              "END ENUM\n"
                              "PRINT 0\n"
                              "END\n");

    EXPECT_TRUE(result.program != nullptr);
    EXPECT_EQ(result.errors, 0u);

    for (const auto &stmt : result.program->main) {
        if (stmt && stmt->stmtKind() == Stmt::Kind::EnumDecl) {
            const auto &ed = static_cast<const EnumDecl &>(*stmt);
            EXPECT_EQ(ed.name, "HTTPSTATUS");
            EXPECT_TRUE(ed.members[0].value.has_value());
            EXPECT_EQ(ed.members[0].value.value(), 200);
            EXPECT_TRUE(ed.members[1].value.has_value());
            EXPECT_EQ(ed.members[1].value.value(), 404);
            EXPECT_TRUE(ed.members[2].value.has_value());
            EXPECT_EQ(ed.members[2].value.value(), 500);
        }
    }
}

TEST(BasicEnums, ParseEnumNegativeValues) {
    auto result = parseSource("ENUM Offset\n"
                              "  BACKWARD = -1\n"
                              "  NONE = 0\n"
                              "  FORWARD = 1\n"
                              "END ENUM\n"
                              "PRINT 0\n"
                              "END\n");

    EXPECT_TRUE(result.program != nullptr);
    EXPECT_EQ(result.errors, 0u);

    for (const auto &stmt : result.program->main) {
        if (stmt && stmt->stmtKind() == Stmt::Kind::EnumDecl) {
            const auto &ed = static_cast<const EnumDecl &>(*stmt);
            EXPECT_TRUE(ed.members[0].value.has_value());
            EXPECT_EQ(ed.members[0].value.value(), -1);
        }
    }
}

// ===== OOP Index =====

TEST(BasicEnums, OopIndexPopulation) {
    // Use "Tint" instead of "Color" to avoid keyword collision (COLOR is a BASIC keyword)
    auto result = parseSource("ENUM Tint\n"
                              "  RED\n"
                              "  GREEN\n"
                              "  BLUE\n"
                              "END ENUM\n"
                              "PRINT 0\n"
                              "END\n");

    EXPECT_TRUE(result.program != nullptr);

    OopIndex index;
    buildOopIndex(*result.program, index, nullptr);

    // Verify enum is in the index (names are uppercased by the lexer)
    auto val = index.findEnumVariant("TINT", "RED");
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 0);

    val = index.findEnumVariant("TINT", "GREEN");
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);

    val = index.findEnumVariant("TINT", "BLUE");
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 2);

    // Unknown variant
    val = index.findEnumVariant("TINT", "YELLOW");
    EXPECT_FALSE(val.has_value());

    // Unknown enum
    val = index.findEnumVariant("SHAPE", "CIRCLE");
    EXPECT_FALSE(val.has_value());
}

TEST(BasicEnums, OopIndexExplicitValues) {
    auto result = parseSource("ENUM Priority\n"
                              "  LOW\n"
                              "  MEDIUM = 5\n"
                              "  HIGH\n"
                              "  CRITICAL\n"
                              "END ENUM\n"
                              "PRINT 0\n"
                              "END\n");

    EXPECT_TRUE(result.program != nullptr);

    OopIndex index;
    buildOopIndex(*result.program, index, nullptr);

    EXPECT_EQ(index.findEnumVariant("PRIORITY", "LOW").value(), 0);
    EXPECT_EQ(index.findEnumVariant("PRIORITY", "MEDIUM").value(), 5);
    EXPECT_EQ(index.findEnumVariant("PRIORITY", "HIGH").value(), 6);
    EXPECT_EQ(index.findEnumVariant("PRIORITY", "CRITICAL").value(), 7);
}

TEST(BasicEnums, OopIndexDuplicateVariantError) {
    // Use "Tint" to avoid keyword collision with COLOR
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    const std::string src = "ENUM Tint\n"
                            "  RED\n"
                            "  GREEN\n"
                            "  RED\n"
                            "END ENUM\n"
                            "PRINT 0\n"
                            "END\n";

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    EXPECT_TRUE(program != nullptr);

    OopIndex index;
    buildOopIndex(*program, index, &emitter);

    EXPECT_TRUE(de.errorCount() > 0);
}

// ===== Lowering =====

TEST(BasicEnums, LowerEnumVariantAccess) {
    // Use "Tint" to avoid keyword collision with COLOR
    const std::string src = "ENUM Tint\n"
                            "  RED\n"
                            "  GREEN\n"
                            "  BLUE\n"
                            "END ENUM\n"
                            "LET x = Tint.GREEN\n"
                            "PRINT x\n"
                            "END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    EXPECT_TRUE(program != nullptr);

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    Lowerer lowerer;
    auto mod = lowerer.lower(*program);

    EXPECT_EQ(de.errorCount(), 0u);
}

// ===== Keyword-as-name =====

TEST(BasicEnums, ParseEnumWithKeywordName) {
    // COLOR is a BASIC keyword — verify the parser accepts it as an enum name
    auto result = parseSource("ENUM Color\n"
                              "  RED\n"
                              "  GREEN\n"
                              "END ENUM\n"
                              "PRINT 0\n"
                              "END\n");

    EXPECT_TRUE(result.program != nullptr);
    EXPECT_EQ(result.errors, 0u);

    bool foundEnum = false;
    for (const auto &stmt : result.program->main) {
        if (stmt && stmt->stmtKind() == Stmt::Kind::EnumDecl) {
            const auto &ed = static_cast<const EnumDecl &>(*stmt);
            EXPECT_EQ(ed.name, "COLOR");
            EXPECT_EQ(ed.members.size(), 2u);
            foundEnum = true;
        }
    }
    EXPECT_TRUE(foundEnum);
}

} // anonymous namespace

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
