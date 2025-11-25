//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_trace_unicode_path.cpp
// Purpose: Verify VM source tracing loads files with non-ASCII paths.
// Key invariants: Trace sink must decode cached file contents when the
// Ownership/Lifetime: Standalone unit test executable.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main()
{
    namespace fs = std::filesystem;

    fs::path tempFile;
    std::string utf8Path;
#if defined(__cpp_char8_t)
    const std::u8string u8Name = u8"トレース.il";
    tempFile = fs::temp_directory_path() / fs::path(u8Name);
    const std::u8string u8FullPath = tempFile.u8string();
    utf8Path.assign(u8FullPath.begin(), u8FullPath.end());
#else
    tempFile = fs::temp_directory_path() / fs::path("トレース.il");
    utf8Path = tempFile.string();
#endif
    {
        std::ofstream out(tempFile);
        assert(out.is_open());
        out << "ret 42\n";
    }

    il::support::SourceManager sm;
    const uint32_t fileId = sm.addFile(utf8Path);
    assert(fileId != 0);

    il::core::Module module;
    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";

    il::core::Instr retInstr;
    retInstr.op = il::core::Opcode::Ret;
    retInstr.type = il::core::Type(il::core::Type::Kind::I64);
    retInstr.operands.push_back(il::core::Value::constInt(42));
    retInstr.loc.file_id = fileId;
    retInstr.loc.line = 1;
    retInstr.loc.column = 1;

    entry.instructions.push_back(retInstr);
    entry.terminated = true;

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);

    il::vm::TraceConfig traceCfg;
    traceCfg.mode = il::vm::TraceConfig::SRC;
    traceCfg.sm = &sm;

    std::ostringstream captured;
    auto *oldErr = std::cerr.rdbuf(captured.rdbuf());

    il::vm::VM vm(module, traceCfg);
    const auto result = vm.run();

    std::cerr.rdbuf(oldErr);

    assert(result == 42);

    const std::string trace = captured.str();
    std::string filename;
#if defined(__cpp_char8_t)
    const std::u8string u8Filename = tempFile.filename().u8string();
    filename.assign(u8Filename.begin(), u8Filename.end());
#else
    filename = tempFile.filename().string();
#endif
    assert(trace.find(filename) != std::string::npos);
    assert(trace.find("ret 42") != std::string::npos);

    fs::remove(tempFile);

    return 0;
}
