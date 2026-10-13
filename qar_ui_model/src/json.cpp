// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ui_model/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

#include "qar_ui_model/unicode.hpp"

// MSVC 2019 16.4+ and libstdc++ 11+ ship floating-point <charconv>; older
// toolchains fall back to the C library, with the locale-dependent decimal
// separator normalized by hand (a Qt host may well have called setlocale()).
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define QAR_UI_MODEL_HAS_FLOAT_CHARCONV 1
#else
#define QAR_UI_MODEL_HAS_FLOAT_CHARCONV 0
#endif

namespace qar_ui_model
{

namespace
{

// ---------------------------------------------------------------------------
// Number formatting
// ---------------------------------------------------------------------------

// Scientific representation of |value| with `precision` significant digits,
// as digit string + decimal exponent (value = 0.d1d2..dk * 10^exponent).
void
format_scientific_digits(double magnitude, int precision, std::string & out_digits, int & out_exponent)
{
  char buffer[64];
  std::string text;
#if QAR_UI_MODEL_HAS_FLOAT_CHARCONV
  const auto result = std::to_chars(
    buffer, buffer + sizeof(buffer), magnitude, std::chars_format::scientific, precision - 1);
  text.assign(buffer, result.ptr);
#else
  std::snprintf(buffer, sizeof(buffer), "%.*e", precision - 1, magnitude);
  text = buffer;
#endif
  const std::size_t e = text.find_first_of("eE");
  out_digits.clear();
  for (std::size_t i = 0; i < e && i < text.size(); ++i) {
    if (text[i] >= '0' && text[i] <= '9') {
      out_digits.push_back(text[i]);
    }
    // Anything else is the decimal separator, whatever the locale made it.
  }
  int exponent = 0;
  if (e != std::string::npos) {
    exponent = std::atoi(text.c_str() + e + 1);
  }
  out_exponent = exponent + 1;
}

bool
parse_double_strict(std::string_view text, double & out)
{
#if QAR_UI_MODEL_HAS_FLOAT_CHARCONV
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
#else
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  stream >> out;
  return !stream.fail() && stream.eof();
#endif
}

}  // namespace

std::string
json_format_int(std::int64_t value)
{
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

std::string
json_format_double(double value)
{
  if (!std::isfinite(value)) {
    return "null";
  }
  if (value == 0.0) {
    return std::signbit(value) ? "-0.0" : "0.0";
  }

  const bool negative = value < 0.0;
  const double magnitude = negative ? -value : value;

  // Shortest digit string that round-trips. 17 significant digits always do.
  std::string digits;
  int n = 0;
  for (int precision = 1; precision <= 17; ++precision) {
    format_scientific_digits(magnitude, precision, digits, n);
    std::string candidate = digits.substr(0, 1);
    if (digits.size() > 1) {
      candidate += '.';
      candidate += digits.substr(1);
    }
    candidate += 'e';
    candidate += std::to_string(n - 1);
    double back = 0.0;
    if (parse_double_strict(candidate, back) && back == magnitude) {
      break;
    }
  }
  while (digits.size() > 1 && digits.back() == '0') {
    digits.pop_back();
  }

  // ECMAScript Number::toString layout (ECMA-262 section 6.1.6.1.20): k digits, and
  // value = digits * 10^(n-k).
  const int k = static_cast<int>(digits.size());
  std::string out;
  if (negative) {
    out.push_back('-');
  }
  if (k <= n && n <= 21) {
    out += digits;
    out.append(static_cast<std::size_t>(n - k), '0');
    out += ".0";  // Keep it a Double on the way back in.
  } else if (0 < n && n <= 21) {
    out.append(digits, 0, static_cast<std::size_t>(n));
    out.push_back('.');
    out.append(digits, static_cast<std::size_t>(n), std::string::npos);
  } else if (-6 < n && n <= 0) {
    out += "0.";
    out.append(static_cast<std::size_t>(-n), '0');
    out += digits;
  } else {
    out.push_back(digits[0]);
    if (k > 1) {
      out.push_back('.');
      out.append(digits, 1, std::string::npos);
    }
    out.push_back('e');
    const int exponent = n - 1;
    out.push_back(exponent < 0 ? '-' : '+');
    out += std::to_string(exponent < 0 ? -exponent : exponent);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

void
json_escape_string_to(std::string & out, std::string_view text, bool html_safe)
{
  static const char kHex[] = "0123456789abcdef";
  const auto append_u_escape = [&out](char32_t code_point) {
    out += "\\u";
    out.push_back(kHex[(code_point >> 12) & 0xF]);
    out.push_back(kHex[(code_point >> 8) & 0xF]);
    out.push_back(kHex[(code_point >> 4) & 0xF]);
    out.push_back(kHex[code_point & 0xF]);
  };

  out.push_back('"');
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t start = pos;
    char32_t code_point = 0;
    if (!utf8::decode_next(text, pos, code_point)) {
      // Ill-formed input: substitute and skip the maximal bad subpart exactly
      // as utf8::sanitize would, then keep going.
      const std::string fixed = utf8::sanitize(text.substr(start, 4));
      std::size_t fixed_pos = 0;
      utf8::decode_next(fixed, fixed_pos, code_point);
      // Re-derive how many source bytes that replacement consumed.
      std::size_t consumed = 1;
      const std::uint8_t lead = static_cast<std::uint8_t>(text[start]);
      std::size_t expected = 0;
      if (lead >= 0xC2 && lead <= 0xDF) {
        expected = 1;
      } else if (lead >= 0xE0 && lead <= 0xEF) {
        expected = 2;
      } else if (lead >= 0xF0 && lead <= 0xF4) {
        expected = 3;
      }
      while (expected > 0 && start + consumed < text.size()) {
        const std::uint8_t b = static_cast<std::uint8_t>(text[start + consumed]);
        if (b < 0x80 || b > 0xBF) {
          break;
        }
        ++consumed;
        --expected;
      }
      pos = start + consumed;
      utf8::encode(utf8::kReplacementCharacter, out);
      continue;
    }

    switch (code_point) {
      case '"':
        out += "\\\"";
        continue;
      case '\\':
        out += "\\\\";
        continue;
      case '\b':
        out += "\\b";
        continue;
      case '\f':
        out += "\\f";
        continue;
      case '\n':
        out += "\\n";
        continue;
      case '\r':
        out += "\\r";
        continue;
      case '\t':
        out += "\\t";
        continue;
      default:
        break;
    }
    if (code_point < 0x20) {
      append_u_escape(code_point);
      continue;
    }
    if (html_safe && (code_point == '<' || code_point == '>' || code_point == '&' ||
      code_point == 0x2028 || code_point == 0x2029))
    {
      append_u_escape(code_point);
      continue;
    }
    out.append(text.data() + start, pos - start);
  }
  out.push_back('"');
}

namespace
{

void
write_indent(std::string & out, int indent, int depth)
{
  out.push_back('\n');
  out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
}

void
write_value(std::string & out, const UiValue & value, const JsonWriteOptions & options, int depth)
{
  switch (value.kind()) {
    case UiValueKind::Null:
      out += "null";
      return;
    case UiValueKind::Bool:
      out += value.as_bool() ? "true" : "false";
      return;
    case UiValueKind::Int:
      out += json_format_int(value.as_int());
      return;
    case UiValueKind::Double:
      out += json_format_double(value.as_double());
      return;
    case UiValueKind::String:
      json_escape_string_to(out, value.as_string(), options.html_safe);
      return;
    case UiValueKind::Array: {
      const UiArray & elements = value.elements();
      if (elements.empty()) {
        out += "[]";
        return;
      }
      out.push_back('[');
      for (std::size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) {
          out.push_back(',');
        }
        if (options.indent > 0) {
          write_indent(out, options.indent, depth + 1);
        }
        write_value(out, elements[i], options, depth + 1);
      }
      if (options.indent > 0) {
        write_indent(out, options.indent, depth);
      }
      out.push_back(']');
      return;
    }
    case UiValueKind::Object: {
      const UiObject & entries = value.entries();
      if (entries.empty()) {
        out += "{}";
        return;
      }
      out.push_back('{');
      for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
          out.push_back(',');
        }
        if (options.indent > 0) {
          write_indent(out, options.indent, depth + 1);
        }
        json_escape_string_to(out, entries[i].key, options.html_safe);
        out.push_back(':');
        if (options.indent > 0) {
          out.push_back(' ');
        }
        write_value(out, entries[i].value, options, depth + 1);
      }
      if (options.indent > 0) {
        write_indent(out, options.indent, depth);
      }
      out.push_back('}');
      return;
    }
  }
}

}  // namespace

