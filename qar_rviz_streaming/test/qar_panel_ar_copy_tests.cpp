// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the AR copy of the Qar Streaming panel (qar_panel_ar_copy.hpp), offscreen: what the
// panel's own controls show reaches the copy, a press on the copy is carried out on the panel's
// control, the copy has larger controls and the same text, and building, showing and destroying
// it leave the panel's own view alone. The copy sits on a plain widget that stands in for the
// panel: the panel's slots are counting lambdas on the signals of the original controls.

#include "qar_rviz_streaming/qar_panel_ar_copy.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QStyleFactory>
#include <QStringList>
#include <QStyleOptionButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QWidget>

#include "qar_qt_streaming/enlarged_controls_style.hpp"
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/scroll_census.hpp"
#include "qar_rviz_streaming/ar_panel_list_widget.hpp"
#include "qar_rviz_streaming/hub_connection_widget.hpp"

namespace
{

using qar_rviz_streaming::QarPanelArCopy;
using qar_rviz_streaming::QarPanelView;
using qar_rviz_streaming::QarPanelViewInputs;

QarPanelViewInputs make_inputs()
{
  QarPanelViewInputs inputs;
  inputs.spatial_targets = {
    {QStringLiteral("Action Point"), QStringLiteral("The runtime's own intersection."), 1},
    {QStringLiteral("Ground plane"), QStringLiteral("The ray meets z = 0."), 0}};
  inputs.spatial_target_default_id = 1;
  inputs.invite_tool_tip = QStringLiteral("Connect first.");
  return inputs;
}

/// A press and a release at @p local of @p target, delivered the way the AR input does it: to the
/// deepest child of @p root at that point, with that child's own coordinates.
void click_at(QWidget * root, QWidget * target, const QPoint & local)
{
  const QPoint in_root = target->mapTo(root, local);
  QWidget * hit = root->childAt(in_root);
  QPoint p = in_root;
  if (hit == nullptr) {
    hit = root;
  } else {
    p = hit->mapFrom(root, in_root);
  }
  const QPoint global = hit->mapToGlobal(p);
  QMouseEvent press(
    QEvent::MouseButtonPress, p, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(hit, &press);
  QMouseEvent release(
    QEvent::MouseButtonRelease, p, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(hit, &release);
  QCoreApplication::processEvents();
}

QPoint indicator_centre(QCheckBox * box)
{
  QStyleOptionButton option;
  option.initFrom(box);
  return box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box).center();
}

std::map<QWidget *, QRect> geometries(QWidget & root)
{
  std::map<QWidget *, QRect> all;
  for (QWidget * widget : root.findChildren<QWidget *>()) {
    all[widget] = widget->geometry();
  }
  return all;
}

/// The panel's view on a plain widget, the style, and the copy; shown off-screen at its size.
struct Fixture
{
  explicit Fixture(bool styled = true, bool show_desk = false)
  : style(2.0)
  {
    desk.resize(480, 700);
    original = qar_rviz_streaming::build_qar_panel_view(desk, make_inputs());
    if (show_desk) {
      desk.setAttribute(Qt::WA_DontShowOnScreen, true);
      desk.show();
      QCoreApplication::processEvents();
    }
    desk_geometries = geometries(desk);
    copy = std::make_unique<QarPanelArCopy>(original, make_inputs(), styled ? &style : nullptr);
    copy->sync();
    const QSize size = copy->stable_size();
    EXPECT_TRUE(qar_qt_streaming::show_offscreen(copy.get(), size.width(), size.height()));
    QCoreApplication::processEvents();
  }

  /// Shows page @p index of the copy and lets the layout run.
  void show_page(int index)
  {
    copy->view().tabs->setCurrentIndex(index);
    QCoreApplication::processEvents();
  }

  QWidget desk;
  qar_qt_streaming::EnlargedControlsStyle style;
  QarPanelView original;
  std::map<QWidget *, QRect> desk_geometries;
  std::unique_ptr<QarPanelArCopy> copy;
};

TEST(QarPanelArCopy, EveryControlIsBound)
{
  Fixture f;
  EXPECT_TRUE(f.copy->complete());
  EXPECT_EQ(f.copy->bound_count(), qar_rviz_streaming::qar_panel_view_bound_count());
  EXPECT_EQ(f.copy->bound_count(), 53);
  EXPECT_EQ(f.copy->objectName(), QStringLiteral("qar_panel_ar_copy"));
  EXPECT_EQ(f.copy->windowTitle(), QStringLiteral("Qar Streaming (AR copy)"));
}

TEST(QarPanelArCopy, ASettingChangedOnTheDesktopShowsInTheArCopy)
{
  Fixture f;
  const QarPanelView & o = f.original;
  const QarPanelView & c = f.copy->view();

  o.resolution_combo->setCurrentIndex(4);
  o.hidden_displays_checkbox->setChecked(true);
  o.advanced_group->hide();
  o.ar_copy_size_combo->setItemText(0, QStringLiteral("Enlarged controls (1.5x)"));
  o.runtime_state_label->setFullText(QStringLiteral("Qar runtime: RUNNING"));
  o.runtime_state_label->setStyleSheet(QStringLiteral("font-weight: bold; color: #1b7f3b;"));
  o.last_error_label->setStyleSheet(QStringLiteral("color: #b00020;"));
  o.stop_qar_button->setEnabled(false);
  for (const char * text : {"/scan", "/map", "/odom"}) {
    o.exposure_list->addItem(QString::fromLatin1(text));
  }
  o.exposure_list->setCurrentRow(1);

  EXPECT_GT(f.copy->sync(), 0);

  EXPECT_EQ(c.resolution_combo->currentIndex(), 4);
  EXPECT_TRUE(c.hidden_displays_checkbox->isChecked());
  EXPECT_TRUE(c.advanced_group->isHidden());
  EXPECT_EQ(c.ar_copy_size_combo->itemText(0), QStringLiteral("Enlarged controls (1.5x)"));
  EXPECT_EQ(c.runtime_state_label->fullText(), QStringLiteral("Qar runtime: RUNNING"));
  // The weight and the colour of the status strip follow the phase.
  EXPECT_EQ(c.runtime_state_label->styleSheet(), o.runtime_state_label->styleSheet());
  EXPECT_EQ(c.last_error_label->styleSheet(), o.last_error_label->styleSheet());
  EXPECT_NE(c.runtime_state_label->styleSheet().indexOf("bold"), -1);
  EXPECT_FALSE(c.stop_qar_button->isEnabled());
  ASSERT_EQ(c.exposure_list->count(), 3);
  EXPECT_EQ(c.exposure_list->item(2)->text(), QStringLiteral("/odom"));
  EXPECT_EQ(c.exposure_list->currentRow(), 1);
  // Nothing the copy did came back to the panel's controls.
  EXPECT_EQ(o.resolution_combo->currentIndex(), 4);

  // The second pass has nothing to copy.
  EXPECT_EQ(f.copy->sync(), 0);
}

TEST(QarPanelArCopy, TheStatusStripKeepsItsColourWhenThePhaseChanges)
{
  Fixture f;
  const QarPanelView & o = f.original;
  const QarPanelView & c = f.copy->view();
  const QString running = QStringLiteral("font-weight: bold; color: #1b7f3b;");
  const QString lost = QStringLiteral("font-weight: bold; color: #c46a00;");

  o.runtime_state_label->setStyleSheet(running);
  f.copy->sync();
  EXPECT_EQ(c.runtime_state_label->styleSheet(), running);
  o.runtime_state_label->setStyleSheet(lost);
  f.copy->sync();
  EXPECT_EQ(c.runtime_state_label->styleSheet(), lost);
  // A style sheet that was cleared is cleared on the copy, and a pass with nothing to copy
  // writes nothing.
  o.runtime_state_label->setStyleSheet(QString());
  f.copy->sync();
  EXPECT_TRUE(c.runtime_state_label->styleSheet().isEmpty());
  EXPECT_EQ(f.copy->sync(), 0);
}

TEST(QarPanelArCopy, ACheckBoxClickedInTheArCopyChangesTheDesktopAndItsSlotRunsOnce)
{
  Fixture f;
  f.show_page(4);  // Settings
  int toggled = 0;
  QObject::connect(
    f.original.show_advanced_checkbox, &QCheckBox::toggled, [&toggled](bool) {++toggled;});
  QCheckBox * box = f.copy->view().show_advanced_checkbox;
  ASSERT_TRUE(box->isVisible());

  click_at(f.copy.get(), box, indicator_centre(box));

  EXPECT_TRUE(f.original.show_advanced_checkbox->isChecked());
  EXPECT_EQ(toggled, 1);
  EXPECT_EQ(f.copy->sync(), 0);
  EXPECT_TRUE(box->isChecked());
  EXPECT_EQ(toggled, 1);
}

TEST(QarPanelArCopy, AComboChosenInTheArCopySetsTheDesktopIndex)
{
  Fixture f;
  int changed = 0;
  QObject::connect(
    f.original.fps_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    [&changed](int) {++changed;});
  ASSERT_EQ(f.original.fps_combo->currentIndex(), 2);
  f.copy->sync();

  // The choice of the popup sets the index of the copy's combo and ends in this signal.
  f.copy->view().fps_combo->setCurrentIndex(4);
  QMetaObject::invokeMethod(f.copy->view().fps_combo, "activated", Q_ARG(int, 4));

  EXPECT_EQ(f.original.fps_combo->currentIndex(), 4);
  EXPECT_EQ(changed, 1);
  EXPECT_EQ(f.copy->sync(), 0);
  EXPECT_EQ(f.copy->view().fps_combo->currentIndex(), 4);
}

TEST(QarPanelArCopy, AButtonClickedInTheArCopyRunsTheDesktopSlot)
{
  Fixture f;
  f.show_page(2);  // AR tools
  int clicks = 0;
  QObject::connect(f.original.add_topic_button, &QPushButton::clicked, [&clicks]() {++clicks;});
  QPushButton * button = f.copy->view().add_topic_button;
  ASSERT_TRUE(button->isVisible());

  click_at(f.copy.get(), button, button->rect().center());

  EXPECT_EQ(clicks, 1);
}

TEST(QarPanelArCopy, AnExposureRowClickedInTheArCopyIsTheDesktopCurrentRow)
{
  Fixture f;
  for (const char * text : {"/scan", "/map", "/odom"}) {
    f.original.exposure_list->addItem(QString::fromLatin1(text));
  }
  f.copy->sync();
  f.show_page(2);  // AR tools
  int changed = 0;
  QObject::connect(
    f.original.exposure_list, &QListWidget::currentItemChanged,
    [&changed](QListWidgetItem *, QListWidgetItem *) {++changed;});
  QListWidget * list = f.copy->view().exposure_list;
  ASSERT_EQ(list->count(), 3);
  ASSERT_TRUE(list->isVisible());

  click_at(f.copy.get(), list->viewport(), list->visualItemRect(list->item(1)).center());

  EXPECT_EQ(f.original.exposure_list->currentRow(), 1);
  EXPECT_EQ(changed, 1);
  EXPECT_EQ(f.copy->sync(), 0);
  EXPECT_EQ(list->currentRow(), 1);
}

/// @p count panel lines, each one a little wider than @p width_px of the box's font: one line in a
/// box of that width wraps to two.
QStringList long_panel_lines(int count, const QFontMetrics & metrics, int width_px)
{
  QStringList lines;
  for (int i = 0; i < count; ++i) {
    QString line = QStringLiteral("dock:Panel %1: 1920x1080 px, sent 123456").arg(i);
    while (metrics.horizontalAdvance(line) <= width_px + width_px / 5) {
      line += QStringLiteral(", copy sync 12/34");
    }
    lines.append(line);
  }
  return lines;
}

/// A view built into @p root, shown off-screen at @p width, on its diagnostics page.
QarPanelView shown_view(QWidget & root, int width)
{
  root.resize(width, 700);
  QarPanelView view = qar_rviz_streaming::build_qar_panel_view(root, make_inputs());
  root.setAttribute(Qt::WA_DontShowOnScreen, true);
  root.show();
  view.tabs->setCurrentIndex(view.tabs->count() - 1);
  for (int i = 0; i < 3; ++i) {
    QCoreApplication::processEvents();
  }
  return view;
}

TEST(QarPanelArCopy, TheReadoutIsShortenedForTheWidthOfEachView)
{
  // The narrow view is as narrow as its widest page lets it be (a page that is wider than the
  // view would scroll sideways and its boxes would not show their own width).
  int narrow_width = 0;
  {
    QWidget probe_root;
    const QarPanelView probe = shown_view(probe_root, 600);
    for (const QWidget * page : probe.page_contents) {
      narrow_width = std::max(narrow_width, page->minimumSizeHint().width());
    }
  }
  narrow_width += 120;
  QWidget wide_root;
  QWidget narrow_root;
  const QarPanelView wide = shown_view(wide_root, narrow_width + 900);
  const QarPanelView narrow = shown_view(narrow_root, narrow_width);
  ASSERT_GT(
    qar_rviz_streaming::readout_text_width(wide), qar_rviz_streaming::readout_text_width(narrow));

  const QStringList head{QStringLiteral("AR panels:")};
  const QString more = QStringLiteral("... and %1 more panel(s); all counters are in the log");
  const int narrow_px = qar_rviz_streaming::readout_text_width(narrow);
  ASSERT_LT(narrow_px + narrow_px / 5, qar_rviz_streaming::readout_text_width(wide));
  const QStringList items =
    long_panel_lines(40, QFontMetrics(narrow.ar_panel_status_label->font()), narrow_px);
  const int dropped_wide = qar_rviz_streaming::set_view_readout(wide, head, items, more);
  const int dropped_narrow = qar_rviz_streaming::set_view_readout(narrow, head, items, more);

  EXPECT_GT(dropped_narrow, dropped_wide);
  // The narrow box says how many lines it left out, whatever the other box shows.
  const QString left_out = QStringLiteral("%1 more panel").arg(dropped_narrow);
  EXPECT_NE(narrow.ar_panel_status_label->text().indexOf(left_out), -1);
}

TEST(QarPanelArCopy, TheReadoutOfTheCopyIsNotOverwrittenByTheDesktopText)
{
  Fixture f;
  const QString more = QStringLiteral("... and %1 more panel(s); all counters are in the log");
  qar_rviz_streaming::set_view_readout(
    f.original, {QStringLiteral("AR panels: desktop")}, QStringList(), more);
  qar_rviz_streaming::set_view_readout(
    f.copy->view(), {QStringLiteral("AR panels: copy")}, QStringList(), more);

  f.copy->sync();

  EXPECT_EQ(f.original.ar_panel_status_label->text(), QStringLiteral("AR panels: desktop"));
  EXPECT_EQ(f.copy->view().ar_panel_status_label->text(), QStringLiteral("AR panels: copy"));
  EXPECT_EQ(f.copy->sync(), 0);
  EXPECT_EQ(f.copy->bound_count(), qar_rviz_streaming::qar_panel_view_bound_count());
}

TEST(QarPanelArCopy, TheTabsAreIndependent)
{
  Fixture f;
  f.copy->view().tabs->setCurrentIndex(3);
  EXPECT_EQ(f.original.tabs->currentIndex(), 0);
  f.original.tabs->setCurrentIndex(2);
  f.copy->sync();
  EXPECT_EQ(f.copy->view().tabs->currentIndex(), 3);
  EXPECT_EQ(f.original.tabs->currentIndex(), 2);
}

TEST(QarPanelArCopy, ItsControlsAreLargerAndItsTextIsAQuarterLarger)
{
  Fixture f;
  std::unique_ptr<QStyle> plain(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(plain, nullptr);
  QCheckBox * box = f.copy->view().show_advanced_checkbox;
  const int base = plain->pixelMetric(QStyle::PM_IndicatorWidth);
  EXPECT_NEAR(box->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, box), 2 * base, 1);
  EXPECT_NEAR(box->style()->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, box), 2 * base, 1);

  // The text is the font of the panel's control, a quarter larger and not hinted.
  const QLabel * original_label = f.original.exposure_status_label;
  const QLabel * copy_label = f.copy->view().exposure_status_label;
  EXPECT_DOUBLE_EQ(copy_label->font().pointSizeF(), original_label->font().pointSizeF() * 1.25);
  EXPECT_EQ(copy_label->font().hintingPreference(), QFont::PreferNoHinting);
  EXPECT_GT(
    QFontMetrics(copy_label->font()).height(), QFontMetrics(original_label->font()).height());
}

TEST(QarPanelArCopy, ItsListsAreMeasuredUnderItsStyle)
{
  Fixture f;
  const QarPanelView & o = f.original;
  const QarPanelView & c = f.copy->view();

  // The Hub's target list and the list of AR panels measure their rows in their constructors.
  EXPECT_GT(
    c.hub_widget->target_list()->maximumHeight(),
    o.hub_widget->target_list()->maximumHeight());
  const QTreeWidget * original_tree = o.ar_panel_list->findChild<QTreeWidget *>();
  const QTreeWidget * copy_tree = c.ar_panel_list->findChild<QTreeWidget *>();
  ASSERT_NE(original_tree, nullptr);
  ASSERT_NE(copy_tree, nullptr);
  EXPECT_GT(copy_tree->maximumHeight(), original_tree->maximumHeight());

  // The topic list: two probe rows and the frame.
  QListWidget * topics = c.topic_watch_list;
  topics->addItem(QStringLiteral("Mg"));
  const int row = topics->sizeHintForRow(0);
  topics->clear();
  EXPECT_EQ(topics->minimumHeight(), row * 2 + 2 * topics->frameWidth());
  EXPECT_GT(topics->minimumHeight(), o.topic_watch_list->minimumHeight());
}

TEST(QarPanelArCopy, ItsStableSizeLeavesNoPageWithoutRoom)
{
  Fixture f;
  const QSize size = f.copy->stable_size();
  EXPECT_GT(size.width(), 0);
  EXPECT_GT(size.height(), 0);
  EXPECT_LE(size.height(), qar_rviz_streaming::kPanelArCopyMaxHeightPx);
  EXPECT_EQ(f.copy->size(), size);
  for (int i = 0; i < 3; ++i) {
    QCoreApplication::processEvents();
  }
  EXPECT_LE(f.copy->shortfall().px, 0) << f.copy->shortfall().page;
}

TEST(QarPanelArCopy, BuildingItLeavesTheDesktopViewAlone)
{
  Fixture f(true, true);
  QCoreApplication::processEvents();
  f.copy->sync();
  f.show_page(2);
  EXPECT_EQ(geometries(f.desk), f.desk_geometries);
  for (QWidget * widget : f.desk.findChildren<QWidget *>()) {
    EXPECT_NE(widget->style(), static_cast<QStyle *>(&f.style));
  }
  EXPECT_EQ(f.desk.style(), QApplication::style());
}

TEST(QarPanelArCopy, ItIsAnOffScreenPanel)
{
  Fixture f;
  EXPECT_TRUE(qar_qt_streaming::is_offscreen_panel(f.copy.get()));
  EXPECT_TRUE(f.copy->isVisible());
  EXPECT_FALSE(f.copy->isActiveWindow());
  EXPECT_EQ(f.copy->parentWidget(), nullptr);
}

TEST(QarPanelArCopy, DestroyingItLeavesTheDesktopUntouched)
{
  Fixture f(true, true);
  const QarPanelView & o = f.original;
  int calls = 0;
  const auto count = [&calls]() {++calls;};
  QObject::connect(o.show_advanced_checkbox, &QCheckBox::toggled, count);
  QObject::connect(o.start_qar_button, &QPushButton::clicked, count);
  QObject::connect(o.fps_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), count);
  QObject::connect(o.exposure_list, &QListWidget::currentRowChanged, count);
  QObject::connect(o.tabs, &QTabWidget::currentChanged, count);
  QObject::connect(o.spatial_mode_combo, QOverload<int>::of(&QComboBox::activated), count);

