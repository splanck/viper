//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RodataPool.cpp
/// @brief Read-only data pool for string literal deduplication and emission.
///
/// This file implements the RodataPool class which manages string literals
/// during AArch64 code generation. It deduplicates identical string constants
/// and emits them to the appropriate read-only data section in the generated
/// assembly.
///
/// **What is a Rodata Pool?**
/// A rodata (read-only data) pool collects all string literals used in a
/// compilation unit and emits them efficiently. String deduplication ensures
/// that identical strings share a single storage location, reducing binary size.
///
/// **Pool Architecture:**
/// ```
/// IL Module:                           RodataPool:
/// ┌────────────────────────┐          ┌────────────────────────────────┐
/// │ global @hello = "Hi"   │          │  contentToLabel_:              │
/// │ global @greet = "Hi"   │ ───────► │    "Hi" → "L.str.0"            │
/// │ global @world = "World"│          │    "World" → "L.str.1"         │
/// └────────────────────────┘          │                                │
///                                     │  nameToLabel_:                 │
///                                     │    "@hello" → "L.str.0"        │
///                                     │    "@greet" → "L.str.0" (dup!) │
///                                     │    "@world" → "L.str.1"        │
///                                     │                                │
///                                     │  ordered_:                     │
///                                     │    [("L.str.0", "Hi"),         │
///                                     │     ("L.str.1", "World")]      │
///                                     └────────────────────────────────┘
/// ```
///
/// **Deduplication Strategy:**
/// 1. Each string added via `addString()` is checked against existing content
/// 2. If content already exists, the IL name maps to the existing label
/// 3. If content is new, a fresh label is generated and the string is stored
/// ```cpp
/// // First occurrence: creates new entry
/// pool.addString("@hello", "Hi");  // L.str.0 → "Hi"
///
/// // Duplicate content: reuses existing label
/// pool.addString("@greet", "Hi");  // @greet → L.str.0 (same as @hello)
/// ```
///
/// **Escape Sequences:**
/// The `escapeAsciz()` function converts raw bytes to assembly-safe strings:
///
/// | Input Byte | Output Sequence |
/// |------------|-----------------|
/// | `"`        | `\"`            |
/// | `\`        | `\\`            |
/// | `\n`       | `\n`            |
/// | `\t`       | `\t`            |
/// | 0x00-0x1F  | `\x00`-`\x1F`   |
/// | 0x80-0xFF  | `\x80`-`\xFF`   |
/// | 0x20-0x7E  | printable char  |
///
/// **Assembly Output:**
/// ```asm
/// ; On macOS/Darwin:
/// .section __TEXT,__const
/// L.str.0:
///   .asciz "Hi"
/// L.str.1:
///   .asciz "World"
///
/// ; On Linux:
/// .section .rodata
/// L.str.0:
///   .asciz "Hi"
/// L.str.1:
///   .asciz "World"
/// ```
///
/// **Integration with Code Generation:**
/// ```cpp
/// // 1. Build pool from module globals
/// RodataPool pool;
/// pool.buildFromModule(module);
///
/// // 2. During instruction lowering, reference labels
/// // adrp x0, L.str.0@PAGE
/// // add  x0, x0, L.str.0@PAGEOFF
///
/// // 3. Emit rodata section before/after code
/// pool.emit(output);
/// ```
///
/// **Label Naming Convention:**
/// - Labels are generated as `L.str.N` where N is the insertion order index
/// - The `L.` prefix indicates a local label (not exported in symbol table)
/// - This prevents symbol collisions with user-defined identifiers
///
/// @see AsmEmitter.cpp For assembly generation that uses rodata labels
/// @see LowerILToMIR.cpp For const_str instruction lowering
///
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
