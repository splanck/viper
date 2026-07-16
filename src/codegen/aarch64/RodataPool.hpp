//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/RodataPool.hpp
// Purpose: Deduplicate string literals and emit a rodata section for AArch64.
// Key invariants: Identical string contents are pooled to a single label;
//                 ordered_ preserves first-seen insertion order for deterministic output.
// Ownership/Lifetime: Constructed per-function or per-module; borrows IL Module
//                     data during buildFromModule but does not retain references.
// Links: codegen/x86_64/AsmEmitter.hpp (x86-64 equivalent), docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace il::core {
struct Module;
}

namespace viper::codegen::aarch64 {

/// @brief Manages a pool of deduplicated read-only string data for AArch64 assembly emission.
/// @details Collects string literals from IL global constants, deduplicates by content,
///          assigns unique assembly labels, and emits a `.section __DATA,__const` (macOS)
///          or `.rodata` (Linux) section containing `.asciz` directives.
class RodataPool {
  public:
    /// @brief Get the mapping from IL global string names to their pooled assembly labels.
    /// @return Const reference to the name-to-label map. Populated after buildFromModule().
    const std::unordered_map<std::string, std::string> &nameToLabel() const noexcept {
        return nameToLabel_;
    }

    /// @brief Get the ordered (label, content) pairs for binary emission.
    /// @return Const reference to the insertion-ordered pool entries.
    const std::vector<std::pair<std::string, std::string>> &entries() const noexcept {
        return ordered_;
    }

    /// @brief Scan an IL module's globals and pool all string constants.
    /// @details Iterates over all globals in @p mod, identifies string constants,
    ///          deduplicates by content, and records the IL name to label mapping.
    /// @param mod The IL module whose global string constants should be pooled.
    void buildFromModule(const il::core::Module &mod);

    /// @brief Emit the read-only data section to an output stream.
    /// @details Writes platform-appropriate section directives followed by labeled
    ///          `.asciz` entries for each unique string in insertion order.
    /// @param os The output stream to write assembly text to.
    void emit(std::ostream &os, const TargetInfo &target) const;

    /// @brief Emit writable scalar globals to a `.data` section.
    /// @details Each non-string global is given an externally visible, mangled
    ///          symbol matching the AsmEmitter's adrp/add references, plus a
    ///          size-appropriate initializer directive. Without this, `gaddr @g`
    ///          on a mutable scalar global resolves to an undefined symbol.
    void emitData(std::ostream &os, const TargetInfo &target) const;

    /// @brief A writable scalar global to emit into `.data`.
    struct DataGlobal {
        std::string name;          ///< Unmangled IL global name (e.g. "counter").
        int sizeBytes = 0;         ///< 1, 2, 4, or 8.
        bool isFloat = false;      ///< True for f64.
        std::string init;          ///< Serialized initializer; empty means zero.
        std::vector<uint8_t> bytes;///< Little-endian initializer bytes for the object path.
    };

    /// @brief Writable scalar globals collected from the module (for the binary path).
    const std::vector<DataGlobal> &dataGlobals() const noexcept {
        return dataGlobals_;
    }

  private:
    /// @brief Map from string content to its deduplicated assembly label.
    std::unordered_map<std::string, std::string> contentToLabel_;

    /// @brief Map from IL global name to the pooled assembly label.
    std::unordered_map<std::string, std::string> nameToLabel_;

    /// @brief Ordered list of (label, content) pairs for deterministic emission.
    std::vector<std::pair<std::string, std::string>> ordered_;

    /// @brief Writable scalar (non-string) globals collected from the module.
    std::vector<DataGlobal> dataGlobals_;

    /// @brief Generate a unique rodata label for the given pool index.
    /// @param index Zero-based index of the string in the pool.
    /// @return A label string like ".Lstr0", ".Lstr1", etc.
    static std::string makeLabel(std::size_t index);

    /// @brief Escape a raw byte string for use in a `.asciz` assembly directive.
    /// @details Converts non-printable and special characters to octal escapes.
    /// @param bytes The raw string content to escape.
    /// @return The escaped string suitable for `.asciz`.
    static std::string escapeAsciz(std::string_view bytes);

    /// @brief Add a string to the pool, deduplicating by content.
    /// @param ilName The IL global name referencing this string.
    /// @param bytes The raw string content.
    void addString(const std::string &ilName, const std::string &bytes);
};

} // namespace viper::codegen::aarch64
