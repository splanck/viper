//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/Json.cpp
// Purpose: JSON value type implementation — parser and emitter.
// Key invariants:
//   - Parser is recursive descent, handles all JSON types per RFC 8259
//   - Strings handle all standard escape sequences (\", \\, \/, \b, \f, \n, \r, \t, \uXXXX)
//   - Numbers: integers stored as int64_t, floats as double
// Ownership/Lifetime:
//   - All allocations via std::string/std::vector (RAII)
// Links: tools/lsp-common/Json.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/Json.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

namespace viper::server {

// Static members
const std::string JsonValue::kEmptyString;
const JsonValue::ArrayType JsonValue::kEmptyArray;
const JsonValue::ObjectType JsonValue::kEmptyObject;
const JsonValue JsonValue::kNull;

// --- Constructors ---

JsonValue::JsonValue() : storage_(nullptr) {}

JsonValue::JsonValue(bool b) : storage_(b) {}

JsonValue::JsonValue(int64_t i) : storage_(i) {}

JsonValue::JsonValue(int i) : storage_(static_cast<int64_t>(i)) {}

JsonValue::JsonValue(double d) : storage_(d) {}

JsonValue::JsonValue(std::string s) : storage_(std::move(s)) {}

JsonValue::JsonValue(std::string_view s) : storage_(std::string(s)) {}

JsonValue::JsonValue(const char *s) : storage_(std::string(s ? s : "")) {}

JsonValue::JsonValue(ArrayType arr) : storage_(std::move(arr)) {}

JsonValue::JsonValue(ObjectType obj) : storage_(std::move(obj)) {}

// --- Type inspection ---

JsonType JsonValue::type() const {
    return static_cast<JsonType>(storage_.index());
}

bool JsonValue::isNull() const {
    return storage_.index() == 0;
}

// --- Accessors ---

bool JsonValue::asBool(bool def) const {
    if (auto *p = std::get_if<bool>(&storage_))
        return *p;
    return def;
}

int64_t JsonValue::asInt(int64_t def) const {
    if (auto *p = std::get_if<int64_t>(&storage_))
        return *p;
    if (auto *p = std::get_if<double>(&storage_)) {
        if (!std::isfinite(*p))
            return def;
        const long double raw = static_cast<long double>(*p);
        const long double value = std::trunc(raw);
        if (value < static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
            value > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
            return def;
        }
        return static_cast<int64_t>(value);
    }
    return def;
}

double JsonValue::asDouble(double def) const {
    if (auto *p = std::get_if<double>(&storage_))
        return *p;
    if (auto *p = std::get_if<int64_t>(&storage_))
        return static_cast<double>(*p);
    return def;
}

const std::string &JsonValue::asString() const {
    if (auto *p = std::get_if<std::string>(&storage_))
        return *p;
    return kEmptyString;
}

const JsonValue::ArrayType &JsonValue::asArray() const {
    if (auto *p = std::get_if<ArrayType>(&storage_))
        return *p;
    return kEmptyArray;
}

const JsonValue::ObjectType &JsonValue::asObject() const {
    if (auto *p = std::get_if<ObjectType>(&storage_))
        return *p;
    return kEmptyObject;
}

// --- Object access ---

const JsonValue *JsonValue::get(const std::string &key) const {
    if (auto *obj = std::get_if<ObjectType>(&storage_)) {
        for (const auto &[k, v] : *obj) {
            if (k == key)
                return &v;
        }
    }
    return nullptr;
}

const JsonValue &JsonValue::operator[](const std::string &key) const {
    if (const auto *v = get(key))
        return *v;
    return kNull;
}

bool JsonValue::has(const std::string &key) const {
    return get(key) != nullptr;
}

// --- Array access ---

size_t JsonValue::size() const {
    if (auto *arr = std::get_if<ArrayType>(&storage_))
        return arr->size();
    if (auto *obj = std::get_if<ObjectType>(&storage_))
        return obj->size();
    return 0;
}

const JsonValue &JsonValue::at(size_t index) const {
    if (auto *arr = std::get_if<ArrayType>(&storage_)) {
        if (index < arr->size())
            return (*arr)[index];
    }
    return kNull;
}

// --- Builders ---

JsonValue JsonValue::object(std::initializer_list<std::pair<std::string, JsonValue>> members) {
    return JsonValue(ObjectType(members.begin(), members.end()));
}

JsonValue JsonValue::array(std::initializer_list<JsonValue> elems) {
    return JsonValue(ArrayType(elems.begin(), elems.end()));
}

JsonValue JsonValue::object(ObjectType members) {
    return JsonValue(std::move(members));
}

JsonValue JsonValue::array(ArrayType elems) {
    return JsonValue(std::move(elems));
}

// --- Comparison ---

bool JsonValue::operator==(const JsonValue &other) const {
    return storage_ == other.storage_;
}

bool JsonValue::operator!=(const JsonValue &other) const {
    return storage_ != other.storage_;
}

// --- Emitter ---

/// @brief Append @p s to @p out as a quoted, escaped JSON string literal.
/// @details Escapes the standard control/quote characters and encodes any other
///          byte below 0x20 as a \\u00XX sequence; the surrounding quotes are
///          added by this function.
static void emitString(std::string &out, const std::string &s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control characters as \u00XX
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

void JsonValue::emitTo(std::string &out) const {
    switch (type()) {
        case JsonType::Null:
            out += "null";
            break;
        case JsonType::Bool:
            out += asBool() ? "true" : "false";
            break;
        case JsonType::Int: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(asInt()));
            out += buf;
            break;
        }
        case JsonType::Double: {
            double d = asDouble();
            if (std::isnan(d) || std::isinf(d)) {
                out += "null"; // JSON has no NaN/Inf
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.17g", d);
                out += buf;
                // Ensure it looks like a float (has . or e)
                if (std::string_view(buf).find_first_of(".eE") == std::string_view::npos)
                    out += ".0";
            }
            break;
        }
        case JsonType::String:
            emitString(out, asString());
            break;
        case JsonType::Array: {
            out += '[';
            const auto &arr = asArray();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0)
                    out += ',';
                arr[i].emitTo(out);
            }
            out += ']';
            break;
        }
        case JsonType::Object: {
            out += '{';
            const auto &obj = asObject();
            for (size_t i = 0; i < obj.size(); ++i) {
                if (i > 0)
                    out += ',';
                emitString(out, obj[i].first);
                out += ':';
                obj[i].second.emitTo(out);
            }
            out += '}';
            break;
        }
    }
}

