//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia 'new' keyword with runtime classes whose constructors
// are not named '.New' (e.g., FrozenSet.FromSeq, Version.Parse, BinFile.Open).
// Fixes bugs A-028, A-029, A-031, A-032, A-033, A-042, A-050.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// Helper: compile a Zia source string and return whether it succeeded.
bool compileOk(const std::string &source)
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "<test>"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    if (!result.succeeded())
    {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    return result.succeeded();
}

// A-028: FrozenSet 'new' was rejected because ctor is FromSeq, not New
TEST(ZiaRtNew, FrozenSetNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestFS;
bind Viper.Collections;
func start() {
    var s = new Seq();
    var x = new FrozenSet(s);
}
)"));
}

// A-029: FrozenMap 'new' was rejected because ctor is FromSeqs, not New
TEST(ZiaRtNew, FrozenMapNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestFM;
bind Viper.Collections;
func start() {
    var keys = new Seq();
    var vals = new Seq();
    var x = new FrozenMap(keys, vals);
}
)"));
}

// A-031: Version 'new' was rejected because ctor is Parse, not New
TEST(ZiaRtNew, VersionNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestVer;
bind Viper.Text;
func start() {
    var v = new Version("1.0.0");
}
)"));
}

// A-032: CompiledPattern 'new' — ctor RT_FUNC was missing entirely
TEST(ZiaRtNew, CompiledPatternNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestCP;
bind Viper.Text;
func start() {
    var p = new CompiledPattern("hello.*");
}
)"));
}

// A-033: Scanner 'new' — ctor RT_FUNC was missing entirely
TEST(ZiaRtNew, ScannerNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestScanner;
bind Viper.Text;
func start() {
    var s = new Scanner("hello world");
}
)"));
}

// A-042: DateOnly 'new' — ctor is Today (0-arg factory)
TEST(ZiaRtNew, DateOnlyNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestDate;
bind Viper.Time;
func start() {
    var d = new DateOnly();
}
)"));
}

// A-050: BinFile 'new' — ctor is Open, not New
TEST(ZiaRtNew, BinFileNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestBF;
bind Viper.IO;
func start() {
    var f = new BinFile("/tmp/test.dat", "rw");
}
)"));
}

// A-050: LineReader 'new' — ctor is Open, not New
TEST(ZiaRtNew, LineReaderNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestLR;
bind Viper.IO;
func start() {
    var r = new LineReader("/tmp/test.txt");
}
)"));
}

// A-050: LineWriter 'new' — ctor is Open, not New
TEST(ZiaRtNew, LineWriterNew)
{
    EXPECT_TRUE(compileOk(R"(
module TestLW;
bind Viper.IO;
func start() {
    var w = new LineWriter("/tmp/test_out.txt");
}
)"));
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
