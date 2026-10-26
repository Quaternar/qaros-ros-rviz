// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_web_panels/panel_token.hpp"

#include <QRandomGenerator>

#include <cstddef>

namespace qar_web_panels
{

std::string
generate_panel_token()
{
  // system(): the OS entropy source, never the seedable global generator.
  quint32 words[4];
  QRandomGenerator::system()->fillRange(words, 4);

  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(32);
  for (const quint32 w : words) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(kHex[(w >> shift) & 0xFu]);
    }
  }
  return out;
}

bool
is_well_formed_token(std::string_view token)
{
  if (token.size() != 32) {
    return false;
  }
  for (const char c : token) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool
tokens_equal(std::string_view a, std::string_view b)
{
  if (a.size() != b.size()) {
    return false;
  }
  unsigned diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<unsigned>(
      static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
  }
  return diff == 0;
}

}  // namespace qar_web_panels
