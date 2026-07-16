//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_backend_benchmark_regressions.cpp
// Purpose: Correctness regression tests for optimizer/backend issues that were first
//          exposed by the Viper benchmark suite. NOTE: despite the file name, these
//          assert IL/result correctness only — they do NOT measure or gate performance.
// Key invariants: Owned string materialization is not CSE/LICM-safe, O2 keeps
//                 verifier-clean checked unsigned div/rem, and O2 has a
//                 bounded multi-block inliner.
// Ownership/Lifetime: Ephemeral modules only.
// Links: docs/il/il-passes.md
//
//===----------------------------------------------------------------------===//

#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/io/Parser.hpp"
#include "il/transform/CallEffects.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/ValueKey.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string_view>

using il::core::Instr;
using il::core::Module;
using il::core::Opcode;
using il::core::Type;
using il::core::Value;

namespace {

Module parseModule(std::string_view text) {
    Module module;
    std::istringstream input{std::string{text}};
    auto parsed = il::io::Parser::parse(input, module);
    assert(parsed && "test IL must parse");
    auto verified = il::verify::Verifier::verify(module);
    assert(verified && "test IL must verify before optimization");
    return module;
}

void runO2(Module &module) {
    il::transform::PassManager pm;
    const bool ok = pm.runPipeline(module, "O2");
    assert(ok && "O2 pipeline must run");
    auto verified = il::verify::Verifier::verify(module);
    if (!verified)
        il::support::printDiag(verified.error(), std::cerr);
    assert(verified && "O2 output must verify");
}

const il::core::Function *findFunction(const Module &module, std::string_view name) {
    for (const auto &fn : module.functions)
        if (fn.name == name)
            return &fn;
    return nullptr;
}

std::size_t countOpcode(const Module &module, Opcode opcode) {
    std::size_t count = 0;
    for (const auto &fn : module.functions)
        for (const auto &block : fn.blocks)
            for (const auto &instr : block.instructions)
                if (instr.op == opcode)
                    ++count;
    return count;
}

bool hasCallTo(const Module &module, std::string_view callee) {
    for (const auto &fn : module.functions)
        for (const auto &block : fn.blocks)
            for (const auto &instr : block.instructions)
                if (instr.op == Opcode::Call && instr.callee == callee)
                    return true;
    return false;
}

void testConstStrOwnershipSemantics() {
    const auto &info = il::core::getOpcodeInfo(Opcode::ConstStr);
    assert(info.hasSideEffects);
    assert(il::core::hasMemoryRead(Opcode::ConstStr));
    assert(!il::core::hasMemoryWrite(Opcode::ConstStr));

    Instr instr;
    instr.op = Opcode::ConstStr;
    instr.result = 1;
    instr.type = Type(Type::Kind::Str);
    instr.operands = {Value::global("@.Lmsg")};
    assert(!il::transform::makeValueKey(instr).has_value());
}

void testStringOwnershipMetadata() {
    Instr concat;
    concat.op = Opcode::Call;
    concat.callee = "rt_str_concat";
    concat.operands = {Value::temp(1), Value::temp(2)};

    auto effects = il::transform::classifyCallEffects(concat);
    assert(effects.consumesArg(0));
    assert(effects.consumesArg(1));
    assert(effects.returnsOwned);
    assert(effects.mayAllocate);

    effects = il::transform::classifyCalleeEffects("Viper.String.Concat");
    assert(effects.consumesArg(0));
    assert(effects.consumesArg(1));
    assert(effects.returnsOwned);
    assert(effects.mayAllocate);

    effects = il::transform::classifyCalleeEffects("rt_str_len");
    assert(effects.readonly);
    assert(!effects.nothrow);
    assert(!effects.hasOwnershipEffects());
}

void testO2DoesNotHoistOwnedConstStr() {
    Module module = parseModule(R"(il 0.3.0
extern @rt_str_concat(str, str) -> str
extern @rt_str_len(str) -> i64
extern @rt_str_release_maybe(str) -> void
global const str @.La = "abcdef"
global const str @.Lb = "ghijkl"
func @main() -> i64 {
entry:
  br loop(0, 0)
loop(%i:i64, %sum:i64):
  %done = scmp_ge %i, 4
  cbr %done, exit(%sum), body(%i, %sum)
body(%bi:i64, %bsum:i64):
  %a = const_str @.La
  %b = const_str @.Lb
  %c = call @rt_str_concat(%a, %b)
  %n = call @rt_str_len(%c)
  call @rt_str_release_maybe(%c)
  %next_sum = iadd.ovf %bsum, %n
  %next_i = iadd.ovf %bi, 1
  br loop(%next_i, %next_sum)
exit(%out:i64):
  ret %out
}
)");
    runO2(module);

    const auto *mainFn = findFunction(module, "main");
    assert(mainFn != nullptr);
    const auto *entry = &mainFn->blocks.front();
    for (const auto &instr : entry->instructions)
        assert(instr.op != Opcode::ConstStr);
    assert(countOpcode(module, Opcode::ConstStr) == 1);
    assert(!hasCallTo(module, "rt_str_concat"));
}

void testO2StrengthReducesCheckedUnsignedPowerOfTwoDivRem() {
    Module module = parseModule(R"(il 0.3.0
func @main(%x:i64) -> i64 {
entry(%x:i64):
  %q = udiv.chk0 %x, 8
  %r = urem.chk0 %x, 4
  %s = iadd.ovf %q, %r
  ret %s
}
)");
    runO2(module);

    assert(countOpcode(module, Opcode::UDiv) == 0);
    assert(countOpcode(module, Opcode::UDivChk0) == 0);
    assert(countOpcode(module, Opcode::URem) == 0);
    assert(countOpcode(module, Opcode::URemChk0) == 0);
    assert(countOpcode(module, Opcode::LShr) >= 1);
    assert(countOpcode(module, Opcode::And) >= 1);
}

void testO2InlineUsesSmallCfgBudget() {
    Module module = parseModule(R"(il 0.3.0
func @choose(%x:i64) -> i64 {
entry(%x:i64):
  %cond = scmp_gt %x, 0
  cbr %cond, pos(%x), neg(%x)
pos(%p:i64):
  %a = iadd.ovf %p, 1
  br join(%a)
neg(%n:i64):
  %b = isub.ovf 0, %n
  br join(%b)
join(%r:i64):
  ret %r
}
func @main() -> i64 {
entry:
  %r = call @choose(3)
  ret %r
}
)");
    runO2(module);
    assert(!hasCallTo(module, "choose"));
}

void testO2InlineMapsEntryParamsByPosition() {
    Module module = parseModule(R"(il 0.3.0
func @add1(i64 %x) -> i64 {
entry(%x0:i64):
  %r = iadd.ovf %x0, 1
  ret %r
}
func @main() -> i64 {
entry:
  %r = call @add1(41)
  ret %r
}
)");
    runO2(module);
    assert(!hasCallTo(module, "add1"));
}

void testO2ExpandsSignedPowerOfTwoRemainder() {
    Module module = parseModule(R"(il 0.3.0
func @main(%x:i64) -> i64 {
entry(%x:i64):
  %r = srem.chk0 %x, 2
  ret %r
}
)");
    runO2(module);
    assert(countOpcode(module, Opcode::SRem) == 0);
    assert(countOpcode(module, Opcode::SRemChk0) == 0);
    assert(countOpcode(module, Opcode::AShr) >= 1);
    assert(countOpcode(module, Opcode::And) >= 1);
}

} // namespace

int main() {
    testConstStrOwnershipSemantics();
    testStringOwnershipMetadata();
    testO2DoesNotHoistOwnedConstStr();
    testO2StrengthReducesCheckedUnsignedPowerOfTwoDivRem();
    testO2InlineUsesSmallCfgBudget();
    testO2InlineMapsEntryParamsByPosition();
    testO2ExpandsSignedPowerOfTwoRemainder();
    return 0;
}
