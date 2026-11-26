// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__QAR_PANEL_AR_COPY_HPP_
#define QAR_RVIZ_STREAMING__QAR_PANEL_AR_COPY_HPP_

/// @file qar_panel_ar_copy.hpp
/// @brief The AR copy of the Qar Streaming panel: a second view of the panel, built by the same
/// function under the enlarged-controls style, that is shown off-screen only. Qt Widgets and the
/// panel's layout helpers only: no RViz, no ROS, no QAROS.
///
/// The copy owns no state of the panel. The panel's own controls stay the only owner: before each
/// capture sync() copies what they show to the copy, and a click, a choice, a step or an edit on
/// a control of the copy is carried out on the panel's control (see WidgetTwinBinder), so the
/// panel's slots run as they do for a user of the panel's own window. The tabs and the scroll
/// positions of the copy are its own.

#include <QPointer>
#include <QSize>
#include <QStyle>
#include <QWidget>

#include <vector>

#include "qar_qt_streaming/widget_twin.hpp"
#include "qar_rviz_streaming/ar_copy_scale.hpp"
#include "qar_rviz_streaming/qar_panel_view.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"

namespace qar_rviz_streaming
{

/// The least and the most height of the AR copy of the panel when it is fitted.
constexpr int kPanelArCopyMinHeightPx = 400;
constexpr int kPanelArCopyMaxHeightPx = 1400;

/// @brief Gives the root of an enlarged AR copy its text size and hinting: the font of @p root is
/// its font as it was before, @p scale times as large (a point or a pixel size), with the hinting
/// of @p hinting; every widget below that has no font of its own follows. The font before is kept
/// on @p root, so a second call starts from it again and does not compound. Qt GUI thread only.
void apply_ar_copy_text(QWidget & root, double scale, ArCopyHinting hinting);

/// @brief Puts the font of @p root back as it was before apply_ar_copy_text(); nothing happens
/// for a root that was not given one.
void remove_ar_copy_text(QWidget & root);

/// What the text of a copy root was given.
struct ArCopyText
{
  double scale = 1.0;
  ArCopyHinting hinting = ArCopyHinting::Default;
};

/// @brief The text of an enlarged copy (@ref ar_copy_text_scale, @ref ar_copy_font_hinting of the
/// environment, read on each call and never written) or, for @p enlarged false, scale 1 with the
/// default hinting.
ArCopyText ar_copy_text_from_environment(bool enlarged);

/// @brief Gives the root of a copy the text of the environment (@ref ar_copy_text_scale,
/// @ref ar_copy_font_hinting) when @p enlarged, else puts its font back (scale 1, default
/// hinting). The environment is read on each call and never written. @return what was set.
ArCopyText set_ar_copy_text(QWidget & root, bool enlarged);

/// @brief The text of the dialog windows that were made below an AR copy.
///
/// The style of an AR copy reaches every window made below the copy (the spreader of
/// qar_qt_streaming::apply_style_to_tree), but a window does not take the font of its parent, so
/// such a dialog (RViz parents its colour picker and its input boxes into the panel that opened
/// them) would show the large controls of the copy next to the text of the desktop. This gives
/// such a window the text of set_ar_copy_text(), and takes it away when the style is taken off the
/// window again. A window not drawn with the enlarged-controls style is never touched. Qt GUI
/// thread only.
class ArDialogCopyText
{
public:
  /// @brief Gives @p dialog the text of an enlarged copy when it is drawn with the
  /// enlarged-controls style and has not been offered the text before; no-op otherwise.
  /// @return true when the text was given now.
  bool give(QWidget & dialog);

  /// @brief Follows the style of the windows that were offered the text: one whose style was
  /// taken off gets its font back, one that has the style again gets the text again. Windows
  /// that are gone are forgotten.
  /// @return the number of windows whose text changed.
  int sync();

  /// The factor of the text @p dialog has now, or 0 when it has none (never offered, or the text
  /// was taken away).
  double text_scale_of(const QWidget & dialog) const;

private:
  struct Entry
  {
    QPointer<QWidget> dialog;
    double scale = 1.0;
    bool applied = false;
  };
  std::vector<Entry> entries_;
};

/// @brief The AR copy of the panel. Parentless; the panel keeps it for its own life. It is not
/// shown here: the caller shows it with qar_qt_streaming::show_offscreen(). Qt GUI thread only.
class QarPanelArCopy : public QWidget
{
public:
  /// @param original the controls of the panel that the copy shows and acts on; the widgets must
  ///        outlive the copy (or the caller calls binder().release() before they die).
  /// @param inputs what the builder needs; rows_by_style is set by the copy.
  /// @param style the style of the copy, or null for the application's style; not owned, it must
  ///        outlive the copy. It is given to the root BEFORE the view is built, so every widget,
  ///        also those that measure themselves in their constructors, is made under it. A style
  ///        also gives the root the text of set_ar_copy_text(); no style leaves its font alone.
  QarPanelArCopy(
    const QarPanelView & original, const QarPanelViewInputs & inputs, QStyle * style);
  /// Ends the binding first, then the widgets of the copy go.
  ~QarPanelArCopy() override;

  /// The controls of the copy.
  const QarPanelView & view() const {return view_;}

  /// True when every pair was bound (bound_count() == qar_panel_view_bound_count()).
  bool complete() const;
  /// The number of pairs that were bound.
  int bound_count() const {return bound_;}

  /// One pass of the binder: what the panel shows is copied to the copy. @return the writes.
  int sync();
  qar_qt_streaming::WidgetTwinBinder & binder() {return binder_;}

  /// The size the copy asks for: the widest page, the tab bar and the tallest page at their
  /// smallest, at most kPanelArCopyMaxHeightPx high. Measured on the first call and kept; call it
  /// after the first sync().
  QSize stable_size() const;

  /// What the pages of the copy lack at its current size (zero when none needs a scroll bar).
  PageShortfall shortfall() const;

  /// Gives the copy another style (null: the application's; the text follows as the constructor
  /// says) and measures the lists that were measured by their rows again. The composite lists
  /// (the AR panels, the Hub targets) keep the height they were built with. The caller fits the
  /// copy again afterwards.
  void set_style(QStyle * style);

private:
  QarPanelView view_;
  qar_qt_streaming::WidgetTwinBinder binder_;
  int bound_ = 0;
  mutable QSize stable_size_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__QAR_PANEL_AR_COPY_HPP_
