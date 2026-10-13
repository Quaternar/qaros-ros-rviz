// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// JSON is the wire format to the browser, so the codec is held to RFC 8259:
// exact escaping, real Unicode handling, JavaScript-compatible number layout,
// bounded nesting, and a useful error for every malformed input.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "qar_ui_model/json.hpp"
#include "qar_ui_model/ui_value.hpp"

using qar_ui_model::json_format_double;
using qar_ui_model::json_format_int;
using qar_ui_model::json_parse;
using qar_ui_model::json_write;
using qar_ui_model::JsonParseError;
using qar_ui_model::JsonParseOptions;
using qar_ui_model::JsonWriteOptions;
using qar_ui_model::UiValue;

namespace
{

UiValue
parse_ok(const std::string & text)
{
  UiValue value;
  JsonParseError error;
  EXPECT_TRUE(json_parse(text, value, error)) << text << " -> " << error.to_string();
  return value;
}

JsonParseError
parse_fail(const std::string & text)
{
  UiValue value;
  JsonParseError error;
  EXPECT_FALSE(json_parse(text, value, error)) << "accepted: " << text;
  EXPECT_FALSE(error.message.empty());
  return error;
}

}  // namespace

// ---- writer ---------------------------------------------------------------

TEST(JsonWrite, Scalars)
{
  EXPECT_EQ(json_write(UiValue()), "null");
  EXPECT_EQ(json_write(UiValue(true)), "true");
  EXPECT_EQ(json_write(UiValue(false)), "false");
  EXPECT_EQ(json_write(UiValue(42)), "42");
  EXPECT_EQ(json_write(UiValue(-7)), "-7");
  EXPECT_EQ(json_write(UiValue("hi")), "\"hi\"");
}

TEST(JsonWrite, DoublesUseJavaScriptLayoutAndStayDoubles)
{
  EXPECT_EQ(json_format_double(0.1), "0.1");
  EXPECT_EQ(json_format_double(0.5), "0.5");
  EXPECT_EQ(json_format_double(123.456), "123.456");
  EXPECT_EQ(json_format_double(-2.5), "-2.5");
  EXPECT_EQ(json_format_double(5.0), "5.0");
  EXPECT_EQ(json_format_double(0.0), "0.0");
  EXPECT_EQ(json_format_double(-0.0), "-0.0");
  EXPECT_EQ(json_format_double(1e21), "1e+21");
  EXPECT_EQ(json_format_double(1e20), "100000000000000000000.0");
  EXPECT_EQ(json_format_double(1e-7), "1e-7");
  EXPECT_EQ(json_format_double(0.000001), "0.000001");
  EXPECT_EQ(json_format_double(1.5e-10), "1.5e-10");
  EXPECT_EQ(json_format_double(1234567.0), "1234567.0");
  EXPECT_EQ(json_format_double(0.30000000000000004), "0.30000000000000004");
}

TEST(JsonWrite, NonFiniteDoublesBecomeNull)
{
  EXPECT_EQ(json_format_double(std::numeric_limits<double>::quiet_NaN()), "null");
  EXPECT_EQ(json_format_double(std::numeric_limits<double>::infinity()), "null");
  EXPECT_EQ(json_format_double(-std::numeric_limits<double>::infinity()), "null");
}

TEST(JsonWrite, DoublesRoundTripBitExact)
{
  const double samples[] = {
    0.1, 1.0 / 3.0, 2.0 / 3.0, 1e-300, 1e300, 123456789.123456789, 5e-324,
    std::numeric_limits<double>::max(), std::numeric_limits<double>::min(), 3.141592653589793,
    -1e-5, 1e15, 1e16, 1e17, 9007199254740993.0};
  for (double sample : samples) {
    const std::string text = json_format_double(sample);
    UiValue back;
    JsonParseError error;
    ASSERT_TRUE(json_parse(text, back, error)) << text;
    EXPECT_TRUE(back.is_double()) << text;
    EXPECT_EQ(back.as_double(), sample) << text;
  }
}

TEST(JsonWrite, IntegerExtremes)
{
  EXPECT_EQ(json_format_int(std::numeric_limits<std::int64_t>::max()), "9223372036854775807");
  EXPECT_EQ(json_format_int(std::numeric_limits<std::int64_t>::min()), "-9223372036854775808");
  EXPECT_EQ(json_format_int(0), "0");
}

