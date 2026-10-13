// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/html_escape.hpp"

#include <cstdint>

#include "qar_ui_model/unicode.hpp"

namespace qar_ui_model
{

void
html_escape_to(std::string & out, std::string_view text)
{
  // Normalize first so the loop below only ever sees well-formed sequences.
  const std::string clean = utf8::sanitize(text);
  out.reserve(out.size() + clean.size());
  std::size_t pos = 0;
  while (pos < clean.size()) {
    const std::size_t start = pos;
    char32_t code_point = 0;
    utf8::decode_next(clean, pos, code_point);
    switch (code_point) {
      case '&':
        out += "&amp;";
        continue;
      case '<':
        out += "&lt;";
        continue;
      case '>':
        out += "&gt;";
        continue;
      case '"':
        out += "&quot;";
        continue;
      case '\'':
        out += "&#39;";
        continue;
      case '\t':
      case '\n':
      case '\r':
        out.push_back(static_cast<char>(code_point));
        continue;
      default:
        break;
    }
    if (code_point < 0x20 || code_point == 0x7F) {
      utf8::encode(utf8::kReplacementCharacter, out);
      continue;
    }
    out.append(clean.data() + start, pos - start);
  }
}

std::string
html_escape(std::string_view text)
{
  std::string out;
  html_escape_to(out, text);
  return out;
}

}  // namespace qar_ui_model
