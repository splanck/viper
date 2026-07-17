//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/zanna/DebugExpr.hpp
// Purpose: Side-effect-free expression evaluator used by the debug adapter to
//          decide conditional breakpoints and interpolate logpoint messages,
//          evaluating against the current stop's local-variable snapshot.
// Key invariants:
//   - Pure: no I/O, no mutation of debuggee state; depends only on the resolver.
//   - Decoupled from VM types: locals are supplied via a Resolver callback that
//     yields (value, type) strings, so this header is unit-testable in isolation.
//   - Grammar: literals (int/float/bool/string), identifiers, unary -/not/!,
//     * / %, + -, comparisons, and/or, parentheses. Numeric int->float promotion.
// Ownership/Lifetime:
//   - Stateless across calls; an Eval instance owns only its source + cursor.
// Links: src/tools/zanna/DebugAdapter.cpp,
//        docs/adr/0012-debug-conditional-breakpoints-logpoints.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace zanna::dbgexpr {

/// @brief A tagged evaluation result. Err propagates so callers can fail safe.
struct Value {
    enum class K { Int, Flt, Bool, Str, Err };
    K k = K::Err;
    int64_t i = 0;
    double f = 0.0;
    bool b = false;
    std::string s;

    static Value mkInt(int64_t v) { Value r; r.k = K::Int; r.i = v; return r; }
    static Value mkFlt(double v) { Value r; r.k = K::Flt; r.f = v; return r; }
    static Value mkBool(bool v) { Value r; r.k = K::Bool; r.b = v; return r; }
    static Value mkStr(std::string v) { Value r; r.k = K::Str; r.s = std::move(v); return r; }
    static Value err() { return Value{}; }

    [[nodiscard]] bool isErr() const { return k == K::Err; }
    [[nodiscard]] bool isNum() const { return k == K::Int || k == K::Flt; }
    [[nodiscard]] double num() const { return k == K::Flt ? f : static_cast<double>(i); }
    [[nodiscard]] bool truthy() const {
        switch (k) {
            case K::Int: return i != 0;
            case K::Flt: return f != 0.0;
            case K::Bool: return b;
            case K::Str: return !s.empty();
            default: return false;
        }
    }
    /// @brief Render for logpoint interpolation.
    [[nodiscard]] std::string str() const {
        switch (k) {
            case K::Int: return std::to_string(i);
            case K::Flt: return std::to_string(f);
            case K::Bool: return b ? "true" : "false";
            case K::Str: return s;
            default: return "<err>";
        }
    }
};

/// @brief Resolve identifier @p name to its (value, type) strings from the stop's
///        locals. Returns false when the name is not in scope.
using Resolver = std::function<bool(const std::string &name, std::string &value, std::string &type)>;

/// @brief Recursive-descent evaluator over a single expression string.
class Eval {
  public:
    Eval(std::string src, Resolver resolver) : s_(std::move(src)), r_(std::move(resolver)) {}

    /// @brief Evaluate the whole expression; trailing junk yields Err.
    Value run() {
        pos_ = 0;
        Value v = parseOr();
        skip();
        if (pos_ != s_.size())
            return Value::err();
        return v;
    }

  private:
    std::string s_;
    Resolver r_;
    size_t pos_ = 0;