  f.copy.reset();
  QCoreApplication::processEvents();

  EXPECT_EQ(calls, 0);
  EXPECT_EQ(geometries(f.desk), f.desk_geometries);
}

TEST(QarPanelArCopy, ANativeCopyHasNoStyle)
{
  Fixture f(false);
  for (QWidget * widget : f.copy->findChildren<QWidget *>()) {
    EXPECT_EQ(widget->style(), QApplication::style()) << widget->metaObject()->className();
  }
  const int native_topic_minimum = f.copy->view().topic_watch_list->minimumHeight();

  // Another style and back: every widget is on the application's style again, and the lists that
  // were measured by their rows are measured again.
  f.copy->set_style(&f.style);
  for (QWidget * widget : f.copy->findChildren<QWidget *>()) {
    EXPECT_EQ(widget->style(), static_cast<QStyle *>(&f.style))
      << widget->metaObject()->className();
  }
  EXPECT_GT(f.copy->view().topic_watch_list->minimumHeight(), native_topic_minimum);
  f.copy->set_style(nullptr);
  for (QWidget * widget : f.copy->findChildren<QWidget *>()) {
    EXPECT_EQ(widget->style(), QApplication::style()) << widget->metaObject()->className();
  }
  EXPECT_EQ(f.copy->view().topic_watch_list->minimumHeight(), native_topic_minimum);
}

