//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_ovf.cpp
// Purpose: Verify iadd.ovf/isub.ovf/imul.ovf rr lowering on two entry params.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/passes/EmitPass.hpp"
#include "codegen/aarch64/passes/LegalizePass.hpp"
#include "codegen/aarch64/passes/LoweringPass.hpp"
#include "codegen/aarch64/passes/PassManager.hpp"
#include "codegen/aarch64/passes/RegAllocPass.hpp"
#include "il/core/Module.hpp"
#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::codegen::aarch64;
using namespace viper::tools::ilc;

static std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

/// @brief Returns the expected mangled symbol name for a call target.
static std::string blSym(const std::string &name) {
#if defined(__APPLE__)
    return "bl _" + name;
#else
    return "bl " + name;
#endif
}

static bool hasExactCall(const std::string &asmText, const std::string &name) {
    const std::string expected = blSym(name);
    std::istringstream lines(asmText);
    for (std::string line; std::getline(lines, line);) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        const std::size_t last = line.find_last_not_of(" \t\r");
        if (line.substr(first, last - first + 1) == expected)
            return true;
    }
    return false;
}

TEST(Arm64CLI, OverflowVariantsRR) {
    struct Case {
        const char *op;
        const char *expectOpcode; // primary instruction opcode
        const char *expectTrap;   // trap branch (nullptr if no trap expected)
    } cases[] = {
        {"iadd.ovf", "adds x", "b.vs"},
        {"isub.ovf", "subs x", "b.vs"},
        {"imul.ovf", "mul x", "b.ne"},
    };

    for (const auto &c : cases) {
        std::string in = std::string("arm64_ovf_") + c.op + ".il";
        std::string out = std::string("arm64_ovf_") + c.op + ".s";
        std::string il = std::string("il 0.1\n"
                                     "func @f(%a:i64, %b:i64) -> i64 {\n"
                                     "entry(%a:i64, %b:i64):\n"
                                     "  %t0 = ") +
                         c.op + " %a, %b\n  ret %t0\n}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        EXPECT_NE(asmText.find(c.expectOpcode), std::string::npos);
        if (std::string(c.op) == "imul.ovf") {
            EXPECT_NE(asmText.find("smulh"), std::string::npos);
        }
        if (c.expectTrap) {
            EXPECT_NE(asmText.find(c.expectTrap), std::string::npos);
            EXPECT_TRUE(hasExactCall(asmText, "rt_trap_ovf"));
        }
    }
}

TEST(Arm64CLI, SubWidthOverflowUsesAnnotatedWidth) {
    il::core::Function fn;
    fn.name = "subwidth_ovf";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.params.push_back({"a", il::core::Type(il::core::Type::Kind::I64), 0});
    entry.params.push_back({"b", il::core::Type(il::core::Type::Kind::I64), 1});

    il::core::Instr add;
    add.result = 2;
    add.op = il::core::Opcode::IAddOvf;
    add.type = il::core::Type(il::core::Type::Kind::I16);
    add.operands.push_back(il::core::Value::temp(0));
    add.operands.push_back(il::core::Value::temp(1));
    entry.instructions.push_back(add);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::temp(2));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    fn.blocks.push_back(entry);
    il::core::Module mod;
    mod.functions.push_back(fn);

    passes::AArch64Module module;
    module.ilMod = &mod;
    module.ti = &darwinTarget();

    passes::PassManager pm;
    pm.addPass(std::make_unique<passes::LoweringPass>());
    pm.addPass(std::make_unique<passes::LegalizePass>());
    pm.addPass(std::make_unique<passes::RegAllocPass>());
    pm.addPass(std::make_unique<passes::EmitPass>());

    passes::Diagnostics diags;
    ASSERT_TRUE(pm.run(module, diags));
    const std::string &asmText = module.assembly;

    EXPECT_NE(asmText.find("add x"), std::string::npos);
    EXPECT_NE(asmText.find("#48"), std::string::npos);
    EXPECT_NE(asmText.find("cmp x"), std::string::npos);
    EXPECT_NE(asmText.find("b.ne L.Ltrap_subwidth_ovf_"), std::string::npos);
    EXPECT_TRUE(hasExactCall(asmText, "rt_trap_ovf"));
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
