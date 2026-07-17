//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/RuntimeFastPathOpt.cpp
// Purpose: Rewrite generic object RC helpers to known-object helpers when the
//          value provenance proves a heap object rather than a string handle.
//
//===----------------------------------------------------------------------===//

#include "il/transform/RuntimeFastPathOpt.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <memory>
#include <string_view>
#include <unordered_set>

using namespace il::core;

namespace il::transform {
namespace {

[[nodiscard]] bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size(), suffix.size()) == suffix;
}

[[nodiscard]] bool isKnownObjectFactory(std::string_view callee) {
    if (callee == "rt_obj_new_i64" || callee == "rt_box_i64" || callee == "rt_box_f64" ||
        callee == "rt_box_i1" || callee == "rt_box_str" || callee == "rt_box_value_type") {
        return true;
    }

    if (!startsWith(callee, "rt_") || !endsWith(callee, "_new"))
        return false;
    if (startsWith(callee, "rt_arr_") || startsWith(callee, "rt_str_"))
        return false;
    return true;
}

} // namespace

std::string_view RuntimeFastPathOpt::id() const {
    return "runtime-fastpath";
}

PreservedAnalyses RuntimeFastPathOpt::run(Function &function, AnalysisManager & /*analysis*/) {
    std::unordered_set<unsigned> knownObjects;
    bool changed = false;

    for (auto &block : function.blocks) {
        for (auto &instr : block.instructions) {
            if (instr.op == Opcode::Call && instr.result && isKnownObjectFactory(instr.callee))
                knownObjects.insert(*instr.result);

            if (instr.op != Opcode::Call || instr.operands.empty() ||
                instr.operands.front().kind != Value::Kind::Temp) {
                continue;
            }
            if (!knownObjects.contains(instr.operands.front().id))
                continue;

            if (instr.callee == "rt_obj_retain_maybe") {
                instr.setDirectCallee("rt_obj_retain_known");
                changed = true;
            } else if (instr.callee == "rt_obj_release_check0") {
                instr.setDirectCallee("rt_obj_release_known_check0");
                changed = true;
            }
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

void registerRuntimeFastPathOptPass(PassRegistry &registry) {
    registry.registerFunctionPass(
        "runtime-fastpath", []() { return std::make_unique<RuntimeFastPathOpt>(); }, true);
}

} // namespace il::transform