/// The text of a full readout: the two lines of totals and more panel lines than the box holds.
void fill_readout(const qar_rviz_streaming::ReadoutBox & box, int width_px)
{
  QStringList panels;
  for (int i = 0; i < 40; ++i) {
    panels.append(QStringLiteral("dock:Panel %1: 1920x1080 px, sent 123456").arg(i));
  }
  qar_rviz_streaming::set_readout_text(
    box, {QStringLiteral("input: in 0, missed 0, dropped 0"),
      QStringLiteral("panels: live=40 closed=0 (destroyed) notice=0")},
    panels, QStringLiteral("... and %1 more panel(s); all counters are in the log"), width_px);
  QCoreApplication::processEvents();
}

TEST(QarPanelArCopy, TheReadoutBoxHoldsTenLinesAndTheDiagnosticsPageNeedsNoScroll)
{
  // The panel's own view at its stable height.
  {
    QWidget root;
    root.resize(480, 700);
    const QarPanelView view = qar_rviz_streaming::build_qar_panel_view(root, make_inputs());
    const qar_rviz_streaming::StableHints hints = qar_rviz_streaming::compute_stable_hints(
      *view.tabs, view.page_contents, *view.status_strip, 900, 0);
    root.resize(std::max(480, hints.hint.width()), hints.hint.height());
    root.setAttribute(Qt::WA_DontShowOnScreen, true);
    root.show();
    view.tabs->setCurrentIndex(view.tabs->count() - 1);
    QCoreApplication::processEvents();
    fill_readout(
      {view.ar_panel_status_scroll, view.ar_panel_status_label},
      view.ar_panel_status_label->width());
    EXPECT_EQ(
      view.ar_panel_status_scroll->height(),
      view.ar_panel_status_scroll->fontMetrics().lineSpacing() * 10 + 8);
    EXPECT_EQ(qar_qt_streaming::take_scroll_census(root).stacked_overflow_px, 0);
    EXPECT_EQ(qar_rviz_streaming::page_shortfall(*view.tabs, view.page_contents).px, 0);
  }
  // The AR copy at the size it asks for.
  Fixture f;
  f.show_page(f.copy->view().tabs->count() - 1);
  fill_readout(
    {f.copy->view().ar_panel_status_scroll, f.copy->view().ar_panel_status_label},
    f.copy->view().ar_panel_status_label->width());
  EXPECT_EQ(
    f.copy->view().ar_panel_status_scroll->height(),
    f.copy->view().ar_panel_status_scroll->fontMetrics().lineSpacing() * 10 + 8);
  EXPECT_EQ(qar_qt_streaming::take_scroll_census(*f.copy).stacked_overflow_px, 0);
  EXPECT_EQ(f.copy->shortfall().px, 0);
}

