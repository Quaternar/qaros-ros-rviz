// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Stand-ins for the classes rtti_guard.hpp meets in RViz, arranged the way RViz's are
// arranged relative to hidden_rviz_panel.cpp:
//
//   RViz                                          here
//   rviz_common::Factory                          FakeRoot                 (non-template root)
//   ClassIdRecordingFactory<Panel>                FakeRecordingFactory<FakePanel>
//                                                 (a class TEMPLATE, complete in every TU)
//   PluginlibFactory<Panel>, PanelFactory,        FakePanelFactory and friends: defined ONLY in
//   other factories, other objects                rtti_guard_test_types.cpp, like PanelFactory is
//                                                 only in rviz_common.dll. A test TU cannot name
//                                                 them and sees only what it gets as a void *.
//
// That is the situation the guard exists for - an opaque pointer, a complete template base,
// an object whose real type the compiler cannot see - including the one that MSVC's static
// typeid bug needs (see rtti_guard.hpp).

#ifndef QAR_RVIZ_STREAMING_TEST__RTTI_GUARD_TEST_TYPES_HPP_
#define QAR_RVIZ_STREAMING_TEST__RTTI_GUARD_TEST_TYPES_HPP_

namespace qar_rviz_streaming_test
{

/// What the fake factories make: the analogue of rviz_common::Panel.
struct FakePanel
{
  int made_by = 0;
};

/// Analogue of rviz_common::Factory: a NON-template, abstract, polymorphic root.
class FakeRoot
{
public:
  virtual ~FakeRoot() = default;

  /// Which fake this is; a virtual call on the root's own part of the vtable.
  virtual int tag() const = 0;
};

/// Analogue of rviz_common::ClassIdRecordingFactory<Panel>: a class TEMPLATE derived from
/// the root. Its virtual `make` is the call the guard exists to protect.
template<class T>
class FakeRecordingFactory : public FakeRoot
{
public:
  virtual T make(int class_id) { return makeRaw(class_id); }

protected:
  virtual T makeRaw(int class_id) = 0;
};

/// The type the guard is asked to hand back.
using FakeClassFactory = FakeRecordingFactory<FakePanel>;

// Opaque pointers to long-lived (function-local static) fakes, so nothing is ever deleted
// and no test can name the concrete type behind one. Each is the address of a complete
// object, except where it says otherwise.

/// The object the guard must accept: a FakeRecordingFactory<FakePanel> through two
/// single-inheritance layers, tag() 1, make(n) gives a panel with made_by == 2 * n.
void * fake_panel_factory();

/// `type_info::name()` of that object's most derived type, which is what a guard for it
/// compares against (the equivalent of "class rviz_common::PanelFactory").
const char * fake_panel_factory_type_name();

/// A DIFFERENT concrete factory over the same FakeRecordingFactory<FakePanel> base (tag()
/// 2): it passes every check except the name, so it shows the name check is load-bearing.
void * fake_other_factory();

/// A polymorphic object that is not a FakeRoot at all.
void * fake_unrelated_object();

/// A FakeRoot that is NOT a FakeRecordingFactory<FakePanel> (tag() 3).
void * fake_root_only_factory();

/// NOT the start of an object: the address of the second base subobject of a factory that
/// has two. Seen through its own vtable it is a perfectly good object of the right type.
void * fake_two_base_factory_second_subobject();

}  // namespace qar_rviz_streaming_test

#endif  // QAR_RVIZ_STREAMING_TEST__RTTI_GUARD_TEST_TYPES_HPP_
