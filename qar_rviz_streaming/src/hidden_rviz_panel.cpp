// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/hidden_rviz_panel.hpp"

#include <exception>
#include <string>
#include <utility>

#include <QMetaObject>
#include <QString>
#include <QTextBrowser>

#include "qar_rviz_streaming/ar_copy_panels.hpp"
#include "qar_rviz_streaming/rtti_guard.hpp"

// Deliberately NOT visualization_manager.hpp, panel_factory.hpp or
// pluginlib_factory.hpp: the first and the last reach pluginlib/class_loader.hpp and
// so <tinyxml2.h>, which this distro does not install, and panel_factory.hpp is not
// installed at all. class_id_recording_factory.hpp depends on nothing but factory.hpp,
// which is also the NON-template root the run-time type check goes through.
#include "rviz_common/display_context.hpp"
#include "rviz_common/factory/class_id_recording_factory.hpp"
#include "rviz_common/factory/factory.hpp"
#include "rviz_common/properties/property_tree_widget.hpp"
#include "rviz_common/visualization_frame.hpp"
#include "rviz_common/window_manager_interface.hpp"

namespace qar_rviz_streaming
{
namespace
{

using PanelClassFactory = rviz_common::ClassIdRecordingFactory<rviz_common::Panel>;

/// Never constructed, never named as a type of any object. It exists for exactly two
/// expressions: `&FramePanelFactoryAccess::panel_factory_` and
/// `&FramePanelFactoryAccess::manager_`. VisualizationFrame's `panel_factory_` and
/// `manager_` are protected (`manager_` has an exported getter, `panel_factory_` has
/// none), and a pointer to a protected member may be formed only through the class the
/// code is a member of - so the code is a member of this derived class. The expressions
/// have type `T VisualizationFrame::*` (a member of the BASE, which is what lets them be
/// applied to a plain VisualizationFrame), so no instance of this class is needed.
///
/// `PanelFactory` and `VisualizationManager` themselves are only forward-declared in the
/// installed headers, which is enough to name a pointer to either and a pointer to member
/// of that type - and not enough to convert one to a base class, which is why nothing
/// here does: `panel_factory_` leaves as a `void *` and is identified by the object's own
/// RTTI (verified_panel_factory).
struct FramePanelFactoryAccess : public rviz_common::VisualizationFrame
{
  /// @return the frame's `panel_factory_` as an opaque address (null if it has none).
  static void * raw_panel_factory(rviz_common::VisualizationFrame & frame)
  {
    rviz_common::PanelFactory * rviz_common::VisualizationFrame::* member =
      &FramePanelFactoryAccess::panel_factory_;
    return frame.*member;
  }