TEST(QarPanelArCopy, TheEnlargedCopyHasAQuarterMoreTextAndNativeRestoresTheFont)
{
  Fixture f(false);
  const QFont native_font = f.copy->font();
  const double native_point = f.copy->view().tabs->font().pointSizeF();
  ASSERT_GT(native_point, 0.0);
  EXPECT_EQ(native_font.hintingPreference(), QApplication::font().hintingPreference());

  f.copy->set_style(&f.style);
  EXPECT_DOUBLE_EQ(f.copy->font().pointSizeF(), native_font.pointSizeF() * 1.25);
  EXPECT_EQ(f.copy->font().hintingPreference(), QFont::PreferNoHinting);
  // A widget without a font of its own follows the root.
  EXPECT_DOUBLE_EQ(f.copy->view().tabs->font().pointSizeF(), native_point * 1.25);
  EXPECT_EQ(f.copy->view().tabs->font().hintingPreference(), QFont::PreferNoHinting);

  // A second style does not compound.
  f.copy->set_style(&f.style);
  EXPECT_DOUBLE_EQ(f.copy->font().pointSizeF(), native_font.pointSizeF() * 1.25);

  f.copy->set_style(nullptr);
  EXPECT_DOUBLE_EQ(f.copy->font().pointSizeF(), native_font.pointSizeF());
  EXPECT_DOUBLE_EQ(f.copy->view().tabs->font().pointSizeF(), native_point);
  EXPECT_EQ(f.copy->font().hintingPreference(), native_font.hintingPreference());
  EXPECT_FALSE(f.copy->testAttribute(Qt::WA_SetFont));
}

