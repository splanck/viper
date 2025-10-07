// File: tests/unit/test_cli_front_basic_verify_diag.cpp
// Purpose: Ensure cmdFrontBasic reports verifier failures with BASIC source locations.
// Key invariants: Diagnostic text must include the filename and line/column of the failing instruction.
// Ownership/Lifetime: Test owns temporary BASIC source file and diagnostic buffers.
// Links: src/tools/ilc/cmd_front_basic.cpp

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include "support/source_location.hpp"
#include "support/diagnostics.hpp"
#include "support/diag_expected.hpp"
#include "il/core/Module.hpp"
#include "il/core/Function.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "tools/ilc/cli.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Provide usage() expected by cmd_front_basic.cpp when embedded in the test.
void usage()
{
}

// Bring in the token utilities and diagnostic emitter implementation for the stub compiler.
#include "frontends/basic/Token.cpp"
#include "frontends/basic/DiagnosticEmitter.cpp"

// Include the implementation under test so the helper functions (e.g., runFrontBasic)
// are available within this translation unit.
#include "tools/ilc/cmd_front_basic.cpp"

namespace il::frontends::basic
{

bool BasicCompilerResult::succeeded() const
{
    return emitter && emitter->errorCount() == 0;
}

BasicCompilerResult compileBasic(const BasicCompilerInput &input,
                                 const BasicCompilerOptions & /*options*/,
                                 il::support::SourceManager &sm)
{
    BasicCompilerResult result{};
    result.emitter = std::make_unique<DiagnosticEmitter>(result.diagnostics, sm);

    uint32_t fileId = input.fileId.value_or(sm.addFile(std::string{input.path}));
    result.fileId = fileId;
    result.emitter->addSource(fileId, std::string{input.source});

    il::core::Module module{};
    il::core::Function mainFn;
    mainFn.name = "@main";
    mainFn.retType = il::core::Type{il::core::Type::Kind::I64};

    il::core::BasicBlock entry{};
    entry.label = "entry";

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type{il::core::Type::Kind::Void};
    ret.loc = {fileId, 2, 1};

    entry.instructions.push_back(ret);
    entry.terminated = true;

    mainFn.blocks.push_back(entry);
    module.functions.push_back(std::move(mainFn));

    result.module = std::move(module);
    return result;
}

} // namespace il::frontends::basic

int main()
{
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmpPath = fs::temp_directory_path();
    tmpPath /= "viper-front-basic-verify-diag-" + std::to_string(stamp) + ".bas";

    {
        std::ofstream ofs(tmpPath);
        ofs << "10 PRINT 1\n20 END\n";
    }

    std::string pathStr = tmpPath.string();
    std::vector<char> arg0(pathStr.begin(), pathStr.end());
    arg0.push_back('\0');

    char *argv[] = {const_cast<char *>("-run"), arg0.data()};

    std::ostringstream errStream;
    auto *oldErr = std::cerr.rdbuf(errStream.rdbuf());
    int rc = cmdFrontBasic(2, argv);
    std::cerr.flush();
    std::cerr.rdbuf(oldErr);

    fs::remove(tmpPath);

    const std::string errText = errStream.str();
    const std::string fileToken = tmpPath.filename().string() + ":2:1";
    const bool hasFileLocation = errText.find(fileToken) != std::string::npos;

    assert(rc != 0);
    assert(hasFileLocation);
    return 0;
}
