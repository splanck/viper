// File: src/vm/Trace.cpp
// Purpose: Implement deterministic tracing for IL VM steps.
// Key invariants: Each executed instruction produces at most one flushed line.
// Ownership/Lifetime: Uses external streams; no resource ownership.
// Links: docs/dev/vm.md
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
#include <list>
#include <locale>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace il::vm
{

/// @brief Determine whether tracing output should be emitted.
/// @return True when the trace mode is not TraceConfig::Off.
bool TraceConfig::enabled() const
{
    return mode != Off;
}

struct TraceSink::SourceCache
{
    struct Key
    {
        uint32_t file = 0;
        uint32_t line = 0;

        bool operator==(const Key &other) const noexcept
        {
            return file == other.file && line == other.line;
        }
    };

    struct KeyHash
    {
        size_t operator()(const Key &key) const noexcept
        {
            return (static_cast<size_t>(key.file) << 32) ^ static_cast<size_t>(key.line);
        }
    };

    struct Entry
    {
        std::string text;
        std::list<Key>::iterator orderIt;
    };

    explicit SourceCache(size_t cap) : capacity(cap) {}

    const std::string *get(uint32_t file, uint32_t line)
    {
        Key key{file, line};
        auto it = map.find(key);
        if (it == map.end())
            return nullptr;
        order.splice(order.begin(), order, it->second.orderIt);
        return &it->second.text;
    }

    const std::string *put(uint32_t file, uint32_t line, std::string value)
    {
        Key key{file, line};
        auto it = map.find(key);
        if (it != map.end())
        {
            it->second.text = std::move(value);
            order.splice(order.begin(), order, it->second.orderIt);
            return &it->second.text;
        }
        order.push_front(key);
        auto inserted = map.emplace(key, Entry{std::move(value), order.begin()});
        if (!inserted.second)
            return &inserted.first->second.text;
        if (map.size() > capacity)
        {
            const Key &oldKey = order.back();
            map.erase(oldKey);
            order.pop_back();
        }
        return &inserted.first->second.text;
    }

  private:
    size_t capacity;
    std::list<Key> order;
    std::unordered_map<Key, Entry, KeyHash> map;
};

namespace
{
constexpr size_t kDefaultSourceCacheSize = 16;

/// @brief Temporarily force the C locale for numeric formatting.
class LocaleGuard
{
    std::ostream &os;
    std::locale oldLoc;
    std::string oldC;

  public:
    explicit LocaleGuard(std::ostream &s) : os(s), oldLoc(s.getloc())
    {
        if (const char *c = std::setlocale(LC_NUMERIC, nullptr))
            oldC = c;
        os.imbue(std::locale::classic());
        std::setlocale(LC_NUMERIC, "C");
    }

    ~LocaleGuard()
    {
        if (!oldC.empty())
            std::setlocale(LC_NUMERIC, oldC.c_str());
        os.imbue(oldLoc);
    }
};

/// @brief Print a Value with stable numeric formatting.
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

TraceSink::TraceSink(TraceConfig cfg) : cfg(cfg)
{
#ifdef _WIN32
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (cfg.mode == TraceConfig::SRC)
        srcCache = std::make_unique<SourceCache>(kDefaultSourceCacheSize);
}

TraceSink::~TraceSink() = default;

void TraceSink::onStep(const il::core::Instr &in, const Frame &fr)
{
    if (!cfg.enabled())
        return;
    LocaleGuard lg(std::cerr);
    std::cerr << std::noboolalpha << std::dec;
    const auto *fn = fr.func;
    if (!fn)
        return;
    auto locIt = fr.instrLocations.find(&in);
    if (locIt == fr.instrLocations.end())
        return;
    const auto *blk = locIt->second.block;
    size_t ip = locIt->second.index;
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
        std::string_view srcView;
        if (cfg.sm && in.loc.isValid())
        {
            std::string path(cfg.sm->getPath(in.loc.file_id));
            std::filesystem::path p(path);
            locStr = p.filename().string() + ':' + std::to_string(in.loc.line) + ':' +
                     std::to_string(in.loc.column);
            if (!srcCache)
                srcCache = std::make_unique<SourceCache>(kDefaultSourceCacheSize);
            const std::string *cachedLine = srcCache->get(in.loc.file_id, in.loc.line);
            if (!cachedLine)
            {
                std::ifstream f(path);
                if (f)
                {
                    std::string line;
                    for (uint32_t i = 0; i < in.loc.line && std::getline(f, line); ++i)
                        ;
                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                        line.pop_back();
                    cachedLine = srcCache->put(in.loc.file_id, in.loc.line, std::move(line));
                }
            }
            if (cachedLine && !cachedLine->empty())
            {
                if (in.loc.column > 0 && static_cast<size_t>(in.loc.column - 1) < cachedLine->size())
                    srcView = std::string_view(*cachedLine).substr(in.loc.column - 1);
                else
                    srcView = std::string_view(*cachedLine);
            }
        }
        std::cerr << "[SRC] " << locStr << "  (fn=@" << fn->name << " blk=" << blk->label << " ip=#"
                  << ip << ')';
        if (!srcView.empty())
            std::cerr << "  " << srcView;
        std::cerr << '\n' << std::flush;
    }
}

} // namespace il::vm