std::string JsonValue::toCompactString() const {
    std::string out;
    out.reserve(128);
    emitTo(out);
    return out;
}

// ==========================================================================
// JSON Parser — recursive descent
// ==========================================================================

namespace {

/// @brief Recursive-descent JSON parser over a borrowed string_view (RFC 8259).
/// @details Tracks a cursor into the input and exposes a single parse() entry
///          point; malformed input is reported by throwing std::runtime_error.
class JsonParser {
  public:
    /// @brief Construct a parser positioned at the start of @p input.
    explicit JsonParser(std::string_view input) : src_(input), pos_(0) {}

    /// @brief Parse the entire input as a single JSON value.
    /// @details Skips surrounding whitespace and rejects trailing characters.
    /// @throws std::runtime_error on any syntax error.
    JsonValue parse() {
        skipWhitespace();
        auto val = parseValue();
        skipWhitespace();
        if (pos_ < src_.size())
            error("unexpected trailing characters");
        return val;
    }

  private:
    static constexpr int kMaxDepth = 512;

    std::string_view src_;
    size_t pos_;

    /// @brief Throw a std::runtime_error annotated with the current position.
    [[noreturn]] void error(const char *msg) const {
        std::string err = "JSON parse error at position ";
        err += std::to_string(pos_);
        err += ": ";
        err += msg;
        throw std::runtime_error(err);
    }