TEST(QarPanelArCopy, AShownCopyThatIsRestyledKeepsEveryFormLabelAsTallAsItsField)
{
  // The text is set before the style, so the style centres each label against the size hint that
  // the field has with the larger font.
  Fixture f(false);
  f.copy->set_style(&f.style);
  QCoreApplication::processEvents();
  int rows = 0;
  for (QFormLayout * form : f.copy->findChildren<QFormLayout *>()) {
    for (int row = 0; row < form->rowCount(); ++row) {
      QLayoutItem * label_item = form->itemAt(row, QFormLayout::LabelRole);
      QLayoutItem * field_item = form->itemAt(row, QFormLayout::FieldRole);
      if (label_item == nullptr || field_item == nullptr || label_item->widget() == nullptr ||
        field_item->widget() == nullptr || !field_item->widget()->isVisible())
      {
        continue;
      }
      auto * label = qobject_cast<QLabel *>(label_item->widget());
      if (label == nullptr || !label->isVisible()) {
        continue;
      }
      ++rows;
      EXPECT_EQ(label->height(), field_item->widget()->height()) << label->text().toStdString();
      EXPECT_GE(label->minimumHeight(), field_item->sizeHint().height())
        << label->text().toStdString();
      EXPECT_TRUE(label->alignment() & Qt::AlignVCenter) << label->text().toStdString();
    }
  }
  EXPECT_GT(rows, 0) << "the copy has form rows on its shown page";

  // Another restyle, and the way back, keep the rows as tall as their fields as well.
  f.copy->set_style(nullptr);
  f.copy->set_style(&f.style);
  QCoreApplication::processEvents();
  for (QFormLayout * form : f.copy->findChildren<QFormLayout *>()) {
    for (int row = 0; row < form->rowCount(); ++row) {
      QLayoutItem * label_item = form->itemAt(row, QFormLayout::LabelRole);
      QLayoutItem * field_item = form->itemAt(row, QFormLayout::FieldRole);
      if (label_item != nullptr && field_item != nullptr && label_item->widget() != nullptr &&
        field_item->widget() != nullptr && label_item->widget()->isVisible() &&
        field_item->widget()->isVisible())
      {
        EXPECT_EQ(label_item->widget()->height(), field_item->widget()->height());
        EXPECT_GE(label_item->widget()->minimumHeight(), field_item->sizeHint().height());
      }
    }
  }
}

