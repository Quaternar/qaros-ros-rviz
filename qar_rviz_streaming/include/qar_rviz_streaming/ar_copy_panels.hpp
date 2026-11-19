// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_COPY_PANELS_HPP_
#define QAR_RVIZ_STREAMING__AR_COPY_PANELS_HPP_

/// @file ar_copy_panels.hpp
/// @brief Which RViz panels get an AR copy of their own, and how wide that copy is. Standard
/// library only.
///
/// ## What an AR copy is
///
/// A panel of RViz whose state lives in a model that RViz shares between its widgets (the
/// display tree, the view manager, the tool manager, the selection manager) can be made a
/// second time through RViz's panel factory: the second instance binds to the same model, so
/// every check box, value edit or button of the copy acts on the same data as the dock on the
/// desktop. The copy is a widget tree of its own, which is what lets the AR side draw its
/// controls larger while the dock on the desktop does not change.
///
/// The Help panel has no state that could differ between two instances: it shows the help file
/// that the display context names in a text browser of its own, so its copy has its own size,
/// its own scroll position and the page it was last given.
///
/// A panel whose state is its own (Time, Transformation, the panels of plugins) has no such
/// copy: a second instance would show a state the dock does not have, or advertise, subscribe
/// or run timers twice. Those panels are streamed as they are.

#include <string>
#include <vector>

namespace qar_rviz_streaming
{

/// An RViz panel class an AR copy can be made of: a second instance binds to RViz's shared
/// models.
struct ArCopyPanelClass
{
  const char * class_id;       ///< the id of RViz's panel factory, e.g. "rviz_common/Views"
  const char * qt_class_name;  ///< what the made object's metaObject() says it is
  bool hide_help_area;         ///< the help text browser under the property tree is hidden
  bool has_property_tree;      ///< the panel shows a property tree (a copy without one is refused)
  int min_height_px;           ///< the least height a new copy is shown with; 0 for none
};

/// The classes that have an AR copy: Displays, Views, Tool Properties, Selection and Help, in
/// that order.
const std::vector<ArCopyPanelClass> & ar_copy_panel_classes();

/// The class of @p class_id, or null for every class without an AR copy.
const ArCopyPanelClass * ar_copy_panel_class(const std::string & class_id);

/// The window title of a copy, `<Name> (AR copy)` with the name taken from the class id after
/// its last slash. It is only a label for debuggers and Qt's own messages: the copy is never
/// on screen.
std::string ar_copy_window_title(const ArCopyPanelClass & cls);

/// The width in pixels below which a copy is not made.
constexpr int kArCopyMinWidthPx = 480;

/// The least height of a copy of a text panel (the Help panel), where the dock is small.
constexpr int kArCopyTextPanelMinHeightPx = 640;

/// @brief The width of a copy: the larger of @p base_width and @ref kArCopyMinWidthPx, grown by
/// a quarter for every step of @p control_factor above 1 (480 becomes 600 at a factor of 2),
/// because enlarged controls widen the indentation of the property tree. The result is never
/// below @p base_width. A factor that is not finite or below 1 counts as 1; one above 4 counts
/// as 4.
int ar_copy_width_px(int base_width, double control_factor);

/// @brief The height a new copy of class @p cls is shown with: the larger of @p dock_height and
/// the least height of the class, within @p least and @p most (the range the fit works in). A
/// range given the wrong way round counts as @p least for both.
int ar_copy_start_height_px(const ArCopyPanelClass & cls, int dock_height, int least, int most);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_COPY_PANELS_HPP_
