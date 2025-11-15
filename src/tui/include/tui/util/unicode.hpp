// tui/include/tui/util/unicode.hpp
// @brief Unicode helper utilities for width and decoding.
// @invariant char_width follows basic East Asian width rules.
// @ownership decode_utf8 returns a new string; caller owns the result.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace viper::tui::util
{
/// @brief Determine display width of a Unicode code point.
/// @param cp Unicode scalar value.
/// @return 0 for combining marks, 2 for wide CJK, otherwise 1.
[[nodiscard]] int char_width(char32_t cp);

/// @brief Decode a UTF-8 string into UTF-32 code points.
/// @param in UTF-8 encoded byte sequence.
/// @return Decoded UTF-32 string; invalid sequences yield U+FFFD.
[[nodiscard]] std::u32string decode_utf8(std::string_view in);

} // namespace viper::tui::util
