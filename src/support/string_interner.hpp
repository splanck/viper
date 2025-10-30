// File: src/support/string_interner.hpp
// Purpose: Declares string interning and symbol types.
// Key invariants: Symbol id 0 is invalid.
// Ownership/Lifetime: Interner owns stored strings.
// Links: docs/codemap.md
#pragma once

#include "symbol.hpp"
#include <deque>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace il::support
{

/// @brief Interns strings to provide stable Symbol identifiers.
/// @invariant Symbol 0 is reserved for invalid.
/// @ownership Stores copies of strings internally.
class StringInterner
{
  public:
    /// Constructs an interner optionally bounded by @p maxSymbols.
    ///
    /// The limit defaults to the full 32-bit Symbol address space, ensuring
    /// backwards compatibility with existing callers.  Tests can request a
    /// smaller cap to exercise overflow handling deterministically.
    explicit StringInterner(uint32_t maxSymbols = std::numeric_limits<uint32_t>::max()) noexcept;

    /// Interns a string to produce a stable symbol for repeated use.
    ///
    /// Stores a copy of @p str if it has not been seen before and assigns it a
    /// new Symbol. Subsequent calls with the same string yield the existing
    /// Symbol without duplicating storage, enabling fast comparisons and
    /// lookups. When the interner reaches its capacity, the function returns
    /// an invalid Symbol (id 0) and leaves the input string uninterned.
    /// @param str String to intern.
    /// @return Symbol uniquely identifying the interned string.
    Symbol intern(std::string_view str);

    /// Retrieves the original string associated with a Symbol.
    ///
    /// Use to obtain the text for a symbol returned by intern(), for example in
    /// diagnostics or reverse mappings. Passing an invalid Symbol yields an
    /// empty view.
    /// @param sym Symbol previously returned by intern().
    /// @return View of the interned string.
    std::string_view lookup(Symbol sym) const;

    StringInterner(const StringInterner &other);
    StringInterner &operator=(const StringInterner &other);
    StringInterner(StringInterner &&) noexcept = default;
    StringInterner &operator=(StringInterner &&) noexcept = default;

  private:
    struct TransparentHash
    {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }

        size_t operator()(const std::string &s) const noexcept
        {
            return (*this)(std::string_view{s});
        }
    };

    struct TransparentEqual
    {
        using is_transparent = void;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
        {
            return lhs == rhs;
        }

        bool operator()(const std::string &lhs, const std::string &rhs) const noexcept
        {
            return lhs == rhs;
        }

        bool operator()(const std::string &lhs, std::string_view rhs) const noexcept
        {
            return lhs == rhs;
        }

        bool operator()(std::string_view lhs, const std::string &rhs) const noexcept
        {
            return lhs == rhs;
        }
    };

    /// Maps string content to assigned symbols for O(1) lookup during interning.
    std::unordered_map<std::string_view, Symbol, TransparentHash, TransparentEqual> map_;
    /// Retains copies of interned strings so lookups return stable views.
    std::deque<std::string> storage_;
    /// Maximum number of unique symbols representable by this interner.
    uint32_t maxSymbols_;

    void rebuildMap();
};
} // namespace il::support
