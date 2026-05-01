//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Ensure ilc il-opt can drive canonical pipelines (O0/O1/O2) via the IL pass
// manager while keeping manual pass selection working implicitly (default O1).
//
//===----------------------------------------------------------------------===//

#include "tools/viper/cli.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool gUsageCalled = false;

struct TempFile {
    std::filesystem::path path;

    explicit TempFile(std::string_view suffix) {
        path = std::filesystem::temp_directory_path() /
               std::filesystem::path("il_opt_pipeline-" + std::string(suffix));
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

/// @brief Read file.
std::string readFile(const std::filesystem::path &p) {
    std::ifstream ifs(p);
    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

} // namespace

/// @brief Usage.
void usage() {
    gUsageCalled = true;
}

int main() {
    // Input with a promotable alloca
    TempFile input{".il"};
    {
        std::ofstream ofs(input.path);
        ofs << "il 0.2.0\n";
        ofs << "func @main() -> i64 {\n";
        ofs << "entry:\n";
        ofs << "  %ptr = alloca 8\n";
        ofs << "  store i64 %ptr, 5\n";
        ofs << "  %v = load i64 %ptr\n";
        ofs << "  ret %v\n";
        ofs << "}\n";
    }

    // O0: should not run mem2reg, so stack ops remain.
    TempFile o0{".o0.il"};
    {
        std::vector<std::string> args{
            input.path.string(), "-o", o0.path.string(), "--pipeline", "O0"};
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(a.data());
        gUsageCalled = false;
        const int rc = cmdILOpt(static_cast<int>(argv.size()), argv.data());
        assert(rc == 0);
        assert(!gUsageCalled);
        const std::string out = readFile(o0.path);
        assert(out.find("alloca") != std::string::npos);
        assert(out.find("store") != std::string::npos);
        assert(out.find("load") != std::string::npos);
    }

    // Default (no --passes/pipeline) should use O1.
    // O1 now includes mem2reg, so SCCP can see through the promoted scalar.
    TempFile def{".o1.il"};
    {
        std::vector<std::string> args{input.path.string(), "-o", def.path.string(), "-verify-each"};
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(a.data());
        gUsageCalled = false;
        const int rc = cmdILOpt(static_cast<int>(argv.size()), argv.data());
        assert(rc == 0);
        assert(!gUsageCalled);
        const std::string out = readFile(def.path);
        assert(out.find("alloca") == std::string::npos);
        assert(out.find("store") == std::string::npos);
        assert(out.find("load") == std::string::npos);
        assert(out.find("ret 5") != std::string::npos);
    }

    // Lower-case rehabilitation pipeline names should be accepted directly.
    TempFile rehab{".rehab.il"};
    {
        std::vector<std::string> args{
            input.path.string(), "-o", rehab.path.string(), "--pipeline", "rehab-peephole"};
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(a.data());
        gUsageCalled = false;
        const int rc = cmdILOpt(static_cast<int>(argv.size()), argv.data());
        assert(rc == 0);
        assert(!gUsageCalled);
        const std::string out = readFile(rehab.path);
        assert(out.find("func @main") != std::string::npos);
    }

    // O2 diagnostics should be usable from the CLI without changing output.
    TempFile diag{".diag.il"};
    {
        std::vector<std::string> args{input.path.string(),
                                      "-o",
                                      diag.path.string(),
                                      "--pipeline",
                                      "O2",
                                      "--pass-stats",
                                      "--bisect-pipeline"};
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(a.data());
        gUsageCalled = false;
        const int rc = cmdILOpt(static_cast<int>(argv.size()), argv.data());
        assert(rc == 0);
        assert(!gUsageCalled);
        const std::string out = readFile(diag.path);
        assert(out.find("ret 5") != std::string::npos);
    }

    // Pass-manager verification failures must propagate as il-opt failures.
    TempFile invalid{".invalid.il"};
    {
        std::ofstream ofs(invalid.path);
        ofs << "il 0.2.0\n";
        ofs << "func @main(%x: i64) -> i64 {\n";
        ofs << "entry:\n";
        ofs << "  %y = sub %x, 1\n";
        ofs << "  ret %y\n";
        ofs << "}\n";
    }
    TempFile invalidOut{".invalid.out.il"};
    {
        std::vector<std::string> args{invalid.path.string(), "-o", invalidOut.path.string(),
                                      "--passes", "simplify-cfg", "-verify-each"};
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(a.data());
        gUsageCalled = false;
        const int rc = cmdILOpt(static_cast<int>(argv.size()), argv.data());
        assert(rc != 0);
        assert(!gUsageCalled);
    }

    return 0;
}
