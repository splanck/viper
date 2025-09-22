// File: tests/unit/test_vm_trace_cache.cpp
// Purpose: Verify VM tracing caches instruction locations and source lines without altering output.
// Key invariants: Cached tracing must emit identical lines as legacy uncached formatting.
// Ownership: Test constructs IL module and executes VM twice under different trace modes.
// Links: docs/dev/vm.md

#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
void legacyPrintValue(std::ostream &os, const il::core::Value &v)
{
    switch (v.kind)
    {
        case il::core::Value::Kind::Temp:
            os << "%t" << std::dec << v.id;
            break;
        case il::core::Value::Kind::ConstInt:
            os << std::dec << v.i64;
            break;
        case il::core::Value::Kind::ConstFloat:
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.17g", v.f64);
            os << buf;
            break;
        }
        case il::core::Value::Kind::ConstStr:
            os << '"' << v.str << '"';
            break;
        case il::core::Value::Kind::GlobalAddr:
            os << '@' << v.str;
            break;
        case il::core::Value::Kind::NullPtr:
            os << "null";
            break;
    }
}

std::string formatLegacyIL(const il::core::Function &fn,
                           const il::core::BasicBlock &blk,
                           size_t ip,
                           const il::core::Instr &in)
{
    std::ostringstream os;
    os << std::noboolalpha << std::dec;
    os << "[IL] fn=@" << fn.name << " blk=" << blk.label << " ip=#" << ip
       << " op=" << il::core::toString(in.op);
    if (!in.operands.empty())
    {
        os << ' ';
        for (size_t idx = 0; idx < in.operands.size(); ++idx)
        {
            if (idx)
                os << ", ";
            legacyPrintValue(os, in.operands[idx]);
        }
    }
    if (in.result)
        os << " -> %t" << std::dec << *in.result;
    os << '\n';
    return os.str();
}

std::string formatLegacySRC(const il::core::Function &fn,
                            const il::core::BasicBlock &blk,
                            size_t ip,
                            const il::core::Instr &in,
                            const il::support::SourceManager &sm)
{
    std::ostringstream os;
    std::string locStr = "<unknown>";
    std::string srcLine;
    if (in.loc.isValid())
    {
        std::string path(sm.getPath(in.loc.file_id));
        std::filesystem::path file(path);
        locStr = file.filename().string() + ':' + std::to_string(in.loc.line) + ':' +
                 std::to_string(in.loc.column);
        std::ifstream f(path);
        if (f)
        {
            std::string line;
            for (uint32_t lineNo = 0; lineNo < in.loc.line && std::getline(f, line); ++lineNo)
                ;
            if (!line.empty())
            {
                if (in.loc.column > 0 && in.loc.column - 1 < line.size())
                    srcLine = line.substr(in.loc.column - 1);
                else
                    srcLine = line;
                while (!srcLine.empty() && (srcLine.back() == '\n' || srcLine.back() == '\r'))
                    srcLine.pop_back();
            }
        }
    }
    os << "[SRC] " << locStr << "  (fn=@" << fn.name << " blk=" << blk.label << " ip=#" << ip << ')';
    if (!srcLine.empty())
        os << "  " << srcLine;
    os << '\n';
    return os.str();
}

class StderrCapture
{
    std::streambuf *oldBuf;
    std::ostringstream buffer;

  public:
    StderrCapture() : oldBuf(std::cerr.rdbuf(buffer.rdbuf())) {}

    ~StderrCapture()
    {
        std::cerr.rdbuf(oldBuf);
    }

    std::string str() const
    {
        return buffer.str();
    }
};

std::filesystem::path dataFilePath()
{
    std::filesystem::path src(__FILE__);
    return std::filesystem::absolute(src).parent_path().parent_path() / "data" / "trace_cache.txt";
}
} // namespace

int main()
{
    using namespace il::core;
    using il::support::SourceManager;

    SourceManager sm;
    const auto dataPath = dataFilePath();
    const uint32_t fileId = sm.addFile(dataPath.string());

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);
    fn.valueNames.resize(2);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    Instr add;
    add.result = 0u;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt(40));
    add.operands.push_back(Value::constInt(2));
    add.loc = {fileId, 2, 3};
    entry.instructions.push_back(add);

    Instr sub;
    sub.result = 1u;
    sub.op = Opcode::Sub;
    sub.type = Type(Type::Kind::I64);
    sub.operands.push_back(Value::temp(0));
    sub.operands.push_back(Value::constInt(1));
    sub.loc = {fileId, 2, 10};
    entry.instructions.push_back(sub);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(1));
    ret.loc = {fileId, 3, 1};
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    m.functions.push_back(std::move(fn));

    const auto &storedFn = m.functions.front();
    std::string expectedIL;
    std::string expectedSRC;
    for (const auto &block : storedFn.blocks)
    {
        for (size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            expectedIL += formatLegacyIL(storedFn, block, idx, block.instructions[idx]);
            expectedSRC += formatLegacySRC(storedFn, block, idx, block.instructions[idx], sm);
        }
    }

    {
        il::vm::TraceConfig cfg{};
        cfg.mode = il::vm::TraceConfig::IL;
        il::vm::VM vm(m, cfg);
        StderrCapture cap;
        int64_t rv = vm.run();
        assert(rv == 41);
        std::string actual = cap.str();
        assert(actual == expectedIL);
    }

    {
        il::vm::TraceConfig cfg{};
        cfg.mode = il::vm::TraceConfig::SRC;
        cfg.sm = &sm;
        il::vm::VM vm(m, cfg);
        StderrCapture cap;
        vm.run();
        std::string actual = cap.str();
        assert(actual == expectedSRC);
    }

    return 0;
}
