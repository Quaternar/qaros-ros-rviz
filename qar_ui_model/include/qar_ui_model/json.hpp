// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__JSON_HPP_
#define QAR_UI_MODEL__JSON_HPP_

/// @file json.hpp
/// @brief RFC 8259 JSON reader/writer over @ref UiValue. No external library.
///
/// This is the wire format between C++ and the browser, so the rules are
/// stated rather than assumed:
///
///   * The reader is STRICT: no trailing commas, comments, single quotes,
///     leading zeros, `NaN`/`Infinity`, unescaped control characters, invalid
///     escapes, lone surrogates or ill-formed UTF-8. Any of those is an error
///     with a line/column and a message. A leading UTF-8 BOM is skipped (RFC
///     8259 section 8.1 allows tolerating it). Duplicate object keys: last one wins,
///     which is what `JSON.parse` does, so both ends agree.
///   * Nesting depth is bounded (default 256) so hostile input cannot overflow
///     the stack of a recursive parser.
///   * Numbers: an integer literal that fits int64 becomes Int; anything with
///     a fraction/exponent, or too large for int64, becomes Double. Overflow to
///     infinity is an error (JSON has no infinity); underflow rounds to 0.
///   * The writer emits Doubles in the layout JavaScript's Number#toString
///     uses, with `.0` appended when the result would look like an integer, so
///     Int/Double survive a C++ round trip and the browser prints the same
///     digits we do. NaN and infinities become `null` (as JSON.stringify does).
///   * `html_safe` additionally escapes `<`, `>`, `&`, U+2028 and U+2029 so the
///     output can be embedded verbatim inside a `<script>` element — a string
///     field containing `</script>` must not be able to end the element.
///   * Strings are validated as UTF-8 on the way out and any ill-formed bytes
///     replaced by U+FFFD: a ROS `string` is not guaranteed to be UTF-8, and a
///     browser closes a WebSocket that delivers a malformed text frame.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "qar_ui_model/ui_value.hpp"

namespace qar_ui_model
{

struct JsonWriteOptions
{
  /// 0 = compact single line; N > 0 = pretty-printed with N-space indentation.
  int indent = 0;
  /// Escape `<`, `>`, `&`, U+2028, U+2029 for embedding inside HTML `<script>`.
  bool html_safe = false;
};

std::string json_write(const UiValue & value, const JsonWriteOptions & options = {});
void json_write_to(std::string & out, const UiValue & value, const JsonWriteOptions & options = {});

/// @brief Append @p text as a quoted, escaped JSON string literal.
void json_escape_string_to(std::string & out, std::string_view text, bool html_safe = false);

/// @brief The writer's number formatting, exposed so the renderer prints
/// values with exactly the digits the wire carries.
std::string json_format_double(double value);
std::string json_format_int(std::int64_t value);

struct JsonParseError
{
  std::size_t offset = 0;  ///< Byte offset into the input.
  std::size_t line = 1;    ///< 1-based.
  std::size_t column = 1;  ///< 1-based, in bytes.
  std::string message;

  /// "message (line L, column C)"
  std::string to_string() const;
};

struct JsonParseOptions
{
  std::size_t max_depth = 256;
};

/// @brief Parse one JSON text (any value type at the top level, per RFC 8259).
/// @param[out] out_value assigned only on success.
/// @param[out] out_error filled only on failure.
bool json_parse(
  std::string_view text,
  UiValue & out_value,
  JsonParseError & out_error,
  const JsonParseOptions & options = {});

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__JSON_HPP_
