//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia 'new' keyword with runtime classes.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::zia;
using namespace il::support;

namespace {

bool compileSource(const std::string &source, bool printDiagnostics) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = "<test>"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    if (!result.succeeded() && printDiagnostics) {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    return result.succeeded();
}

bool compileOk(const std::string &source) {
    return compileSource(source, true);
}

bool compileFails(const std::string &source) {
    return !compileSource(source, false);
}

TEST(ZiaRtNew, FrozenSetNamedFactoryIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestFS;
bind Zanna.Collections;
/// @brief Start.
func start() {    var s = new Seq();
    var x = new FrozenSet(s);
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestFSFactory;
bind Zanna.Collections;
/// @brief Start.
func start() {    var s = new Seq();
    var x = FrozenSet.FromSeq(s);
}
)"));
}

TEST(ZiaRtNew, FrozenMapNamedFactoryIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestFM;
bind Zanna.Collections;
/// @brief Start.
func start() {    var keys = new Seq();
    var vals = new Seq();
    var x = new FrozenMap(keys, vals);
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestFMFactory;
bind Zanna.Collections;
/// @brief Start.
func start() {    var keys = new Seq();
    var vals = new Seq();
    var x = FrozenMap.FromSeqs(keys, vals);
}
)"));
}

TEST(ZiaRtNew, VersionParseIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestVer;
bind Zanna.Text;
/// @brief Start.
func start() {    var v = new Version("1.0.0");
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestVerFactory;
bind Zanna.Text;
/// @brief Start.
func start() {    var v = Version.Parse("1.0.0");
}
)"));
}

// A-032: CompiledPattern 'new' — ctor RT_FUNC was missing entirely
TEST(ZiaRtNew, CompiledPatternNew) {
    EXPECT_TRUE(compileOk(R"(
module TestCP;
bind Zanna.Text;
/// @brief Start.
func start() {    var p = new CompiledPattern("hello.*");
}
)"));
}

// A-033: Scanner 'new' — ctor RT_FUNC was missing entirely
TEST(ZiaRtNew, ScannerNew) {
    EXPECT_TRUE(compileOk(R"(
module TestScanner;
bind Zanna.Text;
/// @brief Start.
func start() {    var s = new Scanner("hello world");
}
)"));
}

TEST(ZiaRtNew, DateOnlyTodayIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestDate;
bind Zanna.Time;
/// @brief Start.
func start() {    var d = new DateOnly();
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestDateFactory;
bind Zanna.Time;
/// @brief Start.
func start() {    var d = DateOnly.Today();
}
)"));
}

TEST(ZiaRtNew, BinFileOpenIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestBF;
bind Zanna.IO;
/// @brief Start.
func start() {    var f = new BinFile("/tmp/test.dat", "rw");
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestBFFactory;
bind Zanna.IO;
/// @brief Start.
func start() {    var f = BinFile.Open("/tmp/test.dat", "rw");
}
)"));
}

TEST(ZiaRtNew, LineReaderOpenIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestLR;
bind Zanna.IO;
/// @brief Start.
func start() {    var r = new LineReader("/tmp/test.txt");
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestLRFactory;
bind Zanna.IO;
/// @brief Start.
func start() {    var r = LineReader.Open("/tmp/test.txt");
}
)"));
}

TEST(ZiaRtNew, LineWriterOpenIsNotNew) {
    EXPECT_TRUE(compileFails(R"(
module TestLW;
bind Zanna.IO;
/// @brief Start.
func start() {    var w = new LineWriter("/tmp/test_out.txt");
}
)"));
    EXPECT_TRUE(compileOk(R"(
module TestLWFactory;
bind Zanna.IO;
/// @brief Start.
func start() {    var w = LineWriter.Open("/tmp/test_out.txt");
}
)"));
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