  /// @return the frame's `manager_`, read at the offset the installed header gives it -
  ///         the same kind of read as raw_panel_factory, which is what makes it a canary
  ///         for that one.
  static rviz_common::VisualizationManager * raw_manager(rviz_common::VisualizationFrame & frame)
  {
    rviz_common::VisualizationManager * rviz_common::VisualizationFrame::* member =
      &FramePanelFactoryAccess::manager_;
    return frame.*member;
  }
};

/// The VisualizationFrame behind @p context: the display context's window manager IS
/// the frame (VisualizationFrame::initialize hands itself to the VisualizationManager).
rviz_common::VisualizationFrame *
find_frame(rviz_common::DisplayContext * context)
{
  rviz_common::WindowManagerInterface * window_manager = context->getWindowManager();
  if (window_manager == nullptr) {
    return nullptr;
  }
  if (auto * frame = dynamic_cast<rviz_common::VisualizationFrame *>(window_manager)) {
    return frame;
  }
  // Same object, found the Qt way: getParentWindow() is the frame as a QWidget.
  return qobject_cast<rviz_common::VisualizationFrame *>(window_manager->getParentWindow());
}

/// @brief The frame's panel factory as the ClassIdRecordingFactory<Panel> it is, or null
/// with @p out.error saying why - and @p out.factory_type set whenever the type check ran.
///
/// `frame.panel_factory_` is read at an offset compiled in from the installed header, to
/// an incomplete type, so nothing about the pointer is taken on trust. In order:
///
///  - G1, layout: the frame's `manager_`, read the same way, must be non-null and equal
///    what the exported `getManager()` returns (which reads it with the BINARY's own
///    layout). A header that does not describe the installed rviz_common fails here, with
///    a message, instead of handing on a wild pointer. A null `panel_factory_` is then
///    reported as such.
///  - G2, identity: the pointer is held as an opaque `void *`, viewed as the NON-template
///    root `rviz_common::Factory` and asked its own run-time type (`typeid`). The most
///    derived type must be exactly `rviz_common::PanelFactory`. `typeid` is deliberately
///    NOT applied to `ClassIdRecordingFactory<Panel>`: MSVC (VS2019 14.29, VS2022 14.41)
///    evaluates that statically when it is the first use of the specialization in the
///    translation unit - which the one use of the factory pointer here would have been -
///    so it always answers with the static type and the check can never pass. A static
///    answer is itself rejected, see rtti_guard.hpp.
///  - G3: `dynamic_cast<void *>` of it gives back the same address: the pointer is the
///    start of that object, not a subobject or an interior address.
///  - G4: `dynamic_cast<ClassIdRecordingFactory<Panel> *>` of it is non-null. That
///    pointer, located by the object's own RTTI and not by an assumed layout, is the only
///    one `make()` is ever called on.
///
/// G2-G4 are rtti_cast_from_opaque. On MSVC a pointer that is not an object at all makes
/// the RTTI throw `std::__non_rtti_object` (a `std::exception`), which rtti_cast_from_opaque
/// catches; create_hidden_rviz_panel has the same net around the whole of this.
PanelClassFactory *
verified_panel_factory(rviz_common::VisualizationFrame & frame, HiddenRvizPanel & out)
{
  // G1.
  rviz_common::VisualizationManager * const manager_by_binary = frame.getManager();
  if (manager_by_binary == nullptr) {
    out.error = "RViz's main window has no visualization manager (it is not initialised)";
    return nullptr;
  }
  if (FramePanelFactoryAccess::raw_manager(frame) != manager_by_binary) {
    out.error =
      "RViz's main window does not have the memory layout this plugin was built against "
      "(the VisualizationManager read at the header's offset is not the one "
      "VisualizationFrame::getManager() returns); the RViz headers and the installed "
      "rviz_common do not match";
    return nullptr;
  }
  void * const raw = FramePanelFactoryAccess::raw_panel_factory(frame);
  if (raw == nullptr) {
    out.error = "RViz's main window has no panel factory";
    return nullptr;
  }

  // G2, G3, G4.
  const RttiCastResult<PanelClassFactory> cast =
    rtti_cast_from_opaque<rviz_common::Factory, PanelClassFactory>(
    raw, &is_rviz_panel_factory_type_name);
  out.factory_type = cast.dynamic_type_name;
  if (cast.target == nullptr) {
    out.error = "the main window's panel factory failed the run-time type check: " + cast.error;
    return nullptr;
  }
  return cast.target;
}

}  // namespace

HiddenRvizPanel
create_hidden_rviz_panel(rviz_common::DisplayContext * context, const ArCopyPanelClass & cls)
{
  HiddenRvizPanel out;
  out.class_id = cls.class_id;
  try {
    if (context == nullptr) {
      out.error = "there is no RViz display context";
      return out;
    }

    rviz_common::VisualizationFrame * frame = find_frame(context);
    if (frame == nullptr) {
      out.error = "the display context's window manager is not RViz's VisualizationFrame";
      return out;
    }

    // The read of the factory is only as good as the layout it assumes; the layout check
    // and the object's own run-time type say whether the pointer really is RViz's
    // PanelFactory before anything is called through it (see verified_panel_factory).
    PanelClassFactory * factory = verified_panel_factory(*frame, out);
    if (factory == nullptr) {
      return out;
    }

    QString make_error;
    rviz_common::Panel * made = factory->make(QString::fromLatin1(cls.class_id), &make_error);
    if (made == nullptr) {
      out.error = std::string("RViz's panel factory could not make '") + cls.class_id + "'";
      if (!make_error.isEmpty()) {
        out.error += ": " + make_error.toStdString();
      }
      return out;
    }
    // Owned from here on: every early return below deletes it.
    std::unique_ptr<rviz_common::Panel> panel(made);

    // The check that what came back is what was asked for, before anything else touches
    // it. A different class under this id would not have the property tree this code
    // then reaches into.
    out.class_name = panel->metaObject()->className();
    if (out.class_name != cls.qt_class_name) {
      out.error = "the factory made a '" + out.class_name + "' for '" + cls.class_id +
        "', not a '" + cls.qt_class_name + "'; it was deleted unused";
      return out;
    }

    // Binds the panel to the SAME model the real dock of this class shows.
    panel->initialize(context);

    // The one thing different about this copy: no item drag, on every property tree of it. A
    // drag started by synthetic AR pointer input must never enter the operating system's
    // drag-and-drop loop (QDrag::exec -> DoDragDrop on Windows). Not finding a tree is a
    // failure rather than a shrug: without this the copy is not safe to hand to AR, and for the
    // Selection panel (named by its base class) it is also what says the object is the one
    // that was asked for. A class without a property tree (the Help panel) has no drag to
    // switch off; what says the object is the one that was asked for there is its exact class
    // name and the text browser it must hold.
    if (cls.has_property_tree) {
      const auto trees = panel->findChildren<rviz_common::properties::PropertyTreeWidget *>();
      if (trees.isEmpty()) {
        out.error = "the panel has no property tree to switch item dragging off on";
        return out;
      }
      for (rviz_common::properties::PropertyTreeWidget * tree : trees) {
        tree->setDragEnabled(false);
      }
      out.item_drag_disabled = true;
    } else if (panel->findChild<QTextBrowser *>() == nullptr) {
      out.error = "the panel has no text browser to show its text in";
      return out;
    }

    // The tree gets the whole height of the copy: the help area under it (a text browser that
    // the tree's current item fills) is hidden. The copy is a window of its own, so the help
    // area of the dock on the desktop is another widget and stays as it is.
    if (cls.hide_help_area) {
      for (QTextBrowser * help : panel->findChildren<QTextBrowser *>()) {
        help->hide();
        out.help_area_hidden = true;
      }
    }

    // Only a label for debuggers and Qt's own messages: the window is never on screen.
    panel->setWindowTitle(QString::fromStdString(ar_copy_window_title(cls)));

    out.panel = std::move(panel);
  } catch (const std::exception & e) {
    out.panel.reset();
    out.error = std::string("creating it threw: ") + e.what();
  } catch (...) {
    out.panel.reset();
    out.error = "creating it threw an unknown exception";
  }
  return out;
}

HiddenRvizPanel
create_hidden_displays_panel(rviz_common::DisplayContext * context)
{
  const ArCopyPanelClass * cls = ar_copy_panel_class(kDisplaysPanelClassId);
  if (cls == nullptr) {
    HiddenRvizPanel out;
    out.class_id = kDisplaysPanelClassId;
    out.error = "the Displays panel is not in the table of the classes that have an AR copy";
    return out;
  }
  return create_hidden_rviz_panel(context, *cls);
}

void copy_panel_view_state(const rviz_common::Panel & from, rviz_common::Panel & to)
{
  // A Config is a handle on a shared node: the panel saves into it and the copy reads it back.
  rviz_common::Config config;
  from.save(config);
  to.load(config);
}

}  // namespace qar_rviz_streaming
