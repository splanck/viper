//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/string_interner.hpp
// Purpose: Declares string interning and symbol types.
// Key invariants: Symbol id 0 is invalid.
// Ownership/Lifetime: Interner owns stored strings.
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "symbol.hpp"
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace il::support {

/// @brief Interns strings to provide stable Symbol identifiers.
/// @invariant Symbol 0 is reserved for invalid.
/// @ownership Stores copies of strings internally.
class StringInterner {
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

    /// @brief Check whether @p sym identifies a currently interned string.
    /// @param sym Symbol to validate against this interner.
    /// @return True when @p sym is nonzero and within the owned storage range.
    [[nodiscard]] bool contains(Symbol sym) const;

    /// @brief Retrieve an interned string while preserving invalid-vs-empty state.
    /// @param sym Symbol previously returned by intern().
    /// @return String view for a valid symbol, or std::nullopt for an invalid one.
    [[nodiscard]] std::optional<std::string_view> lookupOptional(Symbol sym) const;

    /// @brief Copy constructor.
    /// @details Deep-copies all interned strings and rebuilds the internal map
    ///          so that string_view keys point into the new storage.
    /// @param other Interner to copy from.
    StringInterner(const StringInterner &other);

    /// @brief Copy assignment operator.
    /// @details Replaces the current contents with a deep copy of @p other
    ///          and rebuilds the internal map.
    /// @param other Interner to copy from.
    /// @return Reference to this interner.
    StringInterner &operator=(const StringInterner &other);

    /// @brief Move constructor.
    /// @details Moves the owned string storage and rebuilds the string_view-keyed
    ///          lookup table so every key points into the destination object.
    /// @param other Interner to move from; left in a valid but unspecified state.
    StringInterner(StringInterner &&other);

    /// @brief Move assignment operator.
    /// @details Replaces this interner with @p other's storage and rebuilds the
    ///          lookup table from the moved strings.
    /// @param other Interner to move from; left in a valid but unspecified state.
    /// @return Reference to this interner.
    StringInterner &operator=(StringInterner &&other);

  private:
    /// @brief Heterogeneous hash enabling map lookups keyed by string_view.
    /// @details The @c is_transparent tag lets the unordered_map hash both
    ///          std::string and std::string_view identically, so intern() can
    ///          probe the map with a borrowed view instead of allocating a
    ///          temporary std::string for every query.
    struct TransparentHash {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }

        size_t operator()(const std::string &s) const noexcept {
            return (*this)(std::string_view{s});
        }
    };

    /// @brief Heterogeneous equality matching the TransparentHash key family.
    /// @details Provides every string/string_view comparison pairing so the map
    ///          can compare a borrowed lookup key against a stored key without an
    ///          intermediate allocation. Must agree with TransparentHash on which
    ///          keys are equal.
    struct TransparentEqual {
        using is_transparent = void;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }

        bool operator()(const std::string &lhs, const std::string &rhs) const noexcept {
            return lhs == rhs;
        }

        bool operator()(const std::string &lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }

        bool operator()(std::string_view lhs, const std::string &rhs) const noexcept {
            return lhs == rhs;
        }
    };

    using InternMap =
        std::unordered_map<std::string_view, Symbol, TransparentHash, TransparentEqual>;

    /// Serializes access to storage_ and map_ for shared tooling/server use.
    mutable std::mutex mutex_;
    /// Maps string content to assigned symbols for O(1) lookup during interning.
    InternMap map_;
    /// Retains copies of interned strings so lookups return stable views.
    std::deque<std::string> storage_;
    /// Maximum number of unique symbols representable by this interner.
    uint32_t maxSymbols_;

    /// @brief Rebuild the lookup map from storage after move operations.
    /// @details After a move assignment or copy, the map's string_view keys
    /// may become invalidated. This method reconstructs the map by iterating
    /// through the storage_ deque and creating fresh entries.
    void rebuildMap();

    /// @brief Build a lookup map whose keys point into @p storage.
    /// @param storage Owned string storage that will back every string_view key.
    /// @return A complete string-to-symbol map for @p storage.
    /// @details The helper builds into a temporary map so assignment operators can
    ///          prepare all throwing state before replacing the receiving object.
    static InternMap buildMapForStorage(const std::deque<std::string> &storage);

    /// @brief Swap all owned interner state with @p other.
    /// @param other Interner whose state should be exchanged with this object.
    /// @details The intern map stores string_view keys into the companion storage
    ///          deque.  Swapping both containers together preserves that pairing
    ///          and lets assignment operators provide strong replacement semantics.
    void swapWith(StringInterner &other) noexcept;
};
} // namespace il::support
