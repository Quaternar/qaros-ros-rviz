// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Two things stand between a publisher's bytes and the AR page: strict UTF-8
// handling (a malformed WebSocket text frame closes the connection) and HTML
// escaping (a topic name or string field must never become markup).

#include <gtest/gtest.h>

#include <string>

#include "qar_ui_model/html_escape.hpp"
#include "qar_ui_model/unicode.hpp"

using qar_ui_model::html_escape;
namespace utf8 = qar_ui_model::utf8;

TEST(Utf8, ScalarValues)
{
  EXPECT_TRUE(utf8::is_scalar_value(0));
  EXPECT_TRUE(utf8::is_scalar_value(0x7F));
  EXPECT_TRUE(utf8::is_scalar_value(0xD7FF));
  EXPECT_FALSE(utf8::is_scalar_value(0xD800));
  EXPECT_FALSE(utf8::is_scalar_value(0xDFFF));
  EXPECT_TRUE(utf8::is_scalar_value(0xE000));
  EXPECT_TRUE(utf8::is_scalar_value(0x10FFFF));
  EXPECT_FALSE(utf8::is_scalar_value(0x110000));
}

TEST(Utf8, EncodeEveryLength)
{
  std::string out;
  EXPECT_TRUE(utf8::encode('A', out));
  EXPECT_TRUE(utf8::encode(0xE9, out));
  EXPECT_TRUE(utf8::encode(0x20AC, out));
  EXPECT_TRUE(utf8::encode(0x1F600, out));
  EXPECT_EQ(out, "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");

  std::string bad;
  EXPECT_FALSE(utf8::encode(0xD800, bad));
  EXPECT_EQ(bad, "\xEF\xBF\xBD");
}

TEST(Utf8, DecodeRoundTripsAndAdvances)
{
  const std::string text = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
  std::size_t pos = 0;
  char32_t cp = 0;
  ASSERT_TRUE(utf8::decode_next(text, pos, cp));
  EXPECT_EQ(cp, U'A');
  EXPECT_EQ(pos, 1u);
  ASSERT_TRUE(utf8::decode_next(text, pos, cp));
  EXPECT_EQ(cp, 0xE9u);
  EXPECT_EQ(pos, 3u);
  ASSERT_TRUE(utf8::decode_next(text, pos, cp));
  EXPECT_EQ(cp, 0x20ACu);
  EXPECT_EQ(pos, 6u);
  ASSERT_TRUE(utf8::decode_next(text, pos, cp));
  EXPECT_EQ(cp, 0x1F600u);
  EXPECT_EQ(pos, 10u);
  EXPECT_FALSE(utf8::decode_next(text, pos, cp));  // end of input
}

TEST(Utf8, RejectsOverlongSurrogateOutOfRangeAndTruncated)
{
  std::size_t offset = 0;
  EXPECT_TRUE(utf8::is_valid("plain ascii"));
  EXPECT_TRUE(utf8::is_valid("\xC3\xA9"));
  EXPECT_FALSE(utf8::is_valid("\xC0\xAF", &offset));  // overlong '/'
  EXPECT_EQ(offset, 0u);
  EXPECT_FALSE(utf8::is_valid("\xE0\x80\x80"));       // overlong NUL
  EXPECT_FALSE(utf8::is_valid("\xED\xA0\x80"));       // surrogate
  EXPECT_FALSE(utf8::is_valid("\xF4\x90\x80\x80"));   // > U+10FFFF
  EXPECT_FALSE(utf8::is_valid("\xF5\x80\x80\x80"));   // invalid lead
  EXPECT_FALSE(utf8::is_valid("\x80"));               // stray continuation
  EXPECT_FALSE(utf8::is_valid("ok\xE2\x82", &offset)); // truncated at the end
  EXPECT_EQ(offset, 2u);
}

TEST(Utf8, SanitizeReplacesMaximalBadSubsequenceWithOneReplacementCharacter)
{
  EXPECT_EQ(utf8::sanitize("fine"), "fine");
  EXPECT_EQ(utf8::sanitize("a\xFF" "b"), "a\xEF\xBF\xBD" "b");
  // A truncated 3-byte sequence followed by ASCII: one U+FFFD, then the ASCII.
  EXPECT_EQ(utf8::sanitize("\xE2\x82(" ), "\xEF\xBF\xBD(");
  // Two stray continuation bytes: two replacements.
  EXPECT_EQ(utf8::sanitize("\x80\x80"), "\xEF\xBF\xBD\xEF\xBF\xBD");
  // Valid multi-byte text is untouched.
  EXPECT_EQ(utf8::sanitize("\xF0\x9F\x98\x80"), "\xF0\x9F\x98\x80");
}

TEST(HtmlEscape, EscapesTheFiveSignificantCharacters)
{
  EXPECT_EQ(html_escape("a & b"), "a &amp; b");
  EXPECT_EQ(html_escape("<script>alert(1)</script>"),
    "&lt;script&gt;alert(1)&lt;/script&gt;");
  EXPECT_EQ(html_escape("\"quoted\" and 'single'"), "&quot;quoted&quot; and &#39;single&#39;");
  EXPECT_EQ(html_escape("plain text 123"), "plain text 123");
}

TEST(HtmlEscape, AttributeBreakoutAttemptsAreNeutralized)
{
  // A topic name crafted to close an attribute and start a handler.
  const std::string hostile = "/cmd\" onmouseover=\"alert(1)";
  const std::string out = html_escape(hostile);
  EXPECT_EQ(out.find('"'), std::string::npos);
  EXPECT_EQ(out, "/cmd&quot; onmouseover=&quot;alert(1)");
}

TEST(HtmlEscape, ControlCharactersAndInvalidUtf8BecomeReplacementCharacters)
{
  EXPECT_EQ(html_escape(std::string("a\0b", 3)), "a\xEF\xBF\xBD" "b");
  EXPECT_EQ(html_escape("a\x01\x1b\x7f" "b"), "a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "b");
  EXPECT_EQ(html_escape("tab\tnl\ncr\r"), "tab\tnl\ncr\r");  // whitespace controls survive
  EXPECT_EQ(html_escape("\xFF<"), "\xEF\xBF\xBD&lt;");
}

TEST(HtmlEscape, UnicodeTextPassesThrough)
{
  const std::string text = "r\xC3\xB6" "bot \xE2\x80\x94 \xF0\x9F\xA4\x96";
  EXPECT_EQ(html_escape(text), text);
}
