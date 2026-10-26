// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The run-time type check hidden_rviz_panel.cpp puts in front of RViz's panel factory
// (rtti_guard.hpp), against stand-in classes that reproduce the situation it exists for:
// an opaque `void *`, a class-TEMPLATE base that is complete here, and a concrete type that
// only another translation unit can name (rtti_guard_test_types.{hpp,cpp}). The rule that
// shapes the guard - MSVC evaluates `typeid` on a class-template type STATICALLY, so the
// guard goes through the non-template root - is explained in rtti_guard.hpp; the test of
// it is that the right object is ACCEPTED and its dynamic type is what is reported.

#include <gtest/gtest.h>

#include "qar_rviz_streaming/rtti_guard.hpp"
#include "rtti_guard_test_types.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <typeinfo>

using qar_rviz_streaming::RttiCastResult;
using qar_rviz_streaming::is_rviz_panel_factory_type_name;
using qar_rviz_streaming::rtti_cast_from_opaque;
using qar_rviz_streaming_test::FakeClassFactory;
using qar_rviz_streaming_test::FakeRoot;
using qar_rviz_streaming_test::fake_other_factory;
using qar_rviz_streaming_test::fake_panel_factory;
using qar_rviz_streaming_test::fake_panel_factory_type_name;
using qar_rviz_streaming_test::fake_root_only_factory;
using qar_rviz_streaming_test::fake_two_base_factory_second_subobject;
using qar_rviz_streaming_test::fake_unrelated_object;

namespace
{

/// What a guard for the fake panel factory would accept: exactly its own type name.
bool accepts_the_fake_panel_factory(const char * name)
{
  return name != nullptr && std::strcmp(name, fake_panel_factory_type_name()) == 0;
}

bool accepts_any_name(const char *)
{
  return true;
}

using Cast = RttiCastResult<FakeClassFactory>;

Cast cast(void * raw, bool (*accept)(const char *) = &accepts_the_fake_panel_factory)
{
  return rtti_cast_from_opaque<FakeRoot, FakeClassFactory>(raw, accept);
}

}  // namespace

// --- The name check ------------------------------------------------------------------

TEST(RttiGuardTypeName, AcceptsTheMsvcAndTheItaniumSpellingOfPanelFactory)
{
  EXPECT_TRUE(is_rviz_panel_factory_type_name("class rviz_common::PanelFactory"));
  EXPECT_TRUE(is_rviz_panel_factory_type_name("N11rviz_common12PanelFactoryE"));
}

