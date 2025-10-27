// File: tests/unit/test_il_parse_operand_whitespace.cpp
// Purpose: Verify OperandParser trims leading whitespace for registers and globals.
// Key invariants: parseValueToken should succeed for whitespace-prefixed identifiers and
//                 reject tokens lacking a name after the sigil. Ownership/Lifetime:
//                 Test owns module/state and token buffers. Links: docs/il-guide.md#reference

#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/io/OperandParser.hpp"
#include "il/io/ParserState.hpp"

#include <cassert>
#include <fstream>
#include <string>
#include <vector>

#ifndef OPERAND_WS_DIR
#error "OPERAND_WS_DIR must be defined"
#endif

namespace
{
std::vector<std::string> readTokens(const char *path)
{
    std::ifstream stream(path);
    assert(stream && "failed to open operand fixture");
    std::vector<std::string> tokens;
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty())
            tokens.push_back(line);
    }
    return tokens;
}
} // namespace

int main()
{
    using il::core::Instr;
    using il::core::Module;
    using il::core::Opcode;
    using il::core::Type;
    using il::core::Value;
    using il::io::detail::OperandParser;
    using il::io::detail::ParserState;

    Module module;
    ParserState state(module);
    state.lineNo = 1;
    state.tempIds.emplace("tmp", 0);

    Instr instr;
    instr.op = Opcode::Add;
    instr.type = Type(Type::Kind::I32);

    OperandParser parser(state, instr);

    const auto positiveTokens = readTokens(OPERAND_WS_DIR "/operand_leading_space_positive.il");
    assert(positiveTokens.size() == 2 && "expected two positive operand samples");

    state.lineNo = 1;
    auto regValue = parser.parseValueToken(positiveTokens[0]);
    assert(regValue && "leading whitespace should be ignored for temporaries");
    assert(regValue.value().kind == Value::Kind::Temp);
    assert(regValue.value().id == 0);

    state.lineNo = 2;
    auto globalValue = parser.parseValueToken(positiveTokens[1]);
    assert(globalValue && "leading whitespace should be ignored for globals");
    assert(globalValue.value().kind == Value::Kind::GlobalAddr);
    assert(globalValue.value().str == "global_symbol");

    const auto negativeTokens = readTokens(OPERAND_WS_DIR "/operand_leading_space_negative.il");
    assert(!negativeTokens.empty() && "expected negative operand samples");

    for (const auto &token : negativeTokens)
    {
        ++state.lineNo;
        auto parsed = parser.parseValueToken(token);
        assert(!parsed && "missing symbol name must still be rejected");
    }

    return 0;
}