void
json_write_to(std::string & out, const UiValue & value, const JsonWriteOptions & options)
{
  write_value(out, value, options, 0);
}

std::string
json_write(const UiValue & value, const JsonWriteOptions & options)
{
  std::string out;
  json_write_to(out, value, options);
  return out;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

std::string
JsonParseError::to_string() const
{
  return message + " (line " + std::to_string(line) + ", column " + std::to_string(column) + ")";
}

namespace
{

class Parser
{
public:
  Parser(std::string_view text, const JsonParseOptions & options, JsonParseError & error)
  : text_(text), options_(options), error_(error) {}

  bool parse(UiValue & out)
  {
    // RFC 8259 section 8.1: a BOM may be tolerated; JSON.parse rejects it, so
    // tolerate here (we may receive files) and never emit one.
    if (text_.size() >= 3 && static_cast<std::uint8_t>(text_[0]) == 0xEF &&
      static_cast<std::uint8_t>(text_[1]) == 0xBB && static_cast<std::uint8_t>(text_[2]) == 0xBF)
    {
      pos_ = 3;
    }
    skip_whitespace();
    if (pos_ >= text_.size()) {
      return fail("empty input: expected a JSON value", pos_);
    }
    UiValue value;
    if (!parse_value(value, 0)) {
      return false;
    }
    skip_whitespace();
    if (pos_ < text_.size()) {
      return fail("unexpected trailing characters after JSON value", pos_);
    }
    out = std::move(value);
    return true;
  }

private:
  bool fail(std::string message, std::size_t offset)
  {
    error_.offset = offset;
    error_.line = 1;
    error_.column = 1;
    for (std::size_t i = 0; i < offset && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        ++error_.line;
        error_.column = 1;
      } else {
        ++error_.column;
      }
    }
    error_.message = std::move(message);
    return false;
  }

  static std::string describe_char(char c)
  {
    const auto byte = static_cast<std::uint8_t>(c);
    if (byte >= 0x20 && byte < 0x7F) {
      return std::string("'") + c + "'";
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "byte 0x%02X", byte);
    return buffer;
  }

  void skip_whitespace()
  {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool parse_value(UiValue & out, std::size_t depth)
  {
    if (pos_ >= text_.size()) {
      return fail("unexpected end of input: expected a value", pos_);
    }
    const char c = text_[pos_];
    switch (c) {
      case '{':
        return parse_object(out, depth);
      case '[':
        return parse_array(out, depth);
      case '"': {
        std::string s;
        if (!parse_string(s)) {
          return false;
        }
        out = UiValue(std::move(s));
        return true;
      }
      case 't':
        return parse_literal("true", UiValue(true), out);
      case 'f':
        return parse_literal("false", UiValue(false), out);
      case 'n':
        return parse_literal("null", UiValue(nullptr), out);
      default:
        break;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return parse_number(out);
    }
    return fail("unexpected character " + describe_char(c) + ": expected a value", pos_);
  }

  bool parse_literal(const char * literal, UiValue value, UiValue & out)
  {
    const std::size_t length = std::strlen(literal);
    if (text_.compare(pos_, length, literal) != 0) {
      return fail(std::string("invalid literal: expected '") + literal + "'", pos_);
    }
    pos_ += length;
    out = std::move(value);
    return true;
  }

  bool enter(std::size_t depth)
  {
    if (depth >= options_.max_depth) {
      return fail(
        "nesting depth exceeds the limit of " + std::to_string(options_.max_depth), pos_);
    }
    return true;
  }

  bool parse_object(UiValue & out, std::size_t depth)
  {
    if (!enter(depth)) {
      return false;
    }
    ++pos_;  // '{'
    UiValue object = UiValue::object();
    skip_whitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      out = std::move(object);
      return true;
    }
    while (true) {
      skip_whitespace();
      if (pos_ >= text_.size()) {
        return fail("unexpected end of input inside object", pos_);
      }
      if (text_[pos_] != '"') {
        if (text_[pos_] == '}') {
          return fail("trailing comma is not allowed in object", pos_);
        }
        return fail("object key must be a string", pos_);
      }
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_whitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return fail("expected ':' after object key", pos_);
      }
      ++pos_;
      skip_whitespace();
      UiValue value;
      if (!parse_value(value, depth + 1)) {
        return false;
      }
      object.set(std::move(key), std::move(value));  // duplicate key: last wins
      skip_whitespace();
      if (pos_ >= text_.size()) {
        return fail("unexpected end of input inside object", pos_);
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        out = std::move(object);
        return true;
      }
      return fail("expected ',' or '}' in object", pos_);
    }
  }

  bool parse_array(UiValue & out, std::size_t depth)
  {
    if (!enter(depth)) {
      return false;
    }
    ++pos_;  // '['
    UiValue array = UiValue::array();
    skip_whitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      out = std::move(array);
      return true;
    }
    while (true) {
      skip_whitespace();
      if (pos_ < text_.size() && text_[pos_] == ']') {
        return fail("trailing comma is not allowed in array", pos_);
      }
      UiValue element;
      if (!parse_value(element, depth + 1)) {
        return false;
      }
      array.push_back(std::move(element));
      skip_whitespace();
      if (pos_ >= text_.size()) {
        return fail("unexpected end of input inside array", pos_);
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        out = std::move(array);
        return true;
      }
      return fail("expected ',' or ']' in array", pos_);
    }
  }

  bool parse_hex4(std::size_t at, char32_t & out) const
  {
    if (at + 4 > text_.size()) {
      return false;
    }
    char32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const char c = text_[at + i];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<char32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<char32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<char32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    out = value;
    return true;
  }

  bool parse_string(std::string & out)
  {
    const std::size_t start = pos_;
    ++pos_;  // opening quote
    out.clear();
    while (true) {
      if (pos_ >= text_.size()) {
        return fail("unterminated string", start);
      }
      const auto byte = static_cast<std::uint8_t>(text_[pos_]);
      if (byte == '"') {
        ++pos_;
        return true;
      }
      if (byte == '\\') {
        if (pos_ + 1 >= text_.size()) {
          return fail("unterminated string", start);
        }
        const char e = text_[pos_ + 1];
        switch (e) {
          case '"':
            out.push_back('"');
            pos_ += 2;
            continue;
          case '\\':
            out.push_back('\\');
            pos_ += 2;
            continue;
          case '/':
            out.push_back('/');
            pos_ += 2;
            continue;
          case 'b':
            out.push_back('\b');
            pos_ += 2;
            continue;
          case 'f':
            out.push_back('\f');
            pos_ += 2;
            continue;
          case 'n':
            out.push_back('\n');
            pos_ += 2;
            continue;
          case 'r':
            out.push_back('\r');
            pos_ += 2;
            continue;
          case 't':
            out.push_back('\t');
            pos_ += 2;
            continue;
          case 'u': {
            char32_t code_point = 0;
            if (!parse_hex4(pos_ + 2, code_point)) {
              return fail("invalid \\u escape: expected four hex digits", pos_);
            }
            std::size_t consumed = 6;
            if (code_point >= 0xD800 && code_point <= 0xDBFF) {
              // High surrogate: a low surrogate escape must follow.
              char32_t low = 0;
              if (pos_ + 6 + 1 < text_.size() && text_[pos_ + 6] == '\\' &&
                text_[pos_ + 7] == 'u' && parse_hex4(pos_ + 8, low) &&
                low >= 0xDC00 && low <= 0xDFFF)
              {
                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                consumed = 12;
              } else {
                return fail("lone high surrogate in \\u escape", pos_);
              }
            } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
              return fail("lone low surrogate in \\u escape", pos_);
            }
            utf8::encode(code_point, out);
            pos_ += consumed;
            continue;
          }
          default:
            return fail("invalid escape sequence '\\" + std::string(1, e) + "'", pos_);
        }
      }
      if (byte < 0x20) {
        char buffer[64];
        std::snprintf(
          buffer, sizeof(buffer), "control character U+%04X in string must be escaped", byte);
        return fail(buffer, pos_);
      }
      if (byte < 0x80) {
        out.push_back(static_cast<char>(byte));
        ++pos_;
        continue;
      }
      const std::size_t seq_start = pos_;
      char32_t code_point = 0;
      if (!utf8::decode_next(text_, pos_, code_point)) {
        return fail("ill-formed UTF-8 byte sequence in string", seq_start);
      }
      out.append(text_.data() + seq_start, pos_ - seq_start);
    }
  }

  bool parse_number(UiValue & out)
  {
    const std::size_t start = pos_;
    std::size_t p = pos_;
    if (text_[p] == '-') {
      ++p;
    }
    if (p >= text_.size() || text_[p] < '0' || text_[p] > '9') {
      return fail("invalid number: expected a digit", p);
    }
    const std::size_t int_start = p;
    if (text_[p] == '0') {
      ++p;
      if (p < text_.size() && text_[p] >= '0' && text_[p] <= '9') {
        return fail("invalid number: leading zeros are not allowed", p);
      }
    } else {
      while (p < text_.size() && text_[p] >= '0' && text_[p] <= '9') {
        ++p;
      }
    }
    const std::size_t int_end = p;
    bool is_integer = true;
    std::size_t frac_start = 0;
    std::size_t frac_end = 0;
    if (p < text_.size() && text_[p] == '.') {
      is_integer = false;
      ++p;
      frac_start = p;
      if (p >= text_.size() || text_[p] < '0' || text_[p] > '9') {
        return fail("invalid number: expected a digit after '.'", p);
      }
      while (p < text_.size() && text_[p] >= '0' && text_[p] <= '9') {
        ++p;
      }
      frac_end = p;
    }
    long exponent = 0;
    if (p < text_.size() && (text_[p] == 'e' || text_[p] == 'E')) {
      is_integer = false;
      ++p;
      bool exponent_negative = false;
      if (p < text_.size() && (text_[p] == '+' || text_[p] == '-')) {
        exponent_negative = text_[p] == '-';
        ++p;
      }
      if (p >= text_.size() || text_[p] < '0' || text_[p] > '9') {
        return fail("invalid number: expected a digit in exponent", p);
      }
      while (p < text_.size() && text_[p] >= '0' && text_[p] <= '9') {
        if (exponent < 100000) {  // clamp: only the sign matters beyond this
          exponent = exponent * 10 + (text_[p] - '0');
        }
        ++p;
      }
      if (exponent_negative) {
        exponent = -exponent;
      }
    }
    const std::string_view literal = text_.substr(start, p - start);

    if (is_integer) {
      std::int64_t value = 0;
      const auto result = std::from_chars(literal.data(), literal.data() + literal.size(), value);
      if (result.ec == std::errc() && result.ptr == literal.data() + literal.size()) {
        out = UiValue(value);
        pos_ = p;
        return true;
      }
      // Too large for int64: carry it as a double, as JavaScript would.
    }

    double value = 0.0;
    if (!parse_double_strict(literal, value)) {
      // Distinguish overflow (an error: JSON has no infinity) from underflow
      // (rounds to zero, as every JSON implementation does) by the decimal
      // magnitude of the literal.
      std::size_t significant = int_start;
      while (significant < int_end && text_[significant] == '0') {
        ++significant;
      }
      long decimal_exponent = 0;
      if (significant < int_end) {
        decimal_exponent = static_cast<long>(int_end - significant) - 1 + exponent;
      } else {
        std::size_t zeros = 0;
        for (std::size_t i = frac_start; i < frac_end && text_[i] == '0'; ++i) {
          ++zeros;
        }
        decimal_exponent = -static_cast<long>(zeros) - 1 + exponent;
      }
      if (decimal_exponent < 0) {
        out = UiValue(text_[start] == '-' ? -0.0 : 0.0);
        pos_ = p;
        return true;
      }
      return fail("number out of range", start);
    }
    if (!std::isfinite(value)) {
      return fail("number out of range", start);
    }
    out = UiValue(value);
    pos_ = p;
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  const JsonParseOptions & options_;
  JsonParseError & error_;
};

}  // namespace

bool
json_parse(
  std::string_view text,
  UiValue & out_value,
  JsonParseError & out_error,
  const JsonParseOptions & options)
{
  Parser parser(text, options, out_error);
  return parser.parse(out_value);
}

}  // namespace qar_ui_model
