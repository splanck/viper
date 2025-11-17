// File: tests/unit/test_basic_oop_object_array_field.cpp
// Purpose: Verify that object array fields in classes store via rt_arr_obj_put
//          when assigned implicitly inside methods.
// Key invariants: Lowering selects object array helpers for Ptr RHS.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"

#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace {
[[nodiscard]] static bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    unsigned char ac = static_cast<unsigned char>(a[i]);
    unsigned char bc = static_cast<unsigned char>(b[i]);
    if (std::tolower(ac) != std::tolower(bc)) return false;
  }
  return true;
}

[[nodiscard]] static const il::core::Function *findFn(const il::core::Module &m,
                                                      std::string_view name) {
  for (const auto &fn : m.functions)
    if (ieq(fn.name, name)) return &fn;
  return nullptr;
}
} // namespace

TEST(BasicOOPObjectArrayField, ImplicitStoreUsesObjectArrayHelper) {
  const std::string src =
      "10 CLASS Player\n"
      "20 END CLASS\n"
      "30 CLASS Team\n"
      "40   DIM lineup(9) AS Player\n"
      "50   SUB Add()\n"
      "60     lineup(1) = NEW Player()\n"
      "70   END SUB\n"
      "80 END CLASS\n"
      "90 END\n";

  SourceManager sm;
  BasicCompilerInput input{src, "oop_obj_arr_field.bas"};
  BasicCompilerOptions opts{};
  auto result = compileBasic(input, opts, sm);
  ASSERT_TRUE(result.succeeded());

  const il::core::Module &mod = result.module;
  const auto *addFn = findFn(mod, "Team.Add");
  ASSERT_NE(addFn, nullptr);

  bool sawObjPut = false;
  for (const auto &bb : addFn->blocks) {
    for (const auto &in : bb.instructions) {
      if (in.op == il::core::Opcode::Call && ieq(in.callee, "rt_arr_obj_put")) {
        sawObjPut = true;
        break;
      }
    }
    if (sawObjPut) break;
  }
  EXPECT_TRUE(sawObjPut);
}

#if __has_include(<gtest/gtest.h>)
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#else
int main() { return RUN_ALL_TESTS(); }
#endif

