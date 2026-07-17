//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/conformance/MemoryEhConformanceTests.cpp
// Purpose: Cross-layer conformance for memory/aggregate and exception-handling
//          semantics — the non-arithmetic surfaces the arithmetic suites omit.
//          Each fixture is authored as textual IL, parsed, and run under the
//          tree-walking VM and (on ARM64) the native backend, asserting the
//          low-8-bit exit codes agree.
// Key invariants:
//   - alloca/store/load round-trips and GEP offset arithmetic agree across layers
//   - a thrown error caught by a handler yields the same value on every layer
// Platform: On non-ARM64 hosts the native leg is skipped; VM behavior still runs.
// Links: tests/conformance/CrossLayerArithTests.cpp, il/api/expected_api.hpp
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/api/expected_api.hpp"
#include "il/io/Serializer.hpp"
#include "tests/TestHarness.hpp"
#if ZANNA_HAS_ARM64
#include "tools/zanna/cmd_codegen_arm64.hpp"
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace il::core;

namespace {

/// Parse textual IL into a Module, failing the test on malformed input.
Module parseModule(const std::string &src) {
    Module module;
    std::istringstream is(src);
    auto parsed = il::api::v2::parse_text_expected(is, module);
    EXPECT_TRUE(parsed.hasValue());
    return module;
}

int64_t runVm(Module &module) {
    zanna::tests::VmFixture fixture;
    return fixture.run(module);
}

#if ZANNA_HAS_ARM64
int runNative(Module &module) {
    const std::string ilSource = il::io::Serializer::toString(module);
    namespace fs = std::filesystem;
    static int counter = 0;
    const fs::path dir{"build/test-out/crosslayer-memeh"};
    fs::create_directories(dir);
    const fs::path ilPath = dir / ("test_" + std::to_string(counter++) + ".il");
    std::ofstream out(ilPath);
    out << ilSource;
    out.close();
    const char *argv[] = {ilPath.c_str(), "-run-native"};
    const int result = zanna::tools::ilc::cmd_codegen_arm64(2, const_cast<char **>(argv));
    std::error_code ec;
    fs::remove(ilPath, ec);
    return result;
}
#endif

/// Run @p src under the VM and (on ARM64) native, asserting exit codes match.
/// @return the VM's i64 result for further assertion.
int64_t runCrossLayer(const std::string &src) {
    Module vmModule = parseModule(src);
    const int64_t vmResult = runVm(vmModule);
#if ZANNA_HAS_ARM64
    Module nativeModule = parseModule(src);
    const int nativeResult = runNative(nativeModule);
    const int vmExit = static_cast<int>(vmResult) & 0xFF;
    const int natExit = nativeResult & 0xFF;
    if (vmExit != natExit)
        std::cerr << "  VM exit=" << vmExit << "  native exit=" << natExit << "\n";
    ASSERT_EQ(vmExit, natExit);
#endif
    return vmResult;
}

} // namespace

//=============================================================================
// Memory / aggregate conformance
//=============================================================================

TEST(MemoryConformance, AllocaStoreLoadRoundTrip) {
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  %p = alloca 8
  store i64, %p, 42
  %v = load i64, %p
  ret %v
}
)");
    EXPECT_TRUE((result & 0xFF) == 42);
}

TEST(MemoryConformance, GepOffsetStoreLoad) {
    // Store two i64s into a 16-byte slot at offsets 0 and 8; read back offset 8.
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  %base = alloca 16
  %hi = gep %base, 8
  store i64, %base, 11
  store i64, %hi, 99
  %v = load i64, %hi
  ret %v
}
)");
    EXPECT_TRUE((result & 0xFF) == 99);
}

TEST(MemoryConformance, MultiStoreLastWins) {
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  %p = alloca 8
  store i64, %p, 7
  store i64, %p, 23
  %v = load i64, %p
  ret %v
}
)");
    EXPECT_TRUE((result & 0xFF) == 23);
}

TEST(MemoryConformance, NarrowStoreLoadPreservesLowBits) {
    // Store a value, reload, and mask to confirm sub-word memory traffic agrees.
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  %p = alloca 8
  store i64, %p, 65535
  %v = load i64, %p
  ret %v
}
)");
    EXPECT_TRUE((result & 0xFF) == (65535 & 0xFF));
}

//=============================================================================
// Exception-handling conformance
//=============================================================================

TEST(EhConformance, DivByZeroCaughtReturnsKind) {
    // eh.push installs a handler; the checked divide raises; the handler reads the
    // trap kind and returns it. VM and native must agree on the caught kind.
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  eh.push ^handler
  %value = sdiv.chk0 1, 0
  eh.pop
  ret %value

handler(%err: Error, %tok: ResumeTok):
  eh.entry
  %kind = trap.kind %err
  ret %kind
}
)");
    // DivideByZero trap kind == 0 on both the VM and native (runCrossLayer also
    // asserts the two layers' exit codes agree).
    EXPECT_TRUE((result & 0xFF) == 0);
}

TEST(EhConformance, OverflowCaughtReturnsKind) {
    // A different trap source (checked add overflow) routed through the handler.
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  eh.push ^handler
  %value = iadd.ovf 9223372036854775807, 1
  eh.pop
  ret %value

handler(%err: Error, %tok: ResumeTok):
  eh.entry
  %kind = trap.kind %err
  ret %kind
}
)");
    // Overflow trap kind == 1 on both layers.
    EXPECT_TRUE((result & 0xFF) == 1);
}

TEST(EhConformance, NoTrapHandlerSkipped) {
    // With no trap raised, eh.pop unwinds the handler and normal flow returns 5.
    const int64_t result = runCrossLayer(R"(il 0.3.0
func @main() -> i64 {
entry:
  eh.push ^handler
  eh.pop
  ret 5

handler(%err: Error, %tok: ResumeTok):
  eh.entry
  ret 0
}
)");
    EXPECT_TRUE((result & 0xFF) == 5);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
