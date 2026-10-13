// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The value tree is the one data shape every other component agrees on, so
// its edge rules are pinned here: kind strictness, numeric conversions,
// insertion-ordered objects, and the "a Null becomes a container, nothing else
// converts" policy.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "qar_ui_model/ui_value.hpp"

using qar_ui_model::UiValue;
using qar_ui_model::UiValueKind;

TEST(UiValue, DefaultIsNull)
{
  UiValue v;
  EXPECT_EQ(v.kind(), UiValueKind::Null);
  EXPECT_TRUE(v.is_null());
  EXPECT_EQ(v.size(), 0u);
  EXPECT_TRUE(v.empty());
}

TEST(UiValue, ScalarConstructorsPickTheRightKind)
{
  EXPECT_EQ(UiValue(true).kind(), UiValueKind::Bool);
  EXPECT_EQ(UiValue(std::int64_t{5}).kind(), UiValueKind::Int);
  EXPECT_EQ(UiValue(5).kind(), UiValueKind::Int);
  EXPECT_EQ(UiValue(5u).kind(), UiValueKind::Int);
  EXPECT_EQ(UiValue(std::int8_t{-3}).kind(), UiValueKind::Int);
  EXPECT_EQ(UiValue(1.5).kind(), UiValueKind::Double);
  EXPECT_EQ(UiValue(1.5f).kind(), UiValueKind::Double);
  EXPECT_EQ(UiValue("text").kind(), UiValueKind::String);
  EXPECT_EQ(UiValue(std::string("text")).kind(), UiValueKind::String);
  EXPECT_EQ(UiValue(std::string_view("text")).kind(), UiValueKind::String);
  EXPECT_EQ(UiValue(nullptr).kind(), UiValueKind::Null);
  EXPECT_EQ(UiValue::array().kind(), UiValueKind::Array);
  EXPECT_EQ(UiValue::object().kind(), UiValueKind::Object);
}