TEST(JsonWrite, EscapesControlCharactersQuotesAndBackslashes)
{
  EXPECT_EQ(json_write(UiValue("a\"b\\c")), "\"a\\\"b\\\\c\"");
  EXPECT_EQ(json_write(UiValue("\b\f\n\r\t")), "\"\\b\\f\\n\\r\\t\"");
  EXPECT_EQ(json_write(UiValue(std::string("\x01\x1f", 2))), "\"\\u0001\\u001f\"");
  EXPECT_EQ(json_write(UiValue(std::string("nul\0byte", 8))), "\"nul\\u0000byte\"");
  // '/' is not escaped, and DEL passes through.
  EXPECT_EQ(json_write(UiValue("a/b\x7f")), "\"a/b\x7f\"");
}

TEST(JsonWrite, UnicodePassesThroughAsUtf8)
{
  // "héllo €" and a supplementary-plane emoji.
  const std::string text = "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80";
  EXPECT_EQ(json_write(UiValue(text)), "\"" + text + "\"");
}

TEST(JsonWrite, IllFormedUtf8IsReplacedNotForwarded)
{
  // A browser closes a WebSocket that receives a malformed text frame.
  const std::string bad = "a\xFF" "b\xC3(";
  const std::string out = json_write(UiValue(bad));
  EXPECT_EQ(out, "\"a\xEF\xBF\xBD" "b\xEF\xBF\xBD(\"");
}

TEST(JsonWrite, HtmlSafeModeEscapesScriptBreakers)
{
  JsonWriteOptions options;
  options.html_safe = true;
  const std::string out = json_write(UiValue("</script><b>&</b>\xE2\x80\xA8"), options);
  EXPECT_EQ(out, "\"\\u003c/script\\u003e\\u003cb\\u003e\\u0026\\u003c/b\\u003e\\u2028\"");
  EXPECT_EQ(out.find('<'), std::string::npos);
  EXPECT_EQ(out.find('>'), std::string::npos);
  EXPECT_EQ(out.find('&'), std::string::npos);
  // Without the flag the characters are emitted as-is.
  EXPECT_EQ(json_write(UiValue("<a&b>")), "\"<a&b>\"");
}

TEST(JsonWrite, ContainersCompactAndOrdered)
{
  UiValue v = UiValue::object()
    .set("z", UiValue::array().push_back(1).push_back(UiValue::object()))
    .set("a", UiValue::array())
    .set("m", UiValue::object().set("k", nullptr));
  EXPECT_EQ(json_write(v), "{\"z\":[1,{}],\"a\":[],\"m\":{\"k\":null}}");
}

TEST(JsonWrite, PrettyPrinting)
{
  UiValue v = UiValue::object().set("a", UiValue::array().push_back(1).push_back(2)).set("b", "x");
  JsonWriteOptions options;
  options.indent = 2;
  EXPECT_EQ(json_write(v, options), "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": \"x\"\n}");
  EXPECT_EQ(json_write(UiValue::object(), options), "{}");
  EXPECT_EQ(json_write(UiValue::array(), options), "[]");
}

// ---- reader ---------------------------------------------------------------

TEST(JsonParse, ScalarsAtTopLevel)
{
  EXPECT_TRUE(parse_ok("null").is_null());
  EXPECT_EQ(parse_ok("true").as_bool(), true);
  EXPECT_EQ(parse_ok("false").as_bool(true), false);
  EXPECT_EQ(parse_ok("  42 ").as_int(), 42);
  EXPECT_EQ(parse_ok("\"s\"").as_string(), "s");
  EXPECT_TRUE(parse_ok("-0").is_int());
  EXPECT_EQ(parse_ok("-0").as_int(), 0);
}

