// File: tests/unit/test_cli_front_basic_verify_diag.cpp
// Purpose: Ensure cmdFrontBasic reports verifier failures with BASIC source locations.
// Key invariants: Diagnostic text must include the filename and line/column of the failing
// instruction. Ownership/Lifetime: Test owns temporary BASIC source file and diagnostic buffers.
// Links: src/tools/ilc/cmd_front_basic.cpp

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "support/diag_expected.hpp"
#include "support/diagnostics.hpp"
#include "support/source_location.hpp"
#include "support/source_manager.hpp"
#include "tools/ilc/cli.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Provide usage() expected by cmd_front_basic.cpp when embedded in the test.
void usage() {}

// Bring in the token utilities and diagnostic emitter implementation for the stub compiler.
#include "frontends/basic/DiagnosticEmitter.cpp"
#include "frontends/basic/Token.cpp"

// Include the implementation under test so the helper functions (e.g., runFrontBasic)
// are available within this translation unit.
#include "tools/ilc/cmd_front_basic.cpp"

namespace il::support
{
struct SourceManagerTestAccess
{
    static void setNextFileId(SourceManager &sm, uint64_t next)
    {
        sm.next_file_id_ = next;
    }
};
} // namespace il::support

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

    const std::string errText = errStream.str();
    const std::string fileToken = tmpPath.filename().string() + ":2:1";
    const bool hasFileLocation = errText.find(fileToken) != std::string::npos;

    assert(rc != 0);
    assert(hasFileLocation);

    il::support::SourceManager saturatedSm;
    il::support::SourceManagerTestAccess::setNextFileId(
        saturatedSm, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);

    std::ostringstream saturatedErr;
    oldErr = std::cerr.rdbuf(saturatedErr.rdbuf());
    int saturatedRc = cmdFrontBasicWithSourceManager(2, argv, saturatedSm);
    std::cerr.flush();
    std::cerr.rdbuf(oldErr);

    const std::string saturatedText = saturatedErr.str();
    const std::string exhaustionMessage =
        "error: " + std::string(il::support::kSourceManagerFileIdOverflowMessage);
    const bool reportedExhaustion = saturatedText.find(exhaustionMessage) != std::string::npos;
    size_t overflowCount = 0;
    size_t overflowPos = saturatedText.find(exhaustionMessage);
    while (overflowPos != std::string::npos)
    {
        ++overflowCount;
        overflowPos = saturatedText.find(exhaustionMessage, overflowPos + exhaustionMessage.size());
    }

    assert(saturatedRc != 0);
    assert(reportedExhaustion);
    assert(overflowCount == 1);

    fs::remove(tmpPath);

    return 0;
}