TEST(UiValue, UnsignedAboveInt64MaxBecomesDouble)
{
  const UiValue in_range(std::uint64_t{42});
  EXPECT_TRUE(in_range.is_int());
  EXPECT_EQ(in_range.as_int(), 42);

  const UiValue too_big(std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(too_big.is_double());
  EXPECT_DOUBLE_EQ(too_big.as_double(), 18446744073709551615.0);
}

TEST(UiValue, NullConstCharPointerIsAnEmptyString)
{
  const char * null_text = nullptr;
  const UiValue v(null_text);
  EXPECT_TRUE(v.is_string());
  EXPECT_EQ(v.as_string(), "");
}

TEST(UiValue, AccessorsReturnFallbacksForOtherKinds)
{
  const UiValue s("text");
  EXPECT_EQ(s.as_bool(true), true);
  EXPECT_EQ(s.as_int(7), 7);
  EXPECT_DOUBLE_EQ(s.as_double(2.5), 2.5);
  EXPECT_EQ(UiValue(3).as_string(), "");
  EXPECT_EQ(UiValue(3).as_string("fallback"), "fallback");
  EXPECT_EQ(UiValue(1).as_bool(), false);  // no int -> bool coercion
}

TEST(UiValue, NumbersConvertBetweenIntAndDoubleExactlyOnly)
{
  EXPECT_DOUBLE_EQ(UiValue(3).as_double(), 3.0);
  EXPECT_EQ(UiValue(3.0).as_int(), 3);
  EXPECT_EQ(UiValue(3.5).as_int(-1), -1);
  EXPECT_EQ(UiValue(1e300).as_int(-1), -1);
  EXPECT_EQ(UiValue(std::numeric_limits<double>::quiet_NaN()).as_int(-1), -1);
  EXPECT_EQ(UiValue(-9223372036854775808.0).as_int(-1), std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(UiValue(9223372036854775808.0).as_int(-1), -1);  // 2^63 overflows
}

TEST(UiValue, ArrayPushBackAtAndElements)
{
  UiValue a = UiValue::array();
  a.push_back(1).push_back("two").push_back(3.0);
  ASSERT_EQ(a.size(), 3u);
  ASSERT_NE(a.at(1), nullptr);
  EXPECT_EQ(a.at(1)->as_string(), "two");
  EXPECT_EQ(a.at(3), nullptr);
  EXPECT_EQ(a.elements()[2].as_double(), 3.0);

  UiValue * mutable_element = a.at(0);
  ASSERT_NE(mutable_element, nullptr);
  *mutable_element = UiValue(10);
  EXPECT_EQ(a.at(0)->as_int(), 10);
}

TEST(UiValue, NullConvertsToContainerOnFirstUseButOtherKindsDoNot)
{
  UiValue a;
  a.push_back(1);
  EXPECT_TRUE(a.is_array());
  EXPECT_EQ(a.size(), 1u);

  UiValue o;
  o.set("k", 1);
  EXPECT_TRUE(o.is_object());

  UiValue s("text");
  s.push_back(1);
  s.set("k", 1);
  EXPECT_TRUE(s.is_string());  // untouched
  EXPECT_EQ(s.as_string(), "text");
  EXPECT_EQ(s.at(0), nullptr);
  EXPECT_EQ(s.find("k"), nullptr);
  EXPECT_TRUE(s.elements().empty());
  EXPECT_TRUE(s.entries().empty());
}

TEST(UiValue, ObjectKeepsInsertionOrderAndReplacesInPlace)
{
  UiValue o = UiValue::object();
  o.set("z", 1).set("a", 2).set("m", 3);
  ASSERT_EQ(o.size(), 3u);
  EXPECT_EQ(o.entries()[0].key, "z");
  EXPECT_EQ(o.entries()[1].key, "a");
  EXPECT_EQ(o.entries()[2].key, "m");

  o.set("a", 20);  // replace: same position, new value
  ASSERT_EQ(o.size(), 3u);
  EXPECT_EQ(o.entries()[1].key, "a");
  EXPECT_EQ(o.entries()[1].value.as_int(), 20);
}

TEST(UiValue, ObjectFindContainsErase)
{
  UiValue o = UiValue::object();
  o.set("x", 1.5).set("y", "text");
  ASSERT_NE(o.find("x"), nullptr);
  EXPECT_DOUBLE_EQ(o.find("x")->as_double(), 1.5);
  EXPECT_TRUE(o.contains("y"));
  EXPECT_FALSE(o.contains("nope"));
  EXPECT_EQ(o.find("nope"), nullptr);

  UiValue * y = o.find("y");
  ASSERT_NE(y, nullptr);
  *y = UiValue(false);
  EXPECT_FALSE(o.find("y")->as_bool(true));

  EXPECT_TRUE(o.erase("x"));
  EXPECT_FALSE(o.erase("x"));
  EXPECT_EQ(o.size(), 1u);
  EXPECT_EQ(o.entries()[0].key, "y");
}

TEST(UiValue, NestedMutationThroughPointers)
{
  UiValue root = UiValue::object();
  root.set("pose", UiValue::object().set("position", UiValue::object().set("x", 0.0)));
  UiValue * x = root.find("pose")->find("position")->find("x");
  ASSERT_NE(x, nullptr);
  *x = UiValue(2.5);
  EXPECT_DOUBLE_EQ(root.find("pose")->find("position")->find("x")->as_double(), 2.5);
}

TEST(UiValue, EqualityIsStrictOnKind)
{
  EXPECT_EQ(UiValue(5), UiValue(5));
  EXPECT_NE(UiValue(5), UiValue(5.0));
  EXPECT_NE(UiValue(true), UiValue(1));
  EXPECT_NE(UiValue("1"), UiValue(1));
  EXPECT_EQ(UiValue(), UiValue(nullptr));

  UiValue a = UiValue::object().set("k", 1).set("j", 2);
  UiValue b = UiValue::object().set("k", 1).set("j", 2);
  UiValue c = UiValue::object().set("j", 2).set("k", 1);  // different order
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);

  UiValue arr1 = UiValue::array().push_back(1).push_back(2);
  UiValue arr2 = UiValue::array().push_back(1).push_back(2);
  UiValue arr3 = UiValue::array().push_back(1);
  EXPECT_EQ(arr1, arr2);
  EXPECT_NE(arr1, arr3);
}

TEST(UiValue, CopyIsDeep)
{
  UiValue a = UiValue::object().set("list", UiValue::array().push_back(1));
  UiValue b = a;
  b.find("list")->push_back(2);
  EXPECT_EQ(a.find("list")->size(), 1u);
  EXPECT_EQ(b.find("list")->size(), 2u);
}

TEST(UiValue, KindNames)
{
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::Null), "null");
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::Bool), "bool");
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::Int), "int");
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::Double), "double");
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::String), "string");
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::Array), "array");
  EXPECT_STREQ(qar_ui_model::to_string(UiValueKind::Object), "object");
}
