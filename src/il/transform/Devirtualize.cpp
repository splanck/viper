//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/Devirtualize.cpp
// Purpose: Fold call.indirect through constant function addresses to call.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Devirtualize.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include <memory>
#include <string>
#include <unordered_map>

using namespace il::core;

namespace il::transform {
namespace {

[[nodiscard]] std::string globalNameFromValue(const Value &value) {
    if (value.kind == Value::Kind::GlobalAddr)
        return value.str;
    return {};
}

} // namespace

std::string_view Devirtualize::id() const {
    return "devirt";
}

PreservedAnalyses Devirtualize::run(Function &function, AnalysisManager & /*analysis*/) {
    std::unordered_map<unsigned, std::string> globalByTemp;
    bool changed = false;

    for (auto &block : function.blocks) {
        for (auto &instr : block.instructions) {
            if (instr.op == Opcode::GAddr && instr.result && instr.operands.size() == 1) {
                std::string name = globalNameFromValue(instr.operands.front());
                if (!name.empty())
                    globalByTemp[*instr.result] = std::move(name);
                continue;
            }

            if (instr.op != Opcode::CallIndirect || instr.operands.empty())
                continue;

            std::string callee = globalNameFromValue(instr.operands.front());
            if (callee.empty() && instr.operands.front().kind == Value::Kind::Temp) {
                auto it = globalByTemp.find(instr.operands.front().id);
                if (it != globalByTemp.end())
                    callee = it->second;
            }

            if (callee.empty())
                continue;

            instr.op = Opcode::Call;
            instr.setDirectCallee(std::move(callee));
            instr.operands.erase(instr.operands.begin());
            instr.clearIndirectSignature();
            changed = true;
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    preserved.preserveCFG();
    preserved.preserveDominators();
    preserved.preserveLoopInfo();
    return preserved;
}

void registerDevirtualizePass(PassRegistry &registry) {
    registry.registerFunctionPass(
        "devirt", []() { return std::make_unique<Devirtualize>(); }, true);
}

} // namespace il::transform
