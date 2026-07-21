//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_frontend_common.cpp
// Purpose: Exercise shared frontend utilities at correctness boundaries.
// Key invariants:
//   - Constant folding never performs undefined signed arithmetic.
//   - Literal parsers consume their complete input deterministically.
// Ownership/Lifetime:
//   - Test values and cursor source storage live for the duration of each case.
// Links: src/frontends/common, docs/internals/testing.md
//
//===----------------------------------------------------------------------===//

#include "frontends/common/ConstantFolding.hpp"
#include "frontends/common/BlockManager.hpp"
#include "frontends/common/DiagnosticFormatter.hpp"
#include "frontends/common/EscapeSequences.hpp"
#include "frontends/common/ExprResult.hpp"
#include "frontends/common/InstructionEmitter.hpp"
#include "frontends/common/KeywordTable.hpp"
#include "frontends/common/LexerBase.hpp"
#include "frontends/common/LoopContext.hpp"
#include "frontends/common/NameMangler.hpp"
#include "frontends/common/NumberParsing.hpp"
#include "frontends/common/RuntimeMethodResolver.hpp"
#include "frontends/common/ScopeTracker.hpp"
#include "frontends/common/StringTable.hpp"
#include "frontends/common/TypeUtils.hpp"

#include "il/core/Module.hpp"

#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace common = il::frontends::common;

namespace {

class TestCursor : public common::lexer_base::LexerCursor<TestCursor> {
  public:
    explicit TestCursor(std::string_view input)
        : common::lexer_base::LexerCursor<TestCursor>(1), input_(input) {}

    [[nodiscard]] std::string_view source() const {
        return input_;
    }

  private:
    std::string_view input_;
};

} // namespace