    void skip() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_])))
            ++pos_;
    }
    [[nodiscard]] char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    [[nodiscard]] char peek2() const { return pos_ + 1 < s_.size() ? s_[pos_ + 1] : '\0'; }

    /// @brief Consume @p op (a 1- or 2-char operator) after skipping whitespace.
    bool eatOp(const char *op) {
        skip();
        const size_t n = op[1] ? 2 : 1;
        if (s_.compare(pos_, n, op) != 0)
            return false;
        // Disambiguate < from <=, ! from !=, = from ==, & from &&, | from ||.
        if (n == 1) {
            const char c = op[0];
            const char nx = peek2();
            if ((c == '<' || c == '>' || c == '!' || c == '=') && nx == '=')
                return false;
            if (c == '&' && nx == '&')
                return false;
            if (c == '|' && nx == '|')
                return false;
        }
        pos_ += n;
        return true;
    }

    static bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

    /// @brief Consume keyword @p kw on an identifier boundary (and/or/not/true/false).
    bool eatKeyword(const char *kw) {
        skip();
        size_t i = 0;
        while (kw[i] && pos_ + i < s_.size() && s_[pos_ + i] == kw[i])
            ++i;
        if (kw[i] != '\0')
            return false;
        if (pos_ + i < s_.size() && isIdent(s_[pos_ + i]))
            return false; // e.g. "android" must not match "and"
        pos_ += i;
        return true;
    }

    Value parseOr() {
        Value lhs = parseAnd();
        for (;;) {
            if (eatKeyword("or") || eatOp("||")) {
                Value rhs = parseAnd();
                lhs = Value::mkBool(lhs.truthy() || rhs.truthy());
            } else {
                return lhs;
            }
        }
    }

    Value parseAnd() {
        Value lhs = parseCmp();
        for (;;) {
            if (eatKeyword("and") || eatOp("&&")) {
                Value rhs = parseCmp();
                lhs = Value::mkBool(lhs.truthy() && rhs.truthy());
            } else {
                return lhs;
            }
        }
    }

    Value parseCmp() {
        Value lhs = parseAdd();
        // At most one comparison (non-associative), matching common usage.
        const char *ops[] = {"==", "!=", "<=", ">=", "<", ">"};
        for (const char *op : ops) {
            if (eatOp(op)) {
                Value rhs = parseAdd();
                return compare(op, lhs, rhs);
            }
        }
        return lhs;
    }

    static Value compare(const std::string &op, const Value &a, const Value &b) {
        int cmp = 0; // -1,0,1
        if (a.isNum() && b.isNum()) {
            double x = a.num(), y = b.num();
            cmp = x < y ? -1 : (x > y ? 1 : 0);
        } else if (a.k == Value::K::Str && b.k == Value::K::Str) {
            cmp = a.s < b.s ? -1 : (a.s > b.s ? 1 : 0);
        } else if (a.k == Value::K::Bool && b.k == Value::K::Bool) {
            cmp = (a.b ? 1 : 0) - (b.b ? 1 : 0);
        } else {
            return Value::err(); // incomparable types
        }
        if (op == "==") return Value::mkBool(cmp == 0);
        if (op == "!=") return Value::mkBool(cmp != 0);
        if (op == "<") return Value::mkBool(cmp < 0);
        if (op == ">") return Value::mkBool(cmp > 0);
        if (op == "<=") return Value::mkBool(cmp <= 0);
        return Value::mkBool(cmp >= 0); // ">="
    }

    Value parseAdd() {
        Value lhs = parseMul();
        for (;;) {
            if (eatOp("+")) {
                Value rhs = parseMul();
                lhs = arith('+', lhs, rhs);
            } else if (eatOp("-")) {
                Value rhs = parseMul();
                lhs = arith('-', lhs, rhs);
            } else {
                return lhs;
            }
        }
    }

    Value parseMul() {
        Value lhs = parseUnary();
        for (;;) {
            if (eatOp("*")) {
                lhs = arith('*', lhs, parseUnary());
            } else if (eatOp("/")) {
                lhs = arith('/', lhs, parseUnary());
            } else if (eatOp("%")) {
                lhs = arith('%', lhs, parseUnary());
            } else {
                return lhs;
            }
        }
    }

    static Value arith(char op, const Value &a, const Value &b) {
        // String concatenation with '+'.
        if (op == '+' && a.k == Value::K::Str && b.k == Value::K::Str)
            return Value::mkStr(a.s + b.s);
        if (!a.isNum() || !b.isNum())
            return Value::err();
        if (a.k == Value::K::Int && b.k == Value::K::Int) {
            int64_t x = a.i, y = b.i;
            switch (op) {
                case '+': return Value::mkInt(x + y);
                case '-': return Value::mkInt(x - y);
                case '*': return Value::mkInt(x * y);
                case '/': return y != 0 ? Value::mkInt(x / y) : Value::err();
                case '%': return y != 0 ? Value::mkInt(x % y) : Value::err();
            }
        }
        double x = a.num(), y = b.num();
        switch (op) {
            case '+': return Value::mkFlt(x + y);
            case '-': return Value::mkFlt(x - y);
            case '*': return Value::mkFlt(x * y);
            case '/': return y != 0.0 ? Value::mkFlt(x / y) : Value::err();
            default: return Value::err(); // % on floats unsupported
        }
    }

    Value parseUnary() {
        if (eatOp("-")) {
            Value v = parseUnary();
            if (v.k == Value::K::Int) return Value::mkInt(-v.i);
            if (v.k == Value::K::Flt) return Value::mkFlt(-v.f);
            return Value::err();
        }
        if (eatKeyword("not") || eatOp("!")) {
            Value v = parseUnary();
            return Value::mkBool(!v.truthy());
        }
        return parsePrimary();
    }

    Value parsePrimary() {
        skip();
        const char c = peek();
        if (c == '(') {
            ++pos_;
            Value v = parseOr();
            skip();
            if (peek() == ')') { ++pos_; return v; }
            return Value::err();
        }
        if (c == '"')
            return parseString();
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek2()))))
            return parseNumber();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return parseIdentOrKeyword();
        return Value::err();
    }

    Value parseString() {
        ++pos_; // opening quote
        std::string out;
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char ch = s_[pos_++];
            if (ch == '\\' && pos_ < s_.size())
                ch = s_[pos_++];
            out.push_back(ch);
        }
        if (pos_ >= s_.size())
            return Value::err(); // unterminated
        ++pos_; // closing quote
        return Value::mkStr(std::move(out));
    }

    Value parseNumber() {
        const size_t start = pos_;
        bool isFloat = false;
        while (pos_ < s_.size()) {
            const char ch = s_[pos_];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                ++pos_;
            } else if (ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') {
                // '+'/'-' only count as part of a number right after e/E.
                if ((ch == '+' || ch == '-') && !(pos_ > start && (s_[pos_ - 1] == 'e' || s_[pos_ - 1] == 'E')))
                    break;
                if (ch == '.' || ch == 'e' || ch == 'E')
                    isFloat = true;
                ++pos_;
            } else {
                break;
            }
        }
        const std::string tok = s_.substr(start, pos_ - start);
        try {
            if (isFloat)
                return Value::mkFlt(std::stod(tok));
            return Value::mkInt(static_cast<int64_t>(std::stoll(tok)));
        } catch (...) {
            return Value::err();
        }
    }

    Value parseIdentOrKeyword() {
        const size_t start = pos_;
        while (pos_ < s_.size() && isIdent(s_[pos_]))
            ++pos_;
        const std::string id = s_.substr(start, pos_ - start);
        if (id == "true") return Value::mkBool(true);
        if (id == "false") return Value::mkBool(false);
        // Resolve from locals.
        std::string value, type;
        if (!r_ || !r_(id, value, type))
            return Value::err();
        return fromLocal(value, type);
    }

    /// @brief Build a Value from a local's (value, type) string pair.
    static Value fromLocal(const std::string &value, const std::string &type) {
        if (type == "i1")
            return Value::mkBool(value == "1" || value == "true");
        if (!type.empty() && type[0] == 'i') {
            try { return Value::mkInt(static_cast<int64_t>(std::stoll(value))); }
            catch (...) { return Value::mkStr(value); }
        }
        if (!type.empty() && type[0] == 'f') {
            try { return Value::mkFlt(std::stod(value)); }
            catch (...) { return Value::mkStr(value); }
        }
        if (type == "str")
            return Value::mkStr(value);
        // Unknown type: best-effort int, then float, then string.
        try { size_t n; int64_t iv = std::stoll(value, &n); if (n == value.size()) return Value::mkInt(iv); }
        catch (...) {}
        try { size_t n; double dv = std::stod(value, &n); if (n == value.size()) return Value::mkFlt(dv); }
        catch (...) {}
        return Value::mkStr(value);
    }
};

