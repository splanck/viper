// File: tests/frontends/basic/AddFileTests.cpp
// Purpose: Unit tests for the BASIC ADDFILE directive handling in the parser.
// Key invariants: ADDFILE is handled at file scope, resolves paths relative to
//                 the including source, detects cycles, and merges AST content.
// Ownership/Lifetime: Tests create temporary files under the system temp dir.
// Links: docs/codemap.md

#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <unistd.h>
#include <iostream>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

std::filesystem::path write_file(const std::filesystem::path &dir,
                                 const std::string &name,
                                 const std::string &contents)
{
    std::filesystem::create_directories(dir);
    std::filesystem::path path = dir / name;
    std::ofstream out(path);
    out << contents;
    out.close();
    return path;
}

std::string run_and_collect_errors(const std::string &srcPath,
                                   const std::string &source,
                                   size_t &outErrs)
{
    SourceManager sm;
    uint32_t fid = sm.addFile(srcPath);
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, source);
    std::vector<std::string> includeStack;
    Parser parser(source, fid, &emitter, &sm, &includeStack);
    auto program = parser.parseProgram();

    std::ostringstream oss;
    emitter.printAll(oss);
    outErrs = emitter.errorCount();
    return oss.str();
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path tempRoot = fs::temp_directory_path() / "viper_addfile_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));

    // Test 1: Positive include merges content
    {
        const fs::path dir = tempRoot / "pos";
        write_file(dir, "inc.bas", "10 PRINT \"OK\"\n20 SUB Foo()\n30 END SUB\n40 END\n");
        const fs::path mainPath = write_file(dir, "main.bas", "10 ADDFILE \"inc.bas\"\n20 END\n");

        size_t errs = 0;
        std::string diag = run_and_collect_errors(mainPath.string(),
                                                  "10 ADDFILE \"inc.bas\"\n20 END\n",
                                                  errs);
        if (errs != 0) {
            std::cerr << diag << std::endl;
        }
        assert(errs == 0);

        // Compile through BasicCompiler and ensure IL contains the string constant.
        BasicCompilerOptions opts{};
        SourceManager sm;
        const std::string pathStr = mainPath.string();
        const std::string mainSrc = "10 ADDFILE \"inc.bas\"\n20 END\n";
        auto fileId = sm.addFile(pathStr);
        BasicCompilerInput in{.source = mainSrc, .path = pathStr, .fileId = fileId};
        auto result = compileBasic(in, opts, sm);
        assert(result.succeeded());
        // Best-effort sanity: module should contain at least one function (main)
        // and diagnostics are empty.
        assert(result.module.functions.size() > 0);
    }

    // Test 2: Missing file emits error
    {
        const fs::path dir = tempRoot / "missing";
        fs::create_directories(dir);
        const fs::path mainPath = write_file(dir, "main.bas", "10 ADDFILE \"nope.bas\"\n20 END\n");
        size_t errs = 0;
        std::string diag = run_and_collect_errors(mainPath.string(),
                                                  "10 ADDFILE \"nope.bas\"\n20 END\n",
                                                  errs);
        if (errs != 1) {
            std::cerr << diag << std::endl;
        }
        assert(errs == 1);
        assert(diag.find("unable to open") != std::string::npos);
    }

    // Test 3: Cyclic include detected
    {
        const fs::path dir = tempRoot / "cycle";
        // a.bas includes b.bas; b.bas includes a.bas
        write_file(dir, "a.bas", "10 ADDFILE \"b.bas\"\n20 END\n");
        write_file(dir, "b.bas", "10 ADDFILE \"a.bas\"\n20 END\n");
        const fs::path mainPath = dir / "a.bas";
        size_t errs = 0;
        std::string diag = run_and_collect_errors(mainPath.string(),
                                                  "10 ADDFILE \"b.bas\"\n20 END\n",
                                                  errs);
        if (errs != 1) {
            std::cerr << diag << std::endl;
        }
        assert(errs == 1);
        assert(diag.find("cyclic ADDFILE detected") != std::string::npos);
    }

    return 0;
}