TEST(QarPanelArCopy, TheEnvironmentChoosesTheTextScaleAndTheHinting)
{
  qputenv("QAR_PANEL_AR_COPY_TEXT_SCALE", "1.5");
  qputenv("QAR_PANEL_AR_COPY_FONT_HINTING", "default");
  {
    Fixture f(false);
    const double base = f.copy->font().pointSizeF();
    f.copy->set_style(&f.style);
    EXPECT_DOUBLE_EQ(f.copy->font().pointSizeF(), base * 1.5);
    EXPECT_EQ(f.copy->font().hintingPreference(), QFont::PreferDefaultHinting);
    const qar_rviz_streaming::ArCopyText text =
      qar_rviz_streaming::ar_copy_text_from_environment(true);
    EXPECT_DOUBLE_EQ(text.scale, 1.5);
    EXPECT_EQ(text.hinting, qar_rviz_streaming::ArCopyHinting::Default);
  }
  qunsetenv("QAR_PANEL_AR_COPY_TEXT_SCALE");
  qunsetenv("QAR_PANEL_AR_COPY_FONT_HINTING");
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::ar_copy_text_from_environment(false).scale, 1.0);
}

/// A panel of the kind RViz's Help panel is: a text browser in a vertical layout, nothing else.
struct TextPanel
{
  explicit TextPanel(int width, int height)
  {
    auto * layout = new QVBoxLayout(&panel);
    browser = new QTextBrowser();
    layout->addWidget(browser);
    QString text;
    for (int i = 0; i < 200; ++i) {
      text += QString("Line %1 of the help text that is long enough to need a scroll bar.<br>")
        .arg(i);
    }
    browser->setHtml(text);
    panel.resize(width, height);
  }