TEST(JsonParse, IntegerVersusDouble)
{
  EXPECT_TRUE(parse_ok("5").is_int());
  EXPECT_TRUE(parse_ok("5.0").is_double());
  EXPECT_TRUE(parse_ok("5e0").is_double());
  EXPECT_TRUE(parse_ok("5E+2").is_double());
  EXPECT_DOUBLE_EQ(parse_ok("5E+2").as_double(), 500.0);
  EXPECT_DOUBLE_EQ(parse_ok("-1.25e-3").as_double(), -0.00125);
  EXPECT_EQ(parse_ok("9223372036854775807").as_int(), std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(parse_ok("-9223372036854775808").as_int(), std::numeric_limits<std::int64_t>::min());
  // One past int64: carried as a double rather than rejected.
  const UiValue big = parse_ok("9223372036854775808");
  EXPECT_TRUE(big.is_double());
  EXPECT_DOUBLE_EQ(big.as_double(), 9223372036854775808.0);
}

TEST(JsonParse, NumberRangeRules)
{
  parse_fail("1e400");     // overflow: JSON has no infinity
  parse_fail("-1e400");
  const UiValue tiny = parse_ok("1e-400");  // underflow rounds to zero
  EXPECT_TRUE(tiny.is_double());
  EXPECT_EQ(tiny.as_double(), 0.0);
  EXPECT_TRUE(std::signbit(parse_ok("-1e-400").as_double()));
  EXPECT_EQ(parse_ok("0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001e-100").as_double(), 0.0);
}

TEST(JsonParse, MalformedNumbers)
{
  parse_fail("01");
  parse_fail("-");
  parse_fail("1.");
  parse_fail(".5");
  parse_fail("1e");
  parse_fail("1e+");
  parse_fail("+1");
  parse_fail("0x10");
  parse_fail("NaN");
  parse_fail("Infinity");
  parse_fail("-Infinity");
}

TEST(JsonParse, StringsWithEscapes)
{
  EXPECT_EQ(parse_ok("\"a\\\"b\\\\c\\/d\"").as_string(), "a\"b\\c/d");
  EXPECT_EQ(parse_ok("\"\\b\\f\\n\\r\\t\"").as_string(), "\b\f\n\r\t");
  EXPECT_EQ(parse_ok("\"\\u0041\\u00e9\"").as_string(), "A\xC3\xA9");
  EXPECT_EQ(parse_ok("\"\\u20AC\"").as_string(), "\xE2\x82\xAC");
  EXPECT_EQ(parse_ok("\"\\ud83d\\ude00\"").as_string(), "\xF0\x9F\x98\x80");  // surrogate pair
  EXPECT_EQ(parse_ok("\"\\u0000\"").as_string(), std::string("\0", 1));
}

TEST(JsonParse, RawUtf8InStrings)
{
  const std::string text = "\"h\xC3\xA9llo \xF0\x9F\x98\x80\"";
  EXPECT_EQ(parse_ok(text).as_string(), "h\xC3\xA9llo \xF0\x9F\x98\x80");
}

TEST(JsonParse, MalformedStrings)
{
  parse_fail("\"unterminated");
  parse_fail("\"bad \\x escape\"");
  parse_fail("\"\\u12\"");
  parse_fail("\"\\u12G4\"");
  parse_fail("\"\\ud83d\"");         // lone high surrogate
  parse_fail("\"\\ud83d\\u0041\"");  // high surrogate not followed by a low one
  parse_fail("\"\\ude00\"");         // lone low surrogate
  parse_fail("\"tab\there\"");       // raw control character
  parse_fail(std::string("\"nul\0\"", 6));
  parse_fail("\"\xFF\"");            // invalid UTF-8 byte
  parse_fail("\"\xC0\xAF\"");        // overlong encoding of '/'
  parse_fail("\"\xED\xA0\x80\"");    // UTF-8-encoded surrogate
  parse_fail("\"\xF4\x90\x80\x80\"");  // above U+10FFFF
  parse_fail("\"\xE2\x82\"");        // truncated sequence
  parse_fail("'single quotes'");
}

TEST(JsonParse, ContainersAndWhitespace)
{
  const UiValue v = parse_ok(" {\n\t\"a\" : [ 1 , 2.5 , \"x\" , null , true ] ,\r\n \"b\" : { } } ");
  ASSERT_TRUE(v.is_object());
  ASSERT_EQ(v.size(), 2u);
  const UiValue * a = v.find("a");
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->size(), 5u);
  EXPECT_EQ(a->at(0)->as_int(), 1);
  EXPECT_DOUBLE_EQ(a->at(1)->as_double(), 2.5);
  EXPECT_EQ(a->at(2)->as_string(), "x");
  EXPECT_TRUE(a->at(3)->is_null());
  EXPECT_TRUE(a->at(4)->as_bool());
  EXPECT_TRUE(v.find("b")->is_object());
  EXPECT_TRUE(v.find("b")->empty());
  EXPECT_TRUE(parse_ok("[]").is_array());
  EXPECT_TRUE(parse_ok("{}").is_object());
}

