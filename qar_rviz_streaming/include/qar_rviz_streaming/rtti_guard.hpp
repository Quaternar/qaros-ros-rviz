// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RTTI_GUARD_HPP_
#define QAR_RVIZ_STREAMING__RTTI_GUARD_HPP_

/// @file rtti_guard.hpp
/// @brief Identify the object behind an opaque `void *` by its OWN run-time type
/// information, before anything is called through it. Standard library only.
///
/// ## Why this exists
///
/// hidden_rviz_panel.cpp reaches RViz's panel factory through a protected member of
/// `rviz_common::VisualizationFrame` whose type (`PanelFactory`) is only forward-declared
/// in the installed headers. The read assumes a member offset compiled in from those
/// headers, and is only as good as the match between them and the installed `rviz_common`
/// binary, so what it returns is not trusted: it is held as a `void *`, the object it
/// points at is asked what it is, and only a pointer that the object's own RTTI hands
/// back is ever called through. This header is that question, in a form that needs
/// neither RViz nor Qt, so it can be unit tested by the plain CMake build.
///
/// ## The rule: never apply `typeid` to a class-template type here
///
/// MSVC has a front-end bug (it reproduces on VS2019 14.29 and VS2022 14.41, at
/// /O2 and at /Od; see docs/DEVELOPING.md, "The RTTI guard rule"):
/// `typeid(*p)` is evaluated STATICALLY, at compile time, when `*p` is
/// of a class-template specialization and that `typeid` is the first thing in the
/// translation unit that needs the specialization instantiated. The answer is then
/// always the static type - `class rviz_common::ClassIdRecordingFactory<class
/// rviz_common::Panel>` - whatever the object really is, so a guard written as
/// `typeid(*factory)` on that pointer rejected every factory, the right one included.
/// `typeid` through a NON-template polymorphic base (`rviz_common::Factory`) is a true
/// run-time lookup, which is why @ref rtti_cast_from_opaque takes a `Root` to apply it to
/// and a separate `Target` it only `dynamic_cast`s to. Anything added here keeps the rule:
/// `typeid` goes through the non-template root, never through a template `Target`.
///
/// As a safety net against the bug (or a compiler that has it) coming back, the first
/// check below also rejects an answer that is `Root` or `Target` themselves: the object is
/// a complete object and so never of the abstract type either of those is.
///
/// ## The checks, in order
///
///  1. `typeid(*root)` with `root` = `raw` viewed as a `Root`: the object's own most
///     derived type. It must not be `Root` or `Target` (see above).
///  2. That type's name must be accepted by the caller's predicate. The exact class: a
///     different class under the same base is not good enough.
///  3. `dynamic_cast<void *>(root) == raw`: `raw` is the start of that object, not a
///     pointer into the middle of it or to one of its other subobjects.
///  4. `dynamic_cast<Target *>(root)` is non-null: the object really has a `Target` base,
///     located by its RTTI rather than by an assumed layout. Only that pointer is returned.
///
/// Where `raw` is not an object at all, MSVC's RTTI throws `std::__non_rtti_object` (a
/// `std::exception`) instead of crashing, and that - like any other `std::exception` - is
/// caught and becomes the result's error. That safety net is MSVC's; elsewhere reading the
/// RTTI of garbage is undefined behaviour as anywhere, so a caller should first make sure
/// `raw` was read from a sane place (hidden_rviz_panel.cpp checks a layout canary).

#include <cstring>
#include <exception>
#include <string>
#include <typeinfo>

namespace qar_rviz_streaming
{

/// @brief Whether @p name is `type_info::name()` of exactly `rviz_common::PanelFactory`:
/// the MSVC spelling, or the Itanium one (GCC/Clang).
///
/// An exact comparison on purpose. A different class in the same namespace
/// (`rviz_common::PanelFactoryX`), the same name in another namespace
/// (`my::PanelFactory`) and the template layers underneath it
/// (`ClassIdRecordingFactory<...>`, `PluginlibFactory<...>`, `Factory`) are all other
/// strings and are all rejected. Null is rejected.
inline bool is_rviz_panel_factory_type_name(const char * name)
{
  return name != nullptr &&
         (std::strcmp(name, "class rviz_common::PanelFactory") == 0 ||
          std::strcmp(name, "N11rviz_common12PanelFactoryE") == 0);
}

/// @brief What @ref rtti_cast_from_opaque found.
template<class Target>
struct RttiCastResult
{
  /// Set only when every check passed; null otherwise (see @ref error).
  Target * target = nullptr;

  /// What the object's own RTTI says its most derived type is (`type_info::name()`). Set
  /// whenever the lookup got that far - including when a later check then rejected the
  /// object, where it is the reason - and empty when it did not (a null pointer, or the
  /// lookup itself failed).
  std::string dynamic_type_name;

  /// Why there is no target. Empty on success.
  std::string error;
};

/// @brief Identify the object at @p raw by ITS OWN run-time type information and, only if
/// @p accept_type_name accepts its most derived type's name, return its `Target` base as
/// located by that RTTI. See the file comment for the four checks and the MSVC bug that
/// shapes them.
///
/// @tparam Root   a NON-template polymorphic class, complete here, that is the subobject
///                at offset 0 of the object at @p raw. It is the static type `typeid` is
///                applied to; it must never be a class-template specialization.
/// @tparam Target the class the caller will use the object as; reached by `dynamic_cast`
///                from `Root`. May be a class template's specialization.
/// @param raw  the address of a most-derived object whose `Root` subobject starts there.
///             Null is an error, not a crash.
/// @param accept_type_name predicate on `type_info::name()` of the most derived type. A
///             null predicate accepts nothing.
template<class Root, class Target>
RttiCastResult<Target> rtti_cast_from_opaque(void * raw, bool (*accept_type_name)(const char *))
{
  RttiCastResult<Target> out;
  if (raw == nullptr) {
    out.error = "null pointer";
    return out;
  }
  try {
    Root * root = static_cast<Root *>(raw);
    // Through the NON-template Root: a true run-time lookup (see the file comment).
    const std::type_info & dynamic_type = typeid(*root);
    out.dynamic_type_name = dynamic_type.name();
    // A complete object is never of an abstract type; seeing Root or Target here means
    // typeid was evaluated statically after all.
    if (dynamic_type == typeid(Root) || dynamic_type == typeid(Target)) {
      out.error = "typeid reported the static type '" + out.dynamic_type_name +
        "' - run-time type identification did not happen";
      return out;
    }
    if (accept_type_name == nullptr || !accept_type_name(dynamic_type.name())) {
      out.error = "the object is a '" + out.dynamic_type_name + "'";
      return out;
    }
    if (dynamic_cast<void *>(root) != raw) {
      out.error = "the pointer is not the start of the '" + out.dynamic_type_name + "' object";
      return out;
    }
    Target * target = dynamic_cast<Target *>(root);
    if (target == nullptr) {
      out.error = "a '" + out.dynamic_type_name + "' has no accessible base of the requested type";
      return out;
    }
    out.target = target;
  } catch (const std::exception & e) {   // MSVC: std::__non_rtti_object on a bad pointer
    out.target = nullptr;
    out.error = std::string("run-time type identification failed: ") + e.what();
  }
  return out;
}

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RTTI_GUARD_HPP_
