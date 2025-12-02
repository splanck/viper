//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/MultiVMIsolationTests.cpp
// Purpose: Verify per-VM isolation for RNG, module variables, and file channels.
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include "rt_context.h"
#include "rt_file.h"
#include "rt_modvar.h"
#include "rt_random.h"
#include "viper/runtime/rt.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace std;

static string makeTempPath(const char *tag)
{
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path();
    auto path = dir / (std::string("viper_multivm_") + tag + ".txt");
    return path.string();
}

TEST(MultiVMIsolation, RNG_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);

    // Seed and advance A
    rt_set_current_context(&a);
    rt_randomize_i64(42);
    double a0 = rt_rnd();
    double a1 = rt_rnd();
    double a2 = rt_rnd();

    // Seed and advance B – must not be affected by A
    rt_set_current_context(&b);
    rt_randomize_i64(42);
    double b0 = rt_rnd();
    double b1 = rt_rnd();
    double b2 = rt_rnd();

    auto approx_eq = [](double x, double y) { return std::fabs(x - y) < 1e-12; };
    ASSERT_TRUE(approx_eq(a0, b0));
    ASSERT_TRUE(approx_eq(a1, b1));
    ASSERT_TRUE(approx_eq(a2, b2));

    // Advance A further; B should continue its own sequence unaffected
    rt_set_current_context(&a);
    double a3 = rt_rnd();
    (void)a3; // unused but ensures state advances
    rt_set_current_context(&b);
    double b3 = rt_rnd();
    // Recompute expected b3 by reseeding a temp context
    RtContext tmp{};
    rt_context_init(&tmp);
    rt_set_current_context(&tmp);
    rt_randomize_i64(42);
    (void)rt_rnd();
    (void)rt_rnd();
    (void)rt_rnd();
    double expected_b3 = rt_rnd();
    ASSERT_TRUE(approx_eq(b3, expected_b3));
}

TEST(MultiVMIsolation, Modvar_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);

    // Increment X twice in A
    rt_set_current_context(&a);
    rt_string name = rt_const_cstr("X");
    auto *xa = (int64_t *)rt_modvar_addr_i64(name);
    *xa += 1;
    ASSERT_EQ(*xa, 1);
    *xa += 1;
    ASSERT_EQ(*xa, 2);

    // Increment X once in B
    rt_set_current_context(&b);
    rt_string name2 = rt_const_cstr("X");
    auto *xb = (int64_t *)rt_modvar_addr_i64(name2);
    *xb += 1;
    ASSERT_EQ(*xb, 1);

    // Switch back to A and ensure it remained at 2
    rt_set_current_context(&a);
    ASSERT_EQ(*xa, 2);
}

TEST(MultiVMIsolation, FileChannels_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);
    const int ch = 5; // same channel ID used in both contexts deliberately

    // Context A writes to fileA on channel 5
    std::string fileA = makeTempPath("A");
    {
        rt_set_current_context(&a);
        rt_string p = rt_const_cstr(fileA.c_str());
        ASSERT_EQ(0, rt_open_err_vstr(p, RT_F_OUTPUT, ch));
        ASSERT_EQ(0, rt_write_ch_err(ch, rt_const_cstr("HelloA")));
        ASSERT_EQ(0, rt_close_err(ch));
    }

    // Context B writes to fileB on same channel 5
    std::string fileB = makeTempPath("B");
    {
        rt_set_current_context(&b);
        rt_string p = rt_const_cstr(fileB.c_str());
        ASSERT_EQ(0, rt_open_err_vstr(p, RT_F_OUTPUT, ch));
        ASSERT_EQ(0, rt_write_ch_err(ch, rt_const_cstr("HelloB")));
        ASSERT_EQ(0, rt_close_err(ch));
    }

    // Verify contents are as expected and not swapped/interfered
    {
        std::ifstream inA(fileA, std::ios::binary);
        std::string sA;
        std::getline(inA, sA, '\0');
        std::ifstream inB(fileB, std::ios::binary);
        std::string sB;
        std::getline(inB, sB, '\0');
        ASSERT_EQ("HelloA", sA);
        ASSERT_EQ("HelloB", sB);
    }

    std::filesystem::remove(fileA);
    std::filesystem::remove(fileB);
}

// TODO: Add type registry isolation once user-class registration is exposed publicly.

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