TEST(JsonParse, DuplicateKeysLastOneWins)
{
  const UiValue v = parse_ok("{\"k\":1,\"k\":2,\"j\":3}");
  EXPECT_EQ(v.size(), 2u);
  EXPECT_EQ(v.find("k")->as_int(), 2);
  EXPECT_EQ(v.entries()[0].key, "k");  // keeps its first position
}

TEST(JsonParse, MalformedContainers)
{
  parse_fail("[1,]");
  parse_fail("[,1]");
  parse_fail("{\"a\":1,}");
  parse_fail("{,}");
  parse_fail("{\"a\"}");
  parse_fail("{\"a\":}");
  parse_fail("{a:1}");
  parse_fail("{1:1}");
  parse_fail("[1 2]");
  parse_fail("[1,2");
  parse_fail("{\"a\":1");
  parse_fail("]");
  parse_fail("}");
  parse_fail("[1]]");
  parse_fail("{\"a\":1}}");
  parse_fail("[1] [2]");
  parse_fail("// comment\n1");
  parse_fail("1 /* c */");
}

TEST(JsonParse, EmptyAndGarbage)
{
  parse_fail("");
  parse_fail("   ");
  parse_fail("nul");
  parse_fail("tru");
  parse_fail("True");
  parse_fail("undefined");
  parse_fail("\x01");
}

TEST(JsonParse, ErrorCarriesLineAndColumn)
{
  const JsonParseError error = parse_fail("{\n  \"a\": 1,\n  \"b\": ]\n}");
  EXPECT_EQ(error.line, 3u);
  EXPECT_EQ(error.column, 8u);
  EXPECT_NE(error.message.find("expected a value"), std::string::npos);
  EXPECT_NE(error.to_string().find("line 3, column 8"), std::string::npos);

  const JsonParseError trailing = parse_fail("[1,]");
  EXPECT_NE(trailing.message.find("trailing comma"), std::string::npos);
  EXPECT_EQ(trailing.offset, 3u);
}

TEST(JsonParse, LeadingBomIsSkipped)
{
  EXPECT_EQ(parse_ok("\xEF\xBB\xBF[1]").size(), 1u);
}

TEST(JsonParse, DeepNestingWithinTheLimitParsesAndBeyondItIsRejected)
{
  const std::string deep_ok(200, '[');
  EXPECT_TRUE(parse_ok(deep_ok + std::string(200, ']')).is_array());

  const std::string deep_bad(300, '[');
  const JsonParseError error = parse_fail(deep_bad + std::string(300, ']'));
  EXPECT_NE(error.message.find("depth"), std::string::npos);

  JsonParseOptions options;
  options.max_depth = 3;
  UiValue value;
  JsonParseError shallow_error;
  EXPECT_TRUE(json_parse("[[[1]]]", value, shallow_error, options));
  EXPECT_FALSE(json_parse("[[[[1]]]]", value, shallow_error, options));
  EXPECT_FALSE(json_parse("{\"a\":{\"b\":{\"c\":{}}}}", value, shallow_error, options));
}

TEST(JsonRoundTrip, StructuredValueSurvivesWriteAndParse)
{
  UiValue original = UiValue::object()
    .set("header", UiValue::object().set("frame_id", "base_link").set("stamp",
      UiValue::object().set("sec", 12).set("nanosec", 500)))
    .set("ranges", UiValue::array().push_back(0.5).push_back(1.25).push_back(-0.0))
    .set("ok", true)
    .set("name", "r\xC3\xB6" "bot \"one\"\n")
    .set("nothing", nullptr)
    .set("big", std::int64_t{-9223372036854775807});
  const std::string text = json_write(original);
  UiValue back;
  JsonParseError error;
  ASSERT_TRUE(json_parse(text, back, error)) << error.to_string();
  EXPECT_EQ(back, original);
  EXPECT_EQ(json_write(back), text);
}

TEST(JsonRoundTrip, PrettyOutputParsesBackIdentically)
{
  UiValue original = UiValue::object().set("a", UiValue::array().push_back(UiValue::object().set("b", 1.5)));
  JsonWriteOptions options;
  options.indent = 4;
  UiValue back;
  JsonParseError error;
  ASSERT_TRUE(json_parse(json_write(original, options), back, error));
  EXPECT_EQ(back, original);
}
