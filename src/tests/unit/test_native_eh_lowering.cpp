//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_native_eh_lowering.cpp
// Purpose: Verify the shared native EH rewrite before backend lowering.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/common/NativeEHLowering.hpp"
#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"

#include <sstream>
#include <string>

namespace {

il::core::Module parseModule(const std::string &src) {
    std::istringstream in(src);
    il::core::Module mod;
    auto parse = il::api::v2::parse_text_expected(in, mod);
    EXPECT_TRUE(parse.hasValue());
    auto verify = il::api::v2::verify_module_expected(mod);
    EXPECT_TRUE(verify.hasValue());
    return mod;
}

bool hasExtern(const il::core::Module &mod, const std::string &name) {
    for (const auto &ext : mod.externs) {
        if (ext.name == name)
            return true;
    }
    return false;
}

bool hasOpcode(const il::core::Function &fn, il::core::Opcode opcode) {
    for (const auto &bb : fn.blocks) {
        for (const auto &instr : bb.instructions) {
            if (instr.op == opcode)
                return true;
        }
    }
    return false;
}

const il::core::BasicBlock *findBlock(const il::core::Function &fn, const std::string &label) {
    for (const auto &bb : fn.blocks) {
        if (bb.label == label)
            return &bb;
    }
    return nullptr;
}

} // namespace

TEST(NativeEHLowering, RewritesResumeNextIntoRuntimeHelpers) {
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handler\n"
                           "  %q = sdiv.chk0 10, 0\n"
                           "  eh.pop\n"
                           "  ret 42\n"
                           "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  resume.next %tok\n"
                           "}\n";

    il::core::Module mod = parseModule(il);
    ASSERT_TRUE(viper::codegen::common::lowerNativeEh(mod));
    auto verify = il::api::v2::verify_module_expected(mod);
    ASSERT_TRUE(verify.hasValue());
    ASSERT_EQ(mod.functions.size(), 1U);

    const auto &fn = mod.functions.front();
    EXPECT_FALSE(hasOpcode(fn, il::core::Opcode::EhPush));
    EXPECT_FALSE(hasOpcode(fn, il::core::Opcode::EhPop));
    EXPECT_FALSE(hasOpcode(fn, il::core::Opcode::EhEntry));
    EXPECT_FALSE(hasOpcode(fn, il::core::Opcode::ResumeNext));
    EXPECT_FALSE(hasOpcode(fn, il::core::Opcode::ResumeSame));
    EXPECT_FALSE(hasOpcode(fn, il::core::Opcode::ResumeLabel));

    EXPECT_TRUE(hasExtern(mod, "rt_native_eh_frame_alloc"));
    EXPECT_TRUE(hasExtern(mod, "rt_native_eh_push"));
    EXPECT_TRUE(hasExtern(mod, "rt_native_eh_pop"));
    EXPECT_TRUE(hasExtern(mod, "rt_native_eh_set_site"));
    EXPECT_TRUE(hasExtern(mod, "rt_native_eh_get_site"));
    EXPECT_TRUE(hasExtern(mod, "setjmp"));

    const auto *handler = findBlock(fn, "handler");
    ASSERT_TRUE(handler != nullptr);
    ASSERT_EQ(handler->params.size(), 2U);
    EXPECT_EQ(handler->params[0].type.kind, il::core::Type::Kind::Ptr);
    EXPECT_EQ(handler->params[1].type.kind, il::core::Type::Kind::I64);

    bool sawSiteBlock = false;
    bool sawInvalidResume = false;
    bool sawFrameAlloc = false;
    bool sawSetSite = false;
    bool sawGetSite = false;
    bool sawSetjmp = false;

    for (const auto &bb : fn.blocks) {
        if (bb.label.find(".__neh.site.") != std::string::npos)
            sawSiteBlock = true;
        if (bb.label.find(".__neh.invalid_resume") != std::string::npos)
            sawInvalidResume = true;
        for (const auto &instr : bb.instructions) {
            if (instr.op != il::core::Opcode::Call)
                continue;
            if (instr.callee == "rt_native_eh_frame_alloc")
                sawFrameAlloc = true;
            if (instr.callee == "rt_native_eh_set_site")
                sawSetSite = true;
            if (instr.callee == "rt_native_eh_get_site")
                sawGetSite = true;
            if (instr.callee == "setjmp")
                sawSetjmp = true;
        }
    }

    EXPECT_TRUE(sawSiteBlock);
    EXPECT_TRUE(sawInvalidResume);
    EXPECT_TRUE(sawFrameAlloc);
    EXPECT_TRUE(sawSetSite);
    EXPECT_TRUE(sawGetSite);
    EXPECT_TRUE(sawSetjmp);
}

TEST(NativeEHLowering, RewritesResumeSameIntoFaultSiteDispatch) {
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handler\n"
                           "  trap\n"
                           "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  resume.same %tok\n"
                           "}\n";

    il::core::Module mod = parseModule(il);
    ASSERT_TRUE(viper::codegen::common::lowerNativeEh(mod));
    auto verify = il::api::v2::verify_module_expected(mod);
    ASSERT_TRUE(verify.hasValue());

    const auto &fn = mod.functions.front();
    bool sawResumeBackedge = false;
    for (const auto &bb : fn.blocks) {
        for (const auto &instr : bb.instructions) {
            if (instr.op != il::core::Opcode::CBr || instr.labels.empty())
                continue;
            for (const auto &label : instr.labels) {
                if (label.find(".__neh.site.") != std::string::npos)
                    sawResumeBackedge = true;
            }
        }
    }
    EXPECT_TRUE(sawResumeBackedge);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
