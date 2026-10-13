// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/unicode.hpp"

#include <cstdint>

namespace qar_ui_model::utf8
{

bool
is_scalar_value(char32_t code_point)
{
  return code_point <= kMaxCodePoint && !(code_point >= 0xD800 && code_point <= 0xDFFF);
}

bool
encode(char32_t code_point, std::string & out)
{
  const bool valid = is_scalar_value(code_point);
  if (!valid) {
    code_point = kReplacementCharacter;
  }
  if (code_point < 0x80) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
  return valid;
}

bool
decode_next(std::string_view text, std::size_t & pos, char32_t & out_code_point)
{
  if (pos >= text.size()) {
    return false;
  }
  const auto byte = [&text](std::size_t i) {
    return static_cast<std::uint8_t>(text[i]);
  };
  const std::uint8_t lead = byte(pos);

  if (lead < 0x80) {
    out_code_point = lead;
    pos += 1;
    return true;
  }

  // Table 3-7 of the Unicode standard: the valid second-byte range depends on
  // the lead byte, which is what rejects overlongs (E0 80..9F, F0 80..8F),
  // surrogates (ED A0..BF) and > U+10FFFF (F4 90..BF) in one place.
  std::size_t length = 0;
  std::uint8_t second_min = 0x80;
  std::uint8_t second_max = 0xBF;
  char32_t code_point = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    code_point = lead & 0x1F;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    code_point = lead & 0x0F;
    if (lead == 0xE0) {
      second_min = 0xA0;
    } else if (lead == 0xED) {
      second_max = 0x9F;
    }
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    code_point = lead & 0x07;
    if (lead == 0xF0) {
      second_min = 0x90;
    } else if (lead == 0xF4) {
      second_max = 0x8F;
    }
  } else {
    return false;  // C0, C1, F5..FF and stray continuation bytes.
  }

  if (pos + length > text.size()) {
    return false;
  }
  const std::uint8_t second = byte(pos + 1);
  if (second < second_min || second > second_max) {
    return false;
  }
  code_point = (code_point << 6) | (second & 0x3F);
  for (std::size_t i = 2; i < length; ++i) {
    const std::uint8_t continuation = byte(pos + i);
    if (continuation < 0x80 || continuation > 0xBF) {
      return false;
    }
    code_point = (code_point << 6) | (continuation & 0x3F);
  }

  out_code_point = code_point;
  pos += length;
  return true;
}

bool
is_valid(std::string_view text, std::size_t * out_first_invalid_offset)
{
  std::size_t pos = 0;
  char32_t code_point = 0;
  while (pos < text.size()) {
    if (!decode_next(text, pos, code_point)) {
      if (out_first_invalid_offset != nullptr) {
        *out_first_invalid_offset = pos;
      }
      return false;
    }
  }
  return true;
}

std::string
sanitize(std::string_view text)
{
  std::string out;
  out.reserve(text.size());
  std::size_t pos = 0;
  char32_t code_point = 0;
  while (pos < text.size()) {
    const std::size_t start = pos;
    if (decode_next(text, pos, code_point)) {
      out.append(text.data() + start, pos - start);
    } else {
      // Skip the maximal ill-formed subpart: the lead byte plus any continuation
      // bytes that would have been valid for it, so one bad character yields
      // one U+FFFD rather than one per byte.
      encode(kReplacementCharacter, out);
      pos = start + 1;
      const std::uint8_t lead = static_cast<std::uint8_t>(text[start]);
      std::size_t expected = 0;
      if (lead >= 0xC2 && lead <= 0xDF) {
        expected = 1;
      } else if (lead >= 0xE0 && lead <= 0xEF) {
        expected = 2;
      } else if (lead >= 0xF0 && lead <= 0xF4) {
        expected = 3;
      }
      while (expected > 0 && pos < text.size()) {
        const std::uint8_t b = static_cast<std::uint8_t>(text[pos]);
        if (b < 0x80 || b > 0xBF) {
          break;
        }
        ++pos;
        --expected;
      }
    }
  }
  return out;
}

}  // namespace qar_ui_model::utf8
