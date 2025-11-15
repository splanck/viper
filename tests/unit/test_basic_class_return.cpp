// File: tests/unit/test_basic_class_return.cpp
// Purpose: Repro and guard for BUG-040 — ensure FUNCTIONS returning custom
// //         class types lower RETURN of an object variable as a pointer load.
// Key invariants: The ret operand must originate from a Load typed as Ptr.
// Ownership/Lifetime: Standalone unit test executable.
// Links: docs/codemap.md, docs/il-guide.md#reference

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"

#include <optional>
#include <string>

using namespace il::frontends::basic;

namespace
{
constexpr const char *kSrc = R"BASIC(
10 CLASS Person
20 END CLASS

30 FUNCTION CreatePerson() AS Person
40   DIM p AS Person
50   p = NEW Person()
60   RETURN p
70 END FUNCTION
80 END
)BASIC";

static const il::core::Function *findFunctionCaseInsensitive(const il::core::Module &m,
                                                             std::string_view name)
{
    auto ieq = [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
    auto eq = [&](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (!ieq(a[i], b[i])) return false; return true;
    };
    for (const auto &fn : m.functions)
        if (eq(fn.name, name))
            return &fn;
    return nullptr;
}
} // namespace

TEST(BasicClassReturn, ReturnUsesPtrLoad)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{kSrc, "class_return.bas"};
    BasicCompilerOptions opts{};

    auto result = compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;
    const il::core::Function *fn = findFunctionCaseInsensitive(mod, "CreatePerson");
    ASSERT_NE(fn, nullptr);
    // Sanity: function must declare a ptr return type
    EXPECT_EQ(fn->retType.kind, il::core::Type::Kind::Ptr);

    // Find a Ret, then locate the defining instruction for its operand, and ensure it is a Load Ptr.
    bool foundPtrLoadRet = false;
    for (const auto &bb : fn->blocks)
    {
        // Build a quick map from result temp id -> instruction index within the block
        std::unordered_map<unsigned, const il::core::Instr *> defByTemp;
        for (const auto &ins : bb.instructions)
        {
            if (ins.result)
                defByTemp[*ins.result] = &ins;
        }
        for (const auto &ins : bb.instructions)
        {
            if (ins.op != il::core::Opcode::Ret || ins.operands.size() != 1)
                continue;
            const auto &op = ins.operands[0];
            if (op.kind != il::core::Value::Kind::Temp)
                continue;
            auto it = defByTemp.find(op.id);
            if (it == defByTemp.end())
                continue;
            const il::core::Instr *def = it->second;
            if (def->op == il::core::Opcode::Load && def->type.kind == il::core::Type::Kind::Ptr)
            {
                foundPtrLoadRet = true;
                break;
            }
        }
        if (foundPtrLoadRet)
            break;
    }

    EXPECT_TRUE(foundPtrLoadRet);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