    /// @brief Return the current character without consuming it ('\0' at end).
    char peek() const {
        if (pos_ >= src_.size())
            return '\0';
        return src_[pos_];
    }

    /// @brief Consume and return the current character; errors at end of input.
    char advance() {
        if (pos_ >= src_.size())
            error("unexpected end of input");
        return src_[pos_++];
    }

    /// @brief Consume the next character, erroring if it is not @p c.
    void expect(char c) {
        char got = advance();
        if (got != c) {
            std::string msg = "expected '";
            msg += c;
            msg += "', got '";
            msg += got;
            msg += "'";
            error(msg.c_str());
        }
    }

    /// @brief Advance past any run of JSON whitespace (space/tab/newline/CR).
    void skipWhitespace() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    /// @brief Consume @p literal if it matches at the cursor; return whether it did.
    bool tryConsume(std::string_view literal) {
        if (src_.substr(pos_).substr(0, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    /// @brief Parse a single JSON value, dispatching on the leading character.
    /// @param depth Current nesting depth, bounded by kMaxDepth to stop runaway
    ///        recursion on deeply nested or malicious input.
    JsonValue parseValue(int depth = 0) {
        if (depth > kMaxDepth)
            error("maximum nesting depth exceeded");
        skipWhitespace();
        char c = peek();
        if (c == '\0')
            error("unexpected end of input");

        switch (c) {
            case 'n':
                if (tryConsume("null"))
                    return JsonValue();
                error("invalid literal");
            case 't':
                if (tryConsume("true"))
                    return JsonValue(true);
                error("invalid literal");
            case 'f':
                if (tryConsume("false"))
                    return JsonValue(false);
                error("invalid literal");
            case '"':
                return JsonValue(parseString());
            case '[':
                return parseArray(depth + 1);
            case '{':
                return parseObject(depth + 1);
            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                    return parseNumber();
                error("unexpected character");
        }
    }

    /// @brief Parse a quoted JSON string, decoding escapes (including \\uXXXX).
    std::string parseString() {
        expect('"');
        std::string result;
        while (true) {
            if (pos_ >= src_.size())
                error("unterminated string");
            char c = src_[pos_++];
            if (c == '"')
                return result;
            if (c == '\\') {
                if (pos_ >= src_.size())
                    error("unterminated escape");
                char esc = src_[pos_++];
                switch (esc) {
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    case '/':
                        result += '/';
                        break;
                    case 'b':
                        result += '\b';
                        break;
                    case 'f':
                        result += '\f';
                        break;
                    case 'n':
                        result += '\n';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    case 'u': {
                        uint32_t cp = parseHex4();
                        // Handle surrogate pairs
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos_ + 1 < src_.size() && src_[pos_] == '\\' &&
                                src_[pos_ + 1] == 'u') {
                                pos_ += 2;
                                uint32_t lo = parseHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF)
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                else
                                    error("invalid surrogate pair");
                            } else {
                                error("missing low surrogate");
                            }
                        }
                        if (cp >= 0xDC00 && cp <= 0xDFFF)
                            error("lone low surrogate");
                        encodeUtf8(result, cp);
                        break;
                    }
                    default:
                        error("invalid escape sequence");
                }
            } else {
                const auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20)
                    error("unescaped control character in string");
                if (uc < 0x80) {
                    result += c;
                } else {
                    appendValidatedUtf8(result, uc);
                }
            }
        }
    }

    /// @brief Append a raw non-ASCII UTF-8 sequence after validating RFC 3629 form.
    /// @details JSON input is UTF-8. This helper consumes the continuation bytes belonging to
    /// @p lead, rejects malformed, overlong, surrogate, and out-of-range encodings, and appends
    /// the original byte sequence to @p out when valid.
    void appendValidatedUtf8(std::string &out, unsigned char lead) {
        int length = 0;
        uint32_t cp = 0;
        uint32_t minCodePoint = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            cp = lead & 0x1F;
            minCodePoint = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            cp = lead & 0x0F;
            minCodePoint = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            cp = lead & 0x07;
            minCodePoint = 0x10000;
        } else {
            error("invalid UTF-8 sequence in string");
        }