int main() {
    using limits = std::numeric_limits<int64_t>;

    assert(!common::const_fold::foldIntMul(limits::max(), 2));
    assert(!common::const_fold::foldIntAdd(limits::min(), limits::min()));
    assert(!common::const_fold::foldIntSub(limits::max(), -1));
    assert(!common::const_fold::foldIntMul(limits::min(), -1));
    assert(common::const_fold::foldIntMul(-7, 6) == -42);
    assert(!common::const_fold::foldIntMod(limits::min(), -1));
    assert(common::const_fold::foldShl(-1, 1) == -2);
    assert(common::const_fold::foldShr(-4, 1) == -2);
    assert(!common::const_fold::floatToInt(0x1p63));
    assert(common::const_fold::floatToInt(-0x1p63) == limits::min());

    assert(!common::number_parsing::parseDecimalLiteral("12junk").valid);
    assert(!common::number_parsing::parseDecimalLiteral("1.5junk").valid);
    auto overflow = common::number_parsing::parseDecimalLiteral("1e9999");
    assert(!overflow.valid && overflow.overflow);
    assert(!common::number_parsing::parseHexLiteral("ffz").valid);
    assert(!common::number_parsing::parseBinaryLiteral("102").valid);
    assert(!common::number_parsing::parseOctalLiteral("78").valid);

    assert(!common::escape_sequences::parseUnicodeHexDigits("123"));
    assert(!common::escape_sequences::parseUnicodeHexDigits("12345"));
    assert(common::escape_sequences::parseUnicodeHexDigits("0041") == 0x41U);

    TestCursor cursor("a\r\nb\rc\n");
    assert(cursor.peek(std::numeric_limits<std::size_t>::max()) == '\0');
    assert(cursor.get() == 'a');
    assert(cursor.get() == '\r');
    assert(cursor.line() == 2 && cursor.column() == 1);
    assert(cursor.get() == 'b');
    assert(cursor.get() == '\r');
    assert(cursor.line() == 3 && cursor.column() == 1);
    TestCursor nulCursor(std::string_view("\0x", 2));
    assert(nulCursor.peekOptional() && *nulCursor.peekOptional() == '\0');
    assert(nulCursor.getOptional() && !nulCursor.eof());

    assert(common::diag_fmt::getSourceLine("first\r\nsecond", 1) == "first");

    using Kind = il::core::Type::Kind;
    assert(common::type_utils::getCommonType(Kind::F64, Kind::Ptr) == Kind::Void);
    assert(common::type_utils::getCommonType(Kind::F64, Kind::I64) == Kind::F64);
    assert(!common::type_utils::getTypeSize(Kind::Void));
    assert(common::type_utils::getTypeSize(Kind::Error) == 24U);
    assert(common::type_utils::getTypeSize(Kind::ResumeTok) == 8U);
    assert(common::type_utils::getTypeSize(Kind::I16) == 2U);

    common::NameMangler mangler;
    assert(mangler.block("foo1") == "foo1");
    assert(mangler.block("foo") == "foo");
    assert(mangler.block("foo") == "foo2");

    common::ScopeTracker scopes;
    bool threw = false;
    try {
        (void)scopes.declareLocal("missing");
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw);

    assert(common::isGuiWidgetSubclass("Zanna.GUI.Button"));
    assert(!common::isGuiWidgetSubclass("Zanna.GUI.App"));
    auto inheritedMethod =
        common::RuntimeMethodResolver::instance().find("Zanna.GUI.Button", "Focus", 0);
    assert(inheritedMethod && inheritedMethod->target == "Zanna.GUI.Widget.Focus");
    auto inheritedCandidates =
        common::RuntimeMethodResolver::instance().candidates("Zanna.GUI.Button", "Focus");
    assert(!inheritedCandidates.empty());
    scopes.pushScope();
    assert(scopes.bind("name", "name_0"));
    assert(!scopes.bind("name", "replacement"));
    assert(scopes.resolve("name") == "name_0");

    common::LoopContextStack loops;
    threw = false;
    try {
        loops.pop();
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw);
    loops.push(4, 5);
    assert(loops.breakTarget() == 4 && loops.continueTarget() == 5);
    loops.pop();
    loops.push(common::LoopContext{});
    threw = false;
    try {
        (void)loops.breakTarget();
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw);
    loops.pop();

    std::vector<std::string> emitted;
    common::StringTable strings([&](const std::string &label, const std::string &content) {
        emitted.push_back(label + ":" + content);
    });
    assert(strings.intern("second") == ".L0");
    assert(strings.intern("first") == ".L1");
    std::vector<std::string> iterated;
    strings.forEach([&](const std::string &label, const std::string &content) {
        iterated.push_back(label + ":" + content);
    });
    assert(iterated == emitted);

    common::StringTable reentrant;
    reentrant.setEmitter([&](const std::string &, const std::string &) {
        (void)reentrant.intern("nested");
    });
    threw = false;
    try {
        (void)reentrant.intern("outer");
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw && reentrant.empty() && reentrant.nextId() == 0);

    enum class Token { Identifier, If };
    common::keyword_table::KeywordMap<Token> keywords;
    keywords.add("if", Token::If);
    assert(keywords.lookup(std::string_view("if")) == Token::If);

    common::ExprResult invalidConstant(il::core::Value::constInt(1), il::core::Type(Kind::Void));
    assert(!invalidConstant.isValid());

    il::core::Module module;
    il::build::IRBuilder builder(module);
    auto &function = builder.startFunction("common_test", il::core::Type(Kind::Void), {});
    common::BlockManager blocks(&builder, &function);
    threw = false;
    try {
        (void)blocks.currentBlock();
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw);
    auto entryIndex = blocks.createBlockExact("entry");
    blocks.setBlock(entryIndex);
    threw = false;
    try {
        (void)blocks.createBlockExact("entry");
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert(threw);

    il::core::BasicBlock *current = blocks.currentBlock();
    common::InstructionEmitter emitter(&builder, &current, &function);
    emitter.emitCallIndirect(il::core::Value::null(), {});
    assert(!current->instructions.back().hasIndirectSignature);
    emitter.emitCallIndirect(il::core::Value::null(), {}, std::vector<il::core::Type>{});
    assert(current->instructions.back().hasIndirectSignature);
    assert(!current->instructions.back().indirectIsVarArg);
    emitter.emitRetVoid();
    threw = false;
    try {
        emitter.emitCall("unreachable", {});
    } catch (const std::logic_error &) {
        threw = true;
    }
    assert(threw);
    return 0;
}
