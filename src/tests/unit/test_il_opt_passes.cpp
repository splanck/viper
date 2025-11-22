//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_opt_passes.cpp
// Purpose: Ensure il-opt respects explicit pass ordering and trims tokens. 
// Key invariants: Command runs without invoking usage() and applies both passes.
// Ownership/Lifetime: Temporary files created during the test are removed at exit.
// Links: src/tools/ilc/cmd_il_opt.cpp
//
//===----------------------------------------------------------------------===//

#include "tools/ilc/cli.hpp"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

bool gUsageCalled = false;
std::atomic<unsigned long long> gTempFileCounter{0};

struct TempFile
{
    std::filesystem::path path;

    explicit TempFile(std::string_view suffix)
    {
        auto id = gTempFileCounter.fetch_add(1, std::memory_order_relaxed);
        path = std::filesystem::temp_directory_path() /
               std::filesystem::path("il_opt_passes-" + std::to_string(id) + std::string(suffix));
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
    TempFile input{".il"};
    TempFile output{".il"};

    {
        std::ofstream ofs(input.path);
        ofs << "il 0.1.2\n";
        ofs << "extern @rt_abs_i64(i64) -> i64\n";
        ofs << "func @main() -> i64 {\n";
        ofs << "entry:\n";
        ofs << "  %abs = call @rt_abs_i64(-5)\n";
        ofs << "  %ptr = alloca 8\n";
        ofs << "  store i64 %ptr, 0\n";
        ofs << "  ret %abs\n";
        ofs << "}\n";
    }

    std::vector<std::string> storage{
        input.path.string(),
        "-o",
        output.path.string(),
        "--passes",
        "constfold, dce",
    };

    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (auto &arg : storage)
    {
        argv.push_back(arg.data());
    }

    gUsageCalled = false;
    int rc = cmdILOpt(static_cast<int>(argv.size()), argv.data());
    assert(rc == 0);
    assert(!gUsageCalled);

    const std::string content = readFile(output.path);
    assert(content.find("call @rt_abs_i64") == std::string::npos);
    assert(content.find("alloca") == std::string::npos);
    assert(content.find("store") == std::string::npos);
    assert(content.find("ret 5") != std::string::npos);

    return 0;
}