  QWidget panel;
  QTextBrowser * browser = nullptr;
};

TEST(ArCopyOfATextPanel, HasTheEnlargedStyleAndTheTextOfACopyAndGivesThemBackAtNative)
{
  qar_qt_streaming::EnlargedControlsStyle style(2.0);
  TextPanel dock(300, 220);
  TextPanel copy(600, 640);
  dock.panel.show();
  QCoreApplication::processEvents();
  const QSize dock_size = dock.panel.size();
  const double dock_point = dock.panel.font().pointSizeF();
  const double native_point = copy.panel.font().pointSizeF();
  ASSERT_GT(native_point, 0.0);

  // What the host does for a copy of a dock: the text first, then the style on the whole tree.
  qar_rviz_streaming::set_ar_copy_text(copy.panel, true);
  qar_qt_streaming::apply_style_to_tree(&copy.panel, &style);
  EXPECT_DOUBLE_EQ(copy.panel.font().pointSizeF(), native_point * 1.25);
  EXPECT_DOUBLE_EQ(copy.browser->font().pointSizeF(), native_point * 1.25);
  EXPECT_EQ(copy.browser->style(), &style);
  EXPECT_EQ(copy.browser->verticalScrollBar()->style(), &style);
  EXPECT_NE(dock.browser->style(), &style) << "the dock on the desktop is not styled";
  EXPECT_DOUBLE_EQ(dock.panel.font().pointSizeF(), dock_point);
  EXPECT_EQ(dock.panel.size(), dock_size);

  // The copy has its own scroll position: scrolling it does not move the dock's browser.
  copy.panel.show();
  QCoreApplication::processEvents();
  copy.browser->verticalScrollBar()->setValue(copy.browser->verticalScrollBar()->maximum());
  EXPECT_GT(copy.browser->verticalScrollBar()->value(), 0);
  EXPECT_EQ(dock.browser->verticalScrollBar()->value(), 0);

  // A text browser scrolls by design, so a fit of the copy never grows it for its text.
  const qar_qt_streaming::ScrollCensus census = qar_qt_streaming::take_scroll_census(copy.panel);
  ASSERT_FALSE(census.areas.empty());
  for (const auto & area : census.areas) {
    EXPECT_TRUE(area.by_design) << area.class_name;
  }

  // Native: the style and the font go back, the copy and its place in the text stay (the number
  // of the bar follows the smaller text, so it is not compared, only that it is still scrolled).
  qar_rviz_streaming::set_ar_copy_text(copy.panel, false);
  qar_qt_streaming::remove_style_from_tree(&copy.panel);
  EXPECT_NE(copy.browser->style(), &style);
  EXPECT_DOUBLE_EQ(copy.panel.font().pointSizeF(), native_point);
  EXPECT_GT(copy.browser->verticalScrollBar()->value(), 0);
  EXPECT_EQ(dock.browser->verticalScrollBar()->value(), 0);

  // And back to enlarged without compounding the text.
  qar_rviz_streaming::set_ar_copy_text(copy.panel, true);
  qar_qt_streaming::apply_style_to_tree(&copy.panel, &style);
  EXPECT_DOUBLE_EQ(copy.panel.font().pointSizeF(), native_point * 1.25);
  EXPECT_EQ(copy.browser->style(), &style);
}