/// @brief Evaluate @p expr against @p resolve; true unless it cleanly yields a
///        falsey value. Fail-safe: parse/type errors count as true so a malformed
///        condition still halts rather than silently skipping a breakpoint.
inline bool conditionHolds(const std::string &expr, const Resolver &resolve) {
    if (expr.empty())
        return true;
    Value v = Eval(expr, resolve).run();
    if (v.isErr())
        return true;
    return v.truthy();
}

/// @brief Interpolate `{expr}` segments of a logpoint message; non-brace text is
///        copied verbatim, an errored segment renders as "<err>". `{{`/`}}` escape.
inline std::string interpolate(const std::string &msg, const Resolver &resolve) {
    std::string out;
    for (size_t i = 0; i < msg.size();) {
        const char c = msg[i];
        if (c == '{' && i + 1 < msg.size() && msg[i + 1] == '{') { out.push_back('{'); i += 2; continue; }
        if (c == '}' && i + 1 < msg.size() && msg[i + 1] == '}') { out.push_back('}'); i += 2; continue; }
        if (c == '{') {
            const size_t end = msg.find('}', i + 1);
            if (end == std::string::npos) { out.append(msg, i, std::string::npos); break; }
            const std::string expr = msg.substr(i + 1, end - i - 1);
            Value v = Eval(expr, resolve).run();
            out += v.isErr() ? std::string("<err>") : v.str();
            i = end + 1;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

} // namespace zanna::dbgexpr
