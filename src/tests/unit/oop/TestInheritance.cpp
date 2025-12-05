//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/oop/TestInheritance.cpp
// Purpose: Test class inheritance features including fields and methods.
// Key invariants: Inherited fields/methods should be accessible on derived class instances.
// Ownership/Lifetime: N/A - unit test.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

[[nodiscard]] bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        const unsigned char lc = static_cast<unsigned char>(lhs[i]);
        const unsigned char rc = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(lc) != std::tolower(rc))
            return false;
    }
    return true;
}

[[nodiscard]] bool hasFunction(const il::core::Module &module, std::string_view name)
{
    const auto &functions = module.functions;
    return std::any_of(functions.begin(),
                       functions.end(),
                       [&](const il::core::Function &fn)
                       { return equalsIgnoreCase(fn.name, name); });
}

[[nodiscard]] const il::core::Function *findFunctionCaseInsensitive(const il::core::Module &module,
                                                                    std::string_view name)
{
    for (const auto &fn : module.functions)
    {
        if (equalsIgnoreCase(fn.name, name))
            return &fn;
    }
    return nullptr;
}

/// @brief Check if a function contains a call to the given callee name.
[[nodiscard]] bool hasCallTo(const il::core::Function &fn, std::string_view callee)
{
    for (const auto &block : fn.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::Call)
            {
                if (equalsIgnoreCase(instr.callee, callee))
                    return true;
            }
        }
    }
    return false;
}

} // namespace

// BUG-OOP-001: Test inherited field access
TEST(OOP_Inheritance, InheritedFieldAccess)
{
    const char *src = R"BAS(
CLASS Parent
    PUBLIC value AS INTEGER
END CLASS

CLASS Child : Parent
END CLASS

DIM c AS Child
c = NEW Child()
c.value = 100
PRINT c.value
)BAS";

    SourceManager sm;
    BasicCompilerInput in{src, "inherit_field.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());

    // Verify Parent constructor exists (Child uses it via inheritance)
    EXPECT_TRUE(hasFunction(res.module, "Parent.__ctor"));
    // Child constructor should exist
    EXPECT_TRUE(hasFunction(res.module, "Child.__ctor"));
}

// BUG-OOP-002: Test inherited method access (SUB)
TEST(OOP_Inheritance, InheritedMethodSub)
{
    const char *src = R"BAS(
CLASS Parent
    PUBLIC SUB Greet()
        PRINT "Hello from Parent"
    END SUB
END CLASS

CLASS Child : Parent
END CLASS

DIM c AS Child
c = NEW Child()
c.Greet()
)BAS";

    SourceManager sm;
    BasicCompilerInput in{src, "inherit_method.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());

    // Verify Parent.Greet exists
    EXPECT_TRUE(hasFunction(res.module, "Parent.Greet"));
    // Main should call Parent.Greet (not Child.Greet which doesn't exist)
    const auto *mainFn = findFunctionCaseInsensitive(res.module, "main");
    ASSERT_NE(mainFn, nullptr);
    EXPECT_TRUE(hasCallTo(*mainFn, "Parent.Greet"));
}

// BUG-OOP-002: Test inherited method access (FUNCTION with return value)
TEST(OOP_Inheritance, InheritedMethodFunction)
{
    const char *src = R"BAS(
CLASS Parent
    PUBLIC FUNCTION GetMessage() AS STRING
        RETURN "Message from Parent"
    END FUNCTION
END CLASS

CLASS Child : Parent
END CLASS

DIM c AS Child
c = NEW Child()
PRINT c.GetMessage()
)BAS";

    SourceManager sm;
    BasicCompilerInput in{src, "inherit_func.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());

    // Verify Parent.GetMessage exists
    EXPECT_TRUE(hasFunction(res.module, "Parent.GetMessage"));
    // Main should call Parent.GetMessage (not Child.GetMessage)
    const auto *mainFn = findFunctionCaseInsensitive(res.module, "main");
    ASSERT_NE(mainFn, nullptr);
    EXPECT_TRUE(hasCallTo(*mainFn, "Parent.GetMessage"));
}

// BUG-OOP-007: Test constructor argument coercion (i64 to f64)
TEST(OOP_Inheritance, ConstructorArgCoercionI64ToF64)
{
    const char *src = R"BAS(
CLASS Account
    PUBLIC balance AS DOUBLE

    SUB NEW(initial AS DOUBLE)
        balance = initial
    END SUB
END CLASS

DIM acc AS Account
acc = NEW Account(1000)
PRINT acc.balance
)BAS";

    SourceManager sm;
    BasicCompilerInput in{src, "ctor_coerce.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());

    // If we get here without verification failure, the coercion worked
    EXPECT_TRUE(hasFunction(res.module, "Account.__ctor"));
}

// Test multi-level inheritance
TEST(OOP_Inheritance, MultiLevelInheritance)
{
    const char *src = R"BAS(
CLASS GrandParent
    PUBLIC name AS STRING
END CLASS

CLASS Parent : GrandParent
    PUBLIC age AS INTEGER
END CLASS

CLASS Child : Parent
END CLASS

DIM c AS Child
c = NEW Child()
c.name = "Test"
c.age = 25
PRINT c.name
PRINT c.age
)BAS";

    SourceManager sm;
    BasicCompilerInput in{src, "multilevel.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());

    // All three class constructors should exist
    EXPECT_TRUE(hasFunction(res.module, "GrandParent.__ctor"));
    EXPECT_TRUE(hasFunction(res.module, "Parent.__ctor"));
    EXPECT_TRUE(hasFunction(res.module, "Child.__ctor"));
}

#ifndef GTEST_HAS_MAIN
int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