/// RViz parents its colour picker (QColorDialog) and its input boxes (QInputDialog) into the panel
/// that opened them. Below an AR copy such a window gets the style of the copy from the spreader,
/// but it does not take the copy's font: ArDialogCopyText gives it the text of the copy.
TEST(ArDialogCopyText, GivesADialogMadeBelowACopyItsTextAndTakesItBack)
{
  qar_qt_streaming::EnlargedControlsStyle style(2.0);
  TextPanel copy(600, 640);
  TextPanel plain(300, 220);
  const double native_point = copy.panel.font().pointSizeF();
  ASSERT_GT(native_point, 0.0);

  qar_rviz_streaming::set_ar_copy_text(copy.panel, true);
  qar_qt_streaming::apply_style_to_tree(&copy.panel, &style);

  QColorDialog color(Qt::white, &copy.panel);
  QInputDialog input(&copy.panel);
  QColorDialog unstyled(Qt::white, &plain.panel);

  // The window decides: below the copy it is drawn with the style (the controls x2, once),
  // below a plain panel it is not.
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&color), &style);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&input), &style);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&unstyled), nullptr);
  const auto spins = color.findChildren<QSpinBox *>();
  ASSERT_FALSE(spins.isEmpty());
  for (QSpinBox * spin : spins) {
    EXPECT_EQ(spin->style(), &style);
  }
  EXPECT_DOUBLE_EQ(qar_qt_streaming::enlarged_controls_style_of(&color)->factor(), 2.0);

  // A window does not take the font of its parent: the copy's text is not there yet.
  const double dialog_point = color.font().pointSizeF();
  ASSERT_GT(dialog_point, 0.0);
  EXPECT_DOUBLE_EQ(dialog_point, unstyled.font().pointSizeF());

  qar_rviz_streaming::ArDialogCopyText text;
  EXPECT_DOUBLE_EQ(text.text_scale_of(color), 0.0);
  EXPECT_TRUE(text.give(color));
  EXPECT_TRUE(text.give(input));
  EXPECT_FALSE(text.give(unstyled)) << "a window without the style is never touched";
  EXPECT_FALSE(text.give(color)) << "offered once";
  EXPECT_DOUBLE_EQ(color.font().pointSizeF(), dialog_point * 1.25);
  EXPECT_DOUBLE_EQ(input.font().pointSizeF(), dialog_point * 1.25);
  EXPECT_DOUBLE_EQ(unstyled.font().pointSizeF(), dialog_point);
  EXPECT_DOUBLE_EQ(spins.first()->font().pointSizeF(), dialog_point * 1.25)
    << "the controls inside follow the window's font";
  EXPECT_DOUBLE_EQ(text.text_scale_of(color), 1.25);
  EXPECT_DOUBLE_EQ(text.text_scale_of(unstyled), 0.0);

  // The copy goes native: the spreader takes the style off the windows, the text follows.
  qar_rviz_streaming::set_ar_copy_text(copy.panel, false);
  qar_qt_streaming::remove_style_from_tree(&copy.panel);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&color), nullptr);
  EXPECT_EQ(text.sync(), 2);
  EXPECT_DOUBLE_EQ(color.font().pointSizeF(), dialog_point);
  EXPECT_DOUBLE_EQ(input.font().pointSizeF(), dialog_point);
  EXPECT_DOUBLE_EQ(text.text_scale_of(color), 0.0);
  EXPECT_EQ(text.sync(), 0);

  // Enlarged again: the text comes back once, without compounding.
  qar_rviz_streaming::set_ar_copy_text(copy.panel, true);
  qar_qt_streaming::apply_style_to_tree(&copy.panel, &style);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&color), &style);
  EXPECT_EQ(text.sync(), 2);
  EXPECT_DOUBLE_EQ(color.font().pointSizeF(), dialog_point * 1.25);
  EXPECT_DOUBLE_EQ(text.text_scale_of(color), 1.25);

  // A window that is gone is forgotten.
  auto * gone = new QInputDialog(&copy.panel);
  EXPECT_TRUE(text.give(*gone));
  delete gone;
  EXPECT_EQ(text.sync(), 0);
}

}  // namespace
