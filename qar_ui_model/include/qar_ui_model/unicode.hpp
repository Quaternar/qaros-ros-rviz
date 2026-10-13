// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__UNICODE_HPP_
#define QAR_UI_MODEL__UNICODE_HPP_

/// @file unicode.hpp
/// @brief Minimal, strict UTF-8 helpers shared by the JSON codec and the HTML
/// escaper, and useful to qar_ros_ui for `wstring` fields.
///
/// Strictness is the point: a WebSocket text frame that is not well-formed
/// UTF-8 makes a browser close the connection (RFC 6455 section 8.1, close code 1007),
/// and a ROS `string` field carries whatever bytes a publisher put in it. So
/// every string that leaves this library for the page is validated, and
/// anything malformed is replaced with U+FFFD rather than forwarded.

#include <cstddef>
#include <string>
#include <string_view>

namespace qar_ui_model::utf8
{

constexpr char32_t kReplacementCharacter = 0xFFFD;
constexpr char32_t kMaxCodePoint = 0x10FFFF;

/// @brief True for a Unicode scalar value: <= U+10FFFF and not a surrogate.
bool is_scalar_value(char32_t code_point);

/// @brief Append the UTF-8 encoding of @p code_point.
/// @return false (and appends U+FFFD instead) if it is not a scalar value.
bool encode(char32_t code_point, std::string & out);

/// @brief Decode the scalar value starting at @p text[pos].
///
/// On success advances @p pos past the sequence. On a truncated, overlong,
/// surrogate or out-of-range sequence returns false and leaves @p pos on the
/// first offending byte so the caller can report or replace it.
bool decode_next(std::string_view text, std::size_t & pos, char32_t & out_code_point);

/// @brief True if the whole text is well-formed UTF-8.
/// @param[out] out_first_invalid_offset byte offset of the first bad sequence, if any.
bool is_valid(std::string_view text, std::size_t * out_first_invalid_offset = nullptr);

/// @brief Copy of @p text with every maximal ill-formed subsequence replaced by
/// U+FFFD (the "substitution of maximal subparts" practice of the Unicode
/// standard, which is what browsers do too).
std::string sanitize(std::string_view text);

}  // namespace qar_ui_model::utf8

#endif  // QAR_UI_MODEL__UNICODE_HPP_
