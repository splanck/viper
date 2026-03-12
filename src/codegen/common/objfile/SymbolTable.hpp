//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/SymbolTable.hpp
// Purpose: Symbol table for object file generation. Manages symbol entries
//          with name, binding, section, and offset information.
// Key invariants:
//   - Index 0 is reserved for the null entry (ELF requirement)
//   - Names are stored as mapped C-level names (e.g., rt_print_i64)
//   - Platform-specific mangling (Mach-O underscore) is applied by writers
//   - All local symbols precede global symbols (required by ELF and Mach-O)
// Ownership/Lifetime:
//   - Value type, typically lives for the duration of binary emission
// Links: codegen/common/objfile/CodeSection.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::objfile
{

/// Symbol binding/visibility.
enum class SymbolBinding : uint8_t
{
    Local,    ///< File-local symbol (static function, local label).
    Global,   ///< Globally visible defined symbol (exported function).
    External, ///< Undefined symbol (imported from another object/library).
};

/// Which section a symbol is defined in.
enum class SymbolSection : uint8_t
{
    Text,      ///< .text section (machine code).
    Rodata,    ///< .rodata / __TEXT,__const section (read-only data).
    Undefined, ///< External symbol, not defined in this object.
};

/// A symbol entry in the object file.
struct Symbol
{
    std::string name;
    SymbolBinding binding = SymbolBinding::External;
    SymbolSection section = SymbolSection::Undefined;
    size_t offset = 0; ///< Byte offset within section (0 for External).
    size_t size = 0;   ///< Size of symbol (0 if unknown).
};

/// Manages symbol entries for object file generation.
///
/// Index 0 is reserved for the null symbol (ELF requirement). The null entry
/// is created automatically in the constructor.
class SymbolTable
{
public:
    SymbolTable();

    /// Add a symbol, returning its index.
    uint32_t add(Symbol sym);

    /// Find an existing symbol by name, or add it as External.
    uint32_t findOrAdd(const std::string &name);

    /// Look up a symbol by index.
    const Symbol &at(uint32_t index) const { return symbols_[index]; }

    /// Mutable look up a symbol by index.
    Symbol &at(uint32_t index) { return symbols_[index]; }

    /// Total number of symbols (including null entry at index 0).
    uint32_t count() const { return static_cast<uint32_t>(symbols_.size()); }

    /// Iteration support.
    using const_iterator = std::vector<Symbol>::const_iterator;
    const_iterator begin() const { return symbols_.begin(); }
    const_iterator end() const { return symbols_.end(); }

    /// Direct access to the underlying vector.
    const std::vector<Symbol> &symbols() const { return symbols_; }

private:
    std::vector<Symbol> symbols_;
    std::unordered_map<std::string, uint32_t> nameIndex_;
};

} // namespace viper::codegen::objfile