TEST(RttiGuardTypeName, RejectsEveryOtherName)
{
  // The template layers beneath PanelFactory: what typeid reports if it is evaluated
  // statically on a ClassIdRecordingFactory<Panel>, or on the layer above it.
  EXPECT_FALSE(is_rviz_panel_factory_type_name(
      "class rviz_common::ClassIdRecordingFactory<class rviz_common::Panel>"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name(
      "class rviz_common::PluginlibFactory<class rviz_common::Panel>"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("class rviz_common::Factory"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name(
      "N11rviz_common22ClassIdRecordingFactoryINS_5PanelEEE"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("N11rviz_common15PluginlibFactoryINS_5PanelEEE"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("N11rviz_common7FactoryE"));

  // Near misses: another namespace, a longer class name, a prefix, another keyword.
  EXPECT_FALSE(is_rviz_panel_factory_type_name("class my::PanelFactory"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("class rviz_common::PanelFactoryX"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("class rviz_common::PanelFactor"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("rviz_common::PanelFactory"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("PanelFactory"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("struct rviz_common::PanelFactory"));
  EXPECT_FALSE(is_rviz_panel_factory_type_name("class rviz_common::PanelFactory "));

  EXPECT_FALSE(is_rviz_panel_factory_type_name(""));
  EXPECT_FALSE(is_rviz_panel_factory_type_name(nullptr));
}

// --- The cast ------------------------------------------------------------------------

TEST(RttiGuardCast, AcceptsTheExpectedTypeAndHandsBackTheSameObject)
{
  void * raw = fake_panel_factory();
  const Cast result = cast(raw);

  ASSERT_NE(result.target, nullptr) << result.error;
  EXPECT_TRUE(result.error.empty()) << result.error;
  EXPECT_EQ(static_cast<void *>(result.target), raw);

  // The object's OWN type is what is reported, not the static type of the root or of the
  // template the target is an instance of - the thing MSVC's static typeid gets wrong.
  EXPECT_EQ(result.dynamic_type_name, fake_panel_factory_type_name());
  EXPECT_NE(result.dynamic_type_name, typeid(FakeClassFactory).name());
  EXPECT_NE(result.dynamic_type_name, typeid(FakeRoot).name());

  // Virtual calls through the returned pointer reach the most derived class, in the
  // template layer's part of the vtable (make) and in the root's (tag).
  EXPECT_EQ(result.target->tag(), 1);
  EXPECT_EQ(result.target->make(21).made_by, 42);
}

TEST(RttiGuardCast, RejectsAnotherFactoryOverTheSameBaseByItsName)
{
  // Everything but the name is right: it IS a FakeRecordingFactory<FakePanel>, at offset 0.
  // Only the name check stands between it and a make() on the wrong factory. (That it
  // would otherwise pass is shown below by accepting every name.)
  const Cast result = cast(fake_other_factory());

  EXPECT_EQ(result.target, nullptr);
  EXPECT_NE(result.dynamic_type_name.find("FakeOtherFactory"), std::string::npos)
    << result.dynamic_type_name;
  EXPECT_NE(result.error.find("FakeOtherFactory"), std::string::npos) << result.error;

  const Cast anything_goes = cast(fake_other_factory(), &accepts_any_name);
  ASSERT_NE(anything_goes.target, nullptr) << anything_goes.error;
  EXPECT_EQ(anything_goes.target->tag(), 2);
}

TEST(RttiGuardCast, RejectsAnUnrelatedObjectByItsName)
{
  // Not a FakeRoot at all; its own vtable still says what it is.
  const Cast result = cast(fake_unrelated_object());

  EXPECT_EQ(result.target, nullptr);
  EXPECT_NE(result.dynamic_type_name.find("FakeUnrelated"), std::string::npos)
    << result.dynamic_type_name;
  EXPECT_NE(result.error.find("FakeUnrelated"), std::string::npos) << result.error;
}

TEST(RttiGuardCast, RejectsANullPointerWithAnError)
{
  const Cast result = cast(nullptr);

  EXPECT_EQ(result.target, nullptr);
  EXPECT_FALSE(result.error.empty());
  EXPECT_TRUE(result.dynamic_type_name.empty());
}

TEST(RttiGuardCast, AcceptsNothingWithoutAPredicate)
{
  const Cast result = cast(fake_panel_factory(), nullptr);

  EXPECT_EQ(result.target, nullptr);
  EXPECT_NE(result.error.find("FakePanelFactory"), std::string::npos) << result.error;
}

TEST(RttiGuardCast, RejectsAPointerIntoTheMiddleOfAnObject)
{
  // The right KIND of object (every name accepted), but the address is its second base
  // subobject, not its start: read through that subobject's vtable it still says what the
  // whole object is, and that is exactly what makes the start-of-object check necessary.
  void * raw = fake_two_base_factory_second_subobject();
  const Cast result = cast(raw, &accepts_any_name);

  EXPECT_EQ(result.target, nullptr);
  EXPECT_NE(result.dynamic_type_name.find("FakeTwoBaseFactory"), std::string::npos)
    << result.dynamic_type_name;
  EXPECT_NE(result.error.find("not the start of"), std::string::npos) << result.error;
}

TEST(RttiGuardCast, RejectsAnObjectWithoutTheTargetBaseEvenIfItsNameIsAccepted)
{
  // The root is right and the name is waved through, but the object is not a
  // FakeRecordingFactory<FakePanel>: the guard hands back nothing to call make() on.
  const Cast result = cast(fake_root_only_factory(), &accepts_any_name);

  EXPECT_EQ(result.target, nullptr);
  EXPECT_NE(result.dynamic_type_name.find("FakeRootOnlyFactory"), std::string::npos)
    << result.dynamic_type_name;
  EXPECT_NE(result.error.find("no accessible base"), std::string::npos) << result.error;
}

#ifdef _MSC_VER
TEST(RttiGuardCast, MsvcTurnsAGarbagePointerIntoAnErrorInsteadOfACrash)
{
  // 0x30 is in the never-mapped low region: reading a vtable pointer there faults, which
  // MSVC's RTTI reports as std::__non_rtti_object ("Access violation - no RTTI data!").
  void * garbage = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x30));
  const Cast result = cast(garbage);

  EXPECT_EQ(result.target, nullptr);
  EXPECT_NE(result.error.find("run-time type identification failed"), std::string::npos)
    << result.error;
}
#endif
