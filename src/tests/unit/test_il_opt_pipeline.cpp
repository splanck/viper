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

#include "tools/ilc/cli.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{

bool gUsageCalled = false;

struct TempFile
{
    std::filesystem::path path;

    explicit TempFile(std::string_view suffix)
    {
        path = std::filesystem::temp_directory_path() /
               std::filesystem::path("il_opt_pipeline-" + std::string(suffix));
    }

    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

std::string readFile(const std::filesystem::path &p)
{
    std::ifstream ifs(p);
    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

} // namespace

void usage()
{
    gUsageCalled = true;
}

int main()
{
    // Input with a promotable alloca
    TempFile input{".il"};
    {
        std::ofstream ofs(input.path);
        ofs << "il 0.1.2\n";
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
        std::vector<std::string> args{input.path.string(), "-o", o0.path.string(), "--pipeline",
                                      "O0"};
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

    // Default (no --passes/pipeline) should use O1, promoting away the stack ops.
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

    return 0;
}

