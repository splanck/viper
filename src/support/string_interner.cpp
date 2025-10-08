/**
 * @file string_interner.cpp
 * @brief Implements the string interning facility used across the compiler.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     The interner assigns compact `Symbol` identifiers to canonical copies of
 *     strings.  Identifier value `0` is reserved to denote "no symbol"; real
 *     identifiers are 1-based indices into the backing storage vector.
 */

#include "string_interner.hpp"

namespace il::support
{

/**
 * @brief Interns the provided string and returns its stable symbol.
 *
 * The method first checks the hash map for an existing entry to avoid copying
 * duplicate text.  When the string is new, it appends an owned copy to
 * `storage_`, constructs a `Symbol` using the resulting 1-based index, and
 * inserts the mapping into `map_` using the stored string view as the key.  The
 * function never produces identifier `0`, preserving the invariant that this
 * value signals "invalid".
 *
 * @param str String slice to canonicalize.
 * @return Symbol handle representing the canonicalized string.
 */
Symbol StringInterner::intern(std::string_view str)
{
    auto it = map_.find(str);
    if (it != map_.end())
        return it->second;
    storage_.emplace_back(str);
    Symbol sym{static_cast<uint32_t>(storage_.size())};
    map_.emplace(storage_.back(), sym);
    return sym;
}

/**
 * @brief Retrieves the canonical string associated with a symbol.
 *
 * The lookup verifies that the identifier falls within the valid range of the
 * storage vector (1..N).  Requests outside that range—including identifier `0`
 * used for "no symbol"—return an empty `std::string_view` to signal the caller
 * that the lookup failed.
 *
 * @param sym Symbol that was previously produced by the interner.
 * @return View into the stored string, or an empty view when @p sym is invalid.
 */
std::string_view StringInterner::lookup(Symbol sym) const
{
    if (sym.id == 0 || sym.id > storage_.size())
        return {};
    return storage_[sym.id - 1];
}
} // namespace il::support
