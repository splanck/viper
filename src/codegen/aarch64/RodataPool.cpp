//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/RodataPool.cpp
// Purpose: Read-only data pool — string literal deduplication and emission
//          for AArch64 assembly output.
//
// Key invariants:
//   - Strings are deduplicated by content; identical bytes share one label.
//   - Labels are emitted in first-seen insertion order for determinism.
//   - Section directive is selected from the requested target ABI, not host OS.
//
// Ownership/Lifetime:
//   - See RodataPool.hpp.
//
// Links: codegen/aarch64/RodataPool.hpp,
//        il/core/Module.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/RodataPool.hpp"

#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

namespace viper::codegen::aarch64 {

std::string RodataPool::makeLabel(std::size_t index) {
    return std::string("L.str.") + std::to_string(index);
}

std::string RodataPool::escapeAsciz(std::string_view bytes) {
    std::string s;
    s.reserve(bytes.size());
    for (unsigned char c : bytes) {
        switch (c) {
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
                if (c >= 32 && c < 127) {
                    s.push_back(static_cast<char>(c));
                } else {
                    s.push_back('\\');
                    s.push_back(static_cast<char>('0' + ((c >> 6) & 0x7)));
                    s.push_back(static_cast<char>('0' + ((c >> 3) & 0x7)));
                    s.push_back(static_cast<char>('0' + (c & 0x7)));
                }
        }
    }
    return s;
}

void RodataPool::addString(const std::string &ilName, const std::string &bytes) {
    auto it = contentToLabel_.find(bytes);
    if (it == contentToLabel_.end()) {
        const std::string label = makeLabel(ordered_.size());
        contentToLabel_.emplace(bytes, label);
        ordered_.emplace_back(label, bytes);
        nameToLabel_[ilName] = label;
    } else {
        nameToLabel_[ilName] = it->second;
    }
}

void RodataPool::buildFromModule(const il::core::Module &mod) {
    for (const auto &g : mod.globals) {
        if (g.type.kind == il::core::Type::Kind::Str)
            addString(g.name, g.init);
    }
}

void RodataPool::emit(std::ostream &os, const TargetInfo &target) const {
    if (ordered_.empty())
        return;
    if (target.isLinux())
        os << ".section .rodata\n";
    else if (target.isWindows())
        os << ".section .rdata,\"dr\"\n";
    else
        os << ".section __TEXT,__const\n";
    for (const auto &pair : ordered_) {
        const std::string &label = pair.first;
        const std::string &bytes = pair.second;
        os << label << ":\n";
        os << "  .asciz \"" << escapeAsciz(bytes) << "\"\n";
    }
    os << "\n";
}

} // namespace viper::codegen::aarch64
