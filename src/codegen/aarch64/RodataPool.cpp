//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/RodataPool.cpp
// Purpose: Deduplicate and emit string literals for AArch64 assembly.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/RodataPool.hpp"

#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

#include <cstdio>

namespace viper::codegen::aarch64
{

std::string RodataPool::makeLabel(std::size_t index)
{
    return std::string("L.str.") + std::to_string(index);
}

std::string RodataPool::escapeAsciz(std::string_view bytes)
{
    std::string s;
    s.reserve(bytes.size());
    for (unsigned char c : bytes)
    {
        switch (c)
        {
            case '"':
            case '\\':
                s.push_back('\\');
                s.push_back(static_cast<char>(c));
                break;
            case '\n':
                s += "\\n";
                break;
            case '\t':
                s += "\\t";
                break;
            default:
                if (c >= 32 && c < 127)
                {
                    s.push_back(static_cast<char>(c));
                }
                else
                {
                    char buf[5];
                    std::snprintf(buf, sizeof(buf), "\\x%02X", c);
                    s += buf;
                }
        }
    }
    return s;
}

void RodataPool::addString(const std::string &ilName, const std::string &bytes)
{
    auto it = contentToLabel_.find(bytes);
    if (it == contentToLabel_.end())
    {
        const std::string label = makeLabel(ordered_.size());
        contentToLabel_.emplace(bytes, label);
        ordered_.emplace_back(label, bytes);
        nameToLabel_[ilName] = label;
    }
    else
    {
        nameToLabel_[ilName] = it->second;
    }
}

void RodataPool::buildFromModule(const il::core::Module &mod)
{
    for (const auto &g : mod.globals)
    {
        if (g.type.kind == il::core::Type::Kind::Str)
            addString(g.name, g.init);
    }
}

void RodataPool::emit(std::ostream &os) const
{
    if (ordered_.empty())
        return;
#if defined(__APPLE__)
    os << ".section __TEXT,__const\n";
#else
    os << ".section .rodata\n";
#endif
    for (const auto &pair : ordered_)
    {
        const std::string &label = pair.first;
        const std::string &bytes = pair.second;
        os << label << ":\n";
        os << "  .asciz \"" << escapeAsciz(bytes) << "\"\n";
    }
    os << "\n";
}

} // namespace viper::codegen::aarch64
