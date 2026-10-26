// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The ONLY place the concrete fake factories are defined. See rtti_guard_test_types.hpp for
// why: the tests must meet them as opaque pointers, the way hidden_rviz_panel.cpp meets
// RViz's PanelFactory.

#include "rtti_guard_test_types.hpp"

#include <typeinfo>

namespace qar_rviz_streaming_test
{
namespace
{

/// Analogue of rviz_common::PluginlibFactory<Panel>: one more template layer between the
/// recording factory and the concrete class, as in RViz.
template<class T>
class FakePluginlibFactory : public FakeRecordingFactory<T>
{
};

/// Analogue of rviz_common::PanelFactory.
class FakePanelFactory : public FakePluginlibFactory<FakePanel>
{
public:
  int tag() const override { return 1; }

protected:
  FakePanel makeRaw(int class_id) override { return FakePanel{class_id * 2}; }
};

/// Another factory over the same base: everything but the name is right.
class FakeOtherFactory : public FakePluginlibFactory<FakePanel>
{
public:
  int tag() const override { return 2; }

protected:
  FakePanel makeRaw(int class_id) override { return FakePanel{class_id * 3}; }
};

/// Polymorphic, so it has RTTI of its own, but not a FakeRoot.
class FakeUnrelated
{
public:
  virtual ~FakeUnrelated() = default;
  int payload = 5;
};

/// A FakeRoot without the FakeRecordingFactory<FakePanel> layer.
class FakeRootOnlyFactory : public FakeRoot
{
public:
  int tag() const override { return 3; }
};

/// A second, unrelated polymorphic base, so that a factory has a subobject that does not
/// start where the object does.
class FakeSideBase
{
public:
  virtual ~FakeSideBase() = default;
  virtual int side() const { return 7; }
};

class FakeTwoBaseFactory : public FakePluginlibFactory<FakePanel>, public FakeSideBase
{
public:
  int tag() const override { return 4; }

protected:
  FakePanel makeRaw(int class_id) override { return FakePanel{class_id * 4}; }
};

}  // namespace

void * fake_panel_factory()
{
  static FakePanelFactory instance;
  return &instance;
}

const char * fake_panel_factory_type_name()
{
  return typeid(FakePanelFactory).name();
}

void * fake_other_factory()
{
  static FakeOtherFactory instance;
  return &instance;
}

void * fake_unrelated_object()
{
  static FakeUnrelated instance;
  return &instance;
}

void * fake_root_only_factory()
{
  static FakeRootOnlyFactory instance;
  return &instance;
}

void * fake_two_base_factory_second_subobject()
{
  static FakeTwoBaseFactory instance;
  return static_cast<FakeSideBase *>(&instance);
}

}  // namespace qar_rviz_streaming_test
