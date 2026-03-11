//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/Json.hpp
// Purpose: Minimal JSON value type, parser, and emitter for protocol use.
// Key invariants:
//   - Objects preserve insertion order (vector of pairs)
//   - Parse errors throw std::runtime_error with descriptive message
//   - Round-trip: parse(emit(v)) == v for all well-formed values
// Ownership/Lifetime:
//   - JsonValue is a self-contained value type (copyable, movable)
//   - No external memory references
// Links: tools/zia-server/JsonRpc.hpp, tools/zia-server/Transport.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace viper::server
{

/// @brief JSON value types.
enum class JsonType : uint8_t
{
    Null,
    Bool,
    Int,
    Double,
    String,
    Array,
    Object,
};

/// @brief A JSON value (null, bool, int, double, string, array, or object).
///
/// Objects are stored as ordered vectors of (key, value) pairs to preserve
/// insertion order, which is important for deterministic protocol output.
/// O(n) key lookup is acceptable since protocol objects are small (<30 keys).
class JsonValue
{
  public:
    using ArrayType = std::vector<JsonValue>;
    using ObjectType = std::vector<std::pair<std::string, JsonValue>>;

    /// @brief Construct a null JSON value.
    JsonValue();

    /// @brief Construct a boolean JSON value.
    explicit JsonValue(bool b);

    /// @brief Construct an integer JSON value.
    explicit JsonValue(int64_t i);

    /// @brief Construct an integer JSON value from int.
    explicit JsonValue(int i);

    /// @brief Construct a double JSON value.
    explicit JsonValue(double d);

    /// @brief Construct a string JSON value.
    explicit JsonValue(std::string s);

    /// @brief Construct a string JSON value from string_view.
    explicit JsonValue(std::string_view s);

    /// @brief Construct a string JSON value from C string.
    explicit JsonValue(const char *s);

    /// @brief Construct an array JSON value.
    explicit JsonValue(ArrayType arr);

    /// @brief Construct an object JSON value.
    explicit JsonValue(ObjectType obj);

    /// @brief Get the type of this value.
    [[nodiscard]] JsonType type() const;

    /// @brief Check if this value is null.
    [[nodiscard]] bool isNull() const;

    // --- Accessors (return default on type mismatch) ---

    [[nodiscard]] bool asBool(bool def = false) const;
    [[nodiscard]] int64_t asInt(int64_t def = 0) const;
    [[nodiscard]] double asDouble(double def = 0.0) const;
    [[nodiscard]] const std::string &asString() const;
    [[nodiscard]] const ArrayType &asArray() const;
    [[nodiscard]] const ObjectType &asObject() const;

    // --- Object member access ---

    /// @brief Get a member by key (nullptr if not found or not an object).
    [[nodiscard]] const JsonValue *get(const std::string &key) const;

    /// @brief Get a member by key (returns null JsonValue if not found).
    [[nodiscard]] const JsonValue &operator[](const std::string &key) const;

    /// @brief Check if this object has a member with the given key.
    [[nodiscard]] bool has(const std::string &key) const;

    // --- Array access ---

    /// @brief Get array size (0 if not an array).
    [[nodiscard]] size_t size() const;

    /// @brief Get array element by index (returns null if out of range).
    [[nodiscard]] const JsonValue &at(size_t index) const;

    // --- Builders ---

    /// @brief Create a JSON object from an initializer list.
    static JsonValue object(std::initializer_list<std::pair<std::string, JsonValue>> members);

    /// @brief Create a JSON array from an initializer list.
    static JsonValue array(std::initializer_list<JsonValue> elems);

    /// @brief Create a JSON object from a vector of pairs.
    static JsonValue object(ObjectType members);

    /// @brief Create a JSON array from a vector.
    static JsonValue array(ArrayType elems);

    // --- Serialization ---

    /// @brief Serialize to compact JSON string (no whitespace).
    [[nodiscard]] std::string toCompactString() const;

    // --- Parsing ---

    /// @brief Parse a JSON string into a JsonValue.
    /// @throws std::runtime_error on parse error.
    static JsonValue parse(std::string_view input);

    // --- Comparison ---

    bool operator==(const JsonValue &other) const;
    bool operator!=(const JsonValue &other) const;

  private:
    using Storage =
        std::variant<std::nullptr_t, bool, int64_t, double, std::string, ArrayType, ObjectType>;
    Storage storage_;

    static const std::string kEmptyString;
    static const ArrayType kEmptyArray;
    static const ObjectType kEmptyObject;
    static const JsonValue kNull;

    void emitTo(std::string &out) const;
};

} // namespace viper::server
