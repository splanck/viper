// File: tests/unit/test_il_parse_f32.cpp
// Purpose: Ensure IL parser/serializer round-trip f32 instruction annotations.
// Key invariants: F32 instructions retain explicit type annotations and parse to Kind::F32.
// Ownership: Standalone unit test executable.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/io/Serializer.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    constexpr const char *kSource = R"(il 0.1
func @main() -> i64 {
entry:
  %f:f32 = sitofp 7
  %g:f32 = fadd %f, 2.5
  %h:f32 = fmul %g, 0.5
  %diff:f32 = fsub %h, %f
  %back = cast.fp_to_si.rte.chk %diff
  ret %back
}
)";

    il::core::Module parsed;
    std::istringstream input(kSource);
    auto parseResult = il::api::v2::parse_text_expected(input, parsed);
    assert(parseResult);
    assert(parsed.functions.size() == 1);
    const auto &fn = parsed.functions.front();
    assert(fn.blocks.size() == 1);
    const auto &instrs = fn.blocks.front().instructions;
    assert(instrs.size() >= 5);
    assert(instrs[0].type.kind == il::core::Type::Kind::F32);
    assert(instrs[1].type.kind == il::core::Type::Kind::F32);
    assert(instrs[2].type.kind == il::core::Type::Kind::F32);
    assert(instrs[3].type.kind == il::core::Type::Kind::F32);

    const std::string serialized = il::io::Serializer::toString(parsed);
    assert(serialized.find(":f32 = sitofp") != std::string::npos);
    assert(serialized.find(":f32 = fadd") != std::string::npos);

    il::core::Module roundTripped;
    std::istringstream second(serialized);
    auto secondParse = il::api::v2::parse_text_expected(second, roundTripped);
    assert(secondParse);
    const auto &rtInstrs = roundTripped.functions.front().blocks.front().instructions;
    assert(rtInstrs.size() >= 5);
    assert(rtInstrs[0].type.kind == il::core::Type::Kind::F32);
    assert(rtInstrs[1].type.kind == il::core::Type::Kind::F32);
    assert(rtInstrs[2].type.kind == il::core::Type::Kind::F32);
    assert(rtInstrs[3].type.kind == il::core::Type::Kind::F32);

    return 0;
}
