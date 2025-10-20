//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Trace.cpp
// Purpose: Implement deterministic tracing for IL VM steps.
// Key invariants: Each executed instruction produces at most one flushed line.
// Ownership/Lifetime: Uses external streams; no resource ownership.
// Links: docs/dev/vm.md
//
//===----------------------------------------------------------------------===//
#include "vm/Trace.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <clocale>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace il::vm
{

/// @brief Determine whether tracing output should be emitted.
/// @details Checks the configured trace mode and returns @c true when either
///          IL or source tracing is active.
/// @return True when the trace mode is not TraceConfig::Off.
bool TraceConfig::enabled() const
{
    return mode != Off;
}

namespace
{
/// @brief Temporarily force the C locale for numeric formatting.
/// @details Guard class that sets the classic locale on a stream and adjusts
///          the global C locale while tracing prints numeric values.  Restores
///          the original settings on destruction.
class LocaleGuard
{
    std::ostream &os;
    std::locale oldLoc;
    std::string oldC;

  public:
    /// @brief Apply the classic C locale to the wrapped stream.
    /// @param s Stream that will emit trace output.
    explicit LocaleGuard(std::ostream &s) : os(s), oldLoc(s.getloc())
    {
        if (const char *c = std::setlocale(LC_NUMERIC, nullptr))
            oldC = c;
        os.imbue(std::locale::classic());
        std::setlocale(LC_NUMERIC, "C");
    }

    /// @brief Restore the previous locale configuration.
    ~LocaleGuard()
    {
        if (!oldC.empty())
            std::setlocale(LC_NUMERIC, oldC.c_str());
        os.imbue(oldLoc);
    }
};

/// @brief Print a Value with stable numeric formatting.
/// @details Emits temporaries, integers, floats, strings, globals, and null
///          values in a deterministic textual form so trace logs can be parsed
///          predictably.
/// @param os Stream receiving the rendered value.
/// @param v IL value to render.
void printValue(std::ostream &os, const il::core::Value &v)
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
} // namespace

/// @brief Construct a TraceSink with the given configuration.
/// @details Stores the configuration and performs any one-time host
///          initialisation (such as switching Windows stderr to binary mode).
/// @param cfg Trace configuration controlling emission behaviour and source lookup.
TraceSink::TraceSink(TraceConfig cfg) : cfg(cfg)
{
#ifdef _WIN32
    _setmode(_fileno(stderr), _O_BINARY);
#endif
}

/// @brief Precompute instruction source locations for a prepared frame.
/// @details Builds a cache mapping IL instructions to their owning block and
///          instruction index the first time a function is traced.  Subsequent
///          lookups for the same function reuse the cached data.
/// @param fr The frame whose function's instructions should be indexed.
void TraceSink::onFramePrepared(const Frame &fr)
{
    if (!fr.func)
        return;
    auto [it, inserted] = instrLocations.try_emplace(fr.func);
    if (!inserted)
        return;
    auto &map = it->second;
    for (const auto &block : fr.func->blocks)
    {
        for (size_t idx = 0; idx < block.instructions.size(); ++idx)
            map.emplace(&block.instructions[idx], InstrLocation{&block, idx});
    }
}

/// @brief Retrieve cached file contents, loading from disk if necessary.
/// @details Uses the source manager to resolve file paths and stores line data
///          in a map keyed by file identifier.  When the file cannot be
///          located, the method returns @c nullptr.
/// @param file_id Source manager identifier for the file.
/// @param path_hint Optional filesystem path to use when the source manager path is empty.
/// @return Cached file entry or nullptr when the file cannot be resolved.
const TraceSink::FileCacheEntry *TraceSink::getOrLoadFile(uint32_t file_id, std::string path_hint)
{
    if (!cfg.sm || file_id == 0)
        return nullptr;
    auto it = fileCache.find(file_id);
    if (it != fileCache.end())
        return &it->second;

    FileCacheEntry entry;
    if (!path_hint.empty())
        entry.path = std::move(path_hint);
    else
        entry.path = std::string(cfg.sm->getPath(file_id));

    if (entry.path.empty())
        return nullptr;

    std::ifstream f(entry.path);
    if (f)
    {
        std::string line;
        while (std::getline(f, line))
            entry.lines.push_back(line);
    }

    auto [pos, inserted] = fileCache.emplace(file_id, std::move(entry));
    (void)inserted;
    return &pos->second;
}

/// @brief Emit a trace line for a single executed instruction.
/// @details Renders either IL-level or source-level trace output depending on
///          configuration.  Performs lazy lookups of block/IP pairs and, for
///          source traces, loads source lines on demand.
/// @param in Instruction being executed.
/// @param fr Execution frame that provides function and block context.
void TraceSink::onStep(const il::core::Instr &in, const Frame &fr)
{
    if (!cfg.enabled())
        return;
    LocaleGuard lg(std::cerr);
    std::cerr << std::noboolalpha << std::dec;
    const auto *fn = fr.func;
    if (!fn)
        return;
    const il::core::BasicBlock *blk = nullptr;
    size_t ip = 0;
    auto fnIt = instrLocations.find(fn);
    if (fnIt != instrLocations.end())
    {
        auto locIt = fnIt->second.find(&in);
        if (locIt != fnIt->second.end())
        {
            blk = locIt->second.block;
            ip = locIt->second.ip;
        }
    }
    if (!blk)
        return;
    if (cfg.mode == TraceConfig::IL)
    {
        std::cerr << "[IL] fn=@" << fn->name << " blk=" << blk->label << " ip=#" << ip
                  << " op=" << il::core::toString(in.op);
        if (!in.operands.empty())
        {
            std::cerr << ' ';
            for (size_t i = 0; i < in.operands.size(); ++i)
            {
                if (i)
                    std::cerr << ", ";
                printValue(std::cerr, in.operands[i]);
            }
        }
        if (in.result)
            std::cerr << " -> %t" << std::dec << *in.result;
        std::cerr << '\n' << std::flush;
        return;
    }
    if (cfg.mode == TraceConfig::SRC)
    {
        std::string locStr = "<unknown>";
        std::string srcLine;
        if (cfg.sm && in.loc.isValid())
        {
            std::string path(cfg.sm->getPath(in.loc.file_id));
            std::filesystem::path p(path);
            locStr = p.filename().string() + ':' + std::to_string(in.loc.line) + ':' +
                     std::to_string(in.loc.column);
            if (!path.empty())
            {
                const auto *entry = getOrLoadFile(in.loc.file_id, std::move(path));
                if (entry && in.loc.line > 0 &&
                    static_cast<size_t>(in.loc.line) <= entry->lines.size())
                {
                    const std::string &line = entry->lines[in.loc.line - 1];
                    if (!line.empty())
                    {
                        if (in.loc.column > 0 && in.loc.column - 1 < line.size())
                            srcLine = line.substr(in.loc.column - 1);
                        else
                            srcLine = line;
                        while (!srcLine.empty() &&
                               (srcLine.back() == '\n' || srcLine.back() == '\r'))
                            srcLine.pop_back();
                    }
                }
            }
        }
        std::cerr << "[SRC] " << locStr << "  (fn=@" << fn->name << " blk=" << blk->label << " ip=#"
                  << ip << ')';
        if (!srcLine.empty())
            std::cerr << "  " << srcLine;
        std::cerr << '\n' << std::flush;
    }
}

} // namespace il::vm
