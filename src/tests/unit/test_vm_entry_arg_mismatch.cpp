//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_entry_arg_mismatch.cpp
// Purpose: Ensure VM traps when entry frame argument counts do not match block parameters.
// Key invariants: Calling a function with mismatched argument count emits InvalidOperation trap.
// Ownership/Lifetime: Builds synthetic module and executes VM in forked child to capture
// diagnostics. Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "VMTestHook.hpp"
#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace il::core;

namespace {
bool trapHeaderMatches(const std::string &diag, std::string_view function, std::string_view kind) {
    std::string_view view(diag);
    if (const auto newline = view.find('\n'); newline != std::string_view::npos)
        view = view.substr(0, newline);

    // Format: "Trap @function:block#ip line N: Kind (code=C)"
    constexpr std::string_view kPrefix = "Trap @";
    if (!view.starts_with(kPrefix))
        return false;
    view.remove_prefix(kPrefix.size());

    // Match function name (now followed by :block)
    if (view.substr(0, function.size()) != function)
        return false;
    view.remove_prefix(function.size());

    // Expect colon followed by block name, then #ip
    if (view.empty() || view.front() != ':')
        return false;
    view.remove_prefix(1); // skip ':'

    // Skip block name until we hit '#'
    const auto hashPos = view.find('#');
    if (hashPos == std::string_view::npos)
        return false;
    view.remove_prefix(hashPos); // now at "#ip line..."

    if (view.empty() || view.front() != '#')
        return false;

    // Find the colon before Kind
    const auto colonPos = view.find(':');
    if (colonPos == std::string_view::npos || colonPos + 2 > view.size())
        return false;

    if (view[colonPos + 1] != ' ')
        return false;

    view.remove_prefix(colonPos + 2);
    if (view.substr(0, kind.size()) != kind)
        return false;
    if (view.size() <= kind.size() || view[kind.size()] != ' ')
        return false;
    if (view.find("(code=") == std::string_view::npos)
        return false;

    return true;
}
Module buildEntryArgModule() {
    Module module;
    il::build::IRBuilder builder(module);

    builder.startFunction("too_many_args", Type(Type::Kind::Void), {});
    auto &tooManyEntry = builder.createBlock(module.functions.back(), "entry");
    builder.setInsertPoint(tooManyEntry);
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});

    builder.startFunction("too_few_args", Type(Type::Kind::Void), {});
    auto &tooFewEntry = builder.createBlock(
        module.functions.back(), "entry", std::vector<Param>{{"p0", Type(Type::Kind::I64), 0}});
    builder.setInsertPoint(tooFewEntry);
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});
    return module;
}

void runTooManyArgs() {
    auto module = buildEntryArgModule();
    il::vm::Slot slot{};
    slot.i64 = 42;
    il::vm::VM vm(module);
    il::vm::VMTestHook::run(vm, module.functions.front(), {slot});
}

void runTooFewArgs() {
    auto module = buildEntryArgModule();
    il::vm::VM vm(module);
    il::vm::VMTestHook::run(vm, module.functions.back(), {});
}
} // namespace

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(runTooManyArgs);
    viper::tests::registerChildFunction(runTooFewArgs);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    // Too many args
    {
        auto result = viper::tests::runIsolated(runTooManyArgs);
        assert(result.trapped());
        assert(trapHeaderMatches(result.stderrText, "too_many_args", "InvalidOperation"));
        assert(result.stderrText.find("argument count mismatch") != std::string::npos);
    }

    // Too few args
    {
        auto result = viper::tests::runIsolated(runTooFewArgs);
        assert(result.trapped());
        assert(trapHeaderMatches(result.stderrText, "too_few_args", "InvalidOperation"));
        assert(result.stderrText.find("argument count mismatch") != std::string::npos);
    }

    return 0;
}