        std::string bytes;
        bytes.reserve(static_cast<std::size_t>(length));
        bytes.push_back(static_cast<char>(lead));
        for (int i = 1; i < length; ++i) {
            if (pos_ >= src_.size())
                error("truncated UTF-8 sequence in string");
            const auto cont = static_cast<unsigned char>(src_[pos_++]);
            if ((cont & 0xC0u) != 0x80u)
                error("invalid UTF-8 continuation byte in string");
            cp = (cp << 6) | static_cast<uint32_t>(cont & 0x3Fu);
            bytes.push_back(static_cast<char>(cont));
        }

        if (cp < minCodePoint || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            error("invalid UTF-8 code point in string");
        out += bytes;
    }

    /// @brief Parse exactly four hexadecimal digits into a code unit value.
    uint32_t parseHex4() {
        if (pos_ + 4 > src_.size())
            error("incomplete \\u escape");
        uint32_t val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = src_[pos_++];
            val <<= 4;
            if (c >= '0' && c <= '9')
                val |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                val |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                val |= static_cast<uint32_t>(c - 'A' + 10);
            else
                error("invalid hex digit in \\u escape");
        }
        return val;
    }

    /// @brief Append a Unicode code point to @p out as UTF-8 (1–4 bytes).
    static void encodeUtf8(std::string &out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    /// @brief Parse a JSON number, returning an Int when integral or a Double when
    ///        it has a fraction/exponent; rejects out-of-range or malformed values.
    JsonValue parseNumber() {
        size_t start = pos_;
        bool isFloat = false;

        if (peek() == '-')
            ++pos_;

        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9')
                ++pos_;
        } else {
            error("invalid number");
        }

        if (pos_ < src_.size() && src_[pos_] == '.') {
            isFloat = true;
            ++pos_;
            if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9')
                error("expected digit after decimal point");
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9')
                ++pos_;
        }

        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-'))
                ++pos_;
            if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9')
                error("expected digit in exponent");
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9')
                ++pos_;
        }

        const char *begin = src_.data() + start;
        const char *end = src_.data() + pos_;

        if (isFloat) {
            double d = 0.0;
            auto parsed = std::from_chars(begin, end, d);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(d))
                error("invalid number");
            return JsonValue(d);
        } else {
            int64_t value = 0;
            auto parsed = std::from_chars(begin, end, value, 10);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
                error("invalid number");
            return JsonValue(value);
        }
    }

    /// @brief Parse a JSON array `[ value, ... ]` at the given nesting depth.
    JsonValue parseArray(int depth) {
        expect('[');
        skipWhitespace();
        JsonValue::ArrayType arr;
        if (peek() == ']') {
            ++pos_;
            return JsonValue(std::move(arr));
        }
        while (true) {
            arr.push_back(parseValue(depth));
            skipWhitespace();
            if (peek() == ']') {
                ++pos_;
                return JsonValue(std::move(arr));
            }
            expect(',');
        }
    }

    /// @brief Parse a JSON object `{ "key": value, ... }`, preserving member order.
    JsonValue parseObject(int depth) {
        expect('{');
        skipWhitespace();
        JsonValue::ObjectType obj;
        std::unordered_set<std::string> seenKeys;
        if (peek() == '}') {
            ++pos_;
            return JsonValue(std::move(obj));
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"')
                error("expected string key");
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            if (!seenKeys.insert(key).second)
                error("duplicate object key");
            auto val = parseValue(depth);
            obj.emplace_back(std::move(key), std::move(val));
            skipWhitespace();
            if (peek() == '}') {
                ++pos_;
                return JsonValue(std::move(obj));
            }
            expect(',');
        }
    }
};

} // anonymous namespace

JsonValue JsonValue::parse(std::string_view input) {
    JsonParser parser(input);
    return parser.parse();
}

} // namespace viper::server
