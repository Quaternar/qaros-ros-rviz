// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__HIDDEN_RVIZ_PANEL_HPP_
#define QAR_RVIZ_STREAMING__HIDDEN_RVIZ_PANEL_HPP_

/// @file hidden_rviz_panel.hpp
/// @brief A NEW instance of RViz's own Displays panel, made without a dock, for
/// streaming into AR - never shown on the desktop.
///
/// ## Why this reaches into RViz the way it does
///
/// `rviz_common::DisplaysPanel` is not part of RViz's installed API: its header is
/// not installed and the class is not exported. RViz makes it through
/// `rviz_common::PanelFactory`, which registers it as a BUILT-IN class
/// (`addBuiltInClass("rviz_common", "Displays", ...)`, class id
/// `rviz_common/Displays`) - it is not a pluginlib plugin, so no manifest names it
/// and nothing here can ask pluginlib for it. The factory is owned by
/// `rviz_common::VisualizationFrame` (`panel_factory_`), which is `protected`, has no
/// getter, and whose type `PanelFactory` is only forward-declared in the installed
/// headers.
///
/// What IS installed and exported is the frame itself, and the one layer of the
/// factory that matters here: `ClassIdRecordingFactory<Panel>` (a header-only
/// template that depends on nothing but `Factory`), whose virtual `make(class_id,
/// &error)` is the entry point every panel goes through. So:
///
///  1. the frame is found from the display context
///     (`getWindowManager()` is the `VisualizationFrame`);
///  2. its protected `panel_factory_` is read through a pointer-to-member formed in a
///     derived class - the one place the language lets code name a protected member
///     of a base it has no instance of; the derived class is never constructed. The
///     read assumes a member offset compiled in from the installed header, so it is
///     first cross-checked: the frame's `manager_`, read the same way, must equal what
///     the exported `getManager()` returns (which uses the binary's own layout);
///  3. the `PanelFactory*` is NOT converted to a base - `PanelFactory` is incomplete
///     here, and an assumed "offset 0" would be a guess. It is held as a `void *` and
///     the object is asked what it is, by its own run-time type information: `typeid`
///     through the non-template root `rviz_common::Factory` must name exactly
///     `rviz_common::PanelFactory`; `dynamic_cast<void *>` must give the same address
///     back; and `dynamic_cast<ClassIdRecordingFactory<Panel> *>` must succeed. Only
///     the pointer RTTI hands back is ever called through (see rtti_guard.hpp).
///     `typeid` is deliberately never applied to `ClassIdRecordingFactory<Panel>`
///     itself: MSVC (VS2019 14.29 and VS2022 14.41, at /O2 and /Od) evaluates it
///     STATICALLY when it is the first use of that template in the translation unit,
///     so it always answers with the static type and a guard written that way rejects
///     every factory, the right one included;
///  4. `make("rviz_common/Displays")` builds the panel and `initialize(context)`
///     binds it to the SAME display tree model the real Displays dock shows - so a
///     checkbox toggled, a property edited or a display added/removed/renamed through
///     this copy acts on the real displays.
///
/// This depends on the installed headers matching the installed RViz binaries
/// (the plugin is built against the same distro, so they do), and a mismatch is
/// detected and reported rather than assumed away. It cannot be type-checked by the
/// repo-root CMake project, which has no RViz; the colcon build compiles it (the RTTI
/// check itself, rtti_guard.hpp, needs nothing from RViz and IS unit tested there).
/// Every step fails soft: a null at any point, a factory that fails the check, a class
/// that is not what was asked for, or an exception, becomes HiddenRvizPanel::error and
/// no panel - the caller carries on without the feature.
///
/// The panel is returned parentless and NOT shown: making it a never-on-desktop,
/// fixed-size surface is qar_qt_streaming::show_offscreen, which the caller applies.

#include <memory>
#include <string>

#include "rviz_common/panel.hpp"

namespace rviz_common
{
class DisplayContext;
}  // namespace rviz_common

namespace qar_rviz_streaming
{

/// The class id RViz's panel factory registers the Displays panel under.
inline constexpr const char * kDisplaysPanelClassId = "rviz_common/Displays";

/// What Qt's meta-object says that panel's class is. The check that the factory gave
/// back the class that was asked for, before anything else touches it.
inline constexpr const char * kDisplaysPanelQtClassName = "rviz_common::DisplaysPanel";

/// @brief What creating the hidden panel produced.
struct HiddenRvizPanel
{
  /// The new panel, owned by the caller. Null on failure (see @ref error).
  std::unique_ptr<rviz_common::Panel> panel;

  /// The class id that was asked of the factory.
  std::string class_id;

  /// What the run-time type information of the object in RViz's main window's
  /// panel-factory slot says it is: `type_info::name()` of its most derived type
  /// ("class rviz_common::PanelFactory" on MSVC). Set whenever that check ran -
  /// including when it rejected the object, where it is the reason - and empty when the
  /// code never got that far (no frame, a layout mismatch, no factory, RTTI failing).
  std::string factory_type;

  /// Qt's name for the class of the object the factory made. Set whenever the factory
  /// made something - including a panel that was then rejected, where it is the
  /// reason.
  std::string class_name;

  /// True when item dragging was switched off on the copy's property tree, so that
  /// synthetic AR input can never start a native drag-and-drop loop.
  bool item_drag_disabled = false;

  /// True when the help area under the copy's property tree (a text browser) was hidden, so
  /// that the tree gets the whole height of the copy. The Displays panel of the desktop is not
  /// touched.
  bool help_area_hidden = false;

  /// Why there is no panel. Empty on success.
  std::string error;
};

/// @brief Create a new instance of RViz's Displays panel and bind it to @p context.
///
/// GUI thread only. @p context must be the live display context of the running RViz
/// (the one `rviz_common::Panel::getDisplayContext()` gave this plugin) and must stay
/// valid for as long as the returned panel lives; the panel's view of the display
/// tree follows the VisualizationManager's own lifetime, so the caller destroys it
/// no later than the plugin panel that owns it (see QarStreamingPanel::hidden_displays_).
///
/// On the returned copy only, item dragging is turned off: RViz's Displays tree lets
/// the operator drag a display to reorder it, and a drag started by synthetic AR
/// pointer input must never reach the operating system's drag-and-drop loop. Its help
/// area is hidden too, so that the property tree has the whole height of the copy and a
/// copy that is made as high as its tree needs shows no scroll bar.
HiddenRvizPanel create_hidden_displays_panel(rviz_common::DisplayContext * context);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__HIDDEN_RVIZ_PANEL_HPP_
