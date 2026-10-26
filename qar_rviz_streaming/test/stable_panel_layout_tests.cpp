// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the tabbed layout's building blocks (stable_panel_layout.hpp). The panel itself needs
// RViz, so these tests build a stand-in panel from the same pieces, the way the panel does: a
// status strip over a tab widget whose pages are scroll pages, with the hints of the stand-in
// measured once and returned from sizeHint() and minimumSizeHint().
//
// What must hold is that nothing a page shows, hides, enables or rewrites, and no tab switch,
// moves a size the outside world reads: the stand-in's hints, the hints of the layout that holds
// the strip and the tab widget, and the strip's height. The offscreen platform plugin runs
// headless and rasterizes no glyphs, and its font is not the desktop's, so no test asserts on a
// pixel or on an absolute metric: every assertion compares a size before and after.

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "qar_rviz_streaming/stable_panel_layout.hpp"

using qar_rviz_streaming::ElidedLabel;
using qar_rviz_streaming::StableHints;
using qar_rviz_streaming::StatusStrip;

namespace
{

std::string text_of(const QSize & size)
{
  return std::to_string(size.width()) + "x" + std::to_string(size.height());
}

/// Many short words: a text a wrapped label breaks into many lines.
QString many_words()
{
  QString text;
  for (int i = 0; i < 80; ++i) {
    text += QString("word%1 ").arg(i);
  }
  return text;
}

/// One word with no break in it: a text no wrapped label can break.
QString one_long_word()
{
  return QString(500, QLatin1Char('w'));
}

/// The stand-in panel: the same pieces in the same order as the panel's buildUi().
class Skeleton : public QWidget
{
public:
  Skeleton()
  {
    auto * root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    strip = qar_rviz_streaming::make_status_strip(this);
    strip.first->setFullText("Qar runtime: STOPPED");
    strip.second->setFullText("Last error: <none>");
    root->addWidget(strip.frame);
    tabs = qar_rviz_streaming::make_tab_widget(this);
    root->addWidget(tabs, 1);

    // Connection: a row of buttons, a group that only one mode shows, a wrapped status line.
    add_page("Connection", [this](QVBoxLayout * page) {
        auto * row = new QHBoxLayout();
        row->addWidget(new QPushButton("Connect to QarOS"));
        row->addWidget(new QPushButton("Disconnect (pause)"));
        page->addLayout(row);
        auto * invite = new QGroupBox("Invite a device");
        auto * invite_layout = new QVBoxLayout(invite);
        invite_layout->addWidget(new QComboBox());
        invite_layout->addWidget(new QLineEdit());
        invite_layout->addWidget(new QPushButton("Invite peer"));
        qar_rviz_streaming::retain_size_when_hidden(invite);
        retained.push_back(invite);
        page->addWidget(invite);
        page->addWidget(width_neutral_label());
      });
    // AR panels: a combo with a button, and a readout in a scroll area of a fixed height.
    add_page("AR panels", [this](QVBoxLayout * page) {
        auto * row = new QHBoxLayout();
        row->addWidget(new QComboBox(), 1);
        row->addWidget(new QPushButton("Refresh"));
        page->addLayout(row);
        auto * readout_label = width_neutral_label();
        auto * readout = new QScrollArea();
        readout->setWidget(readout_label);
        readout->setWidgetResizable(true);
        readout->setFixedHeight(readout->fontMetrics().lineSpacing() * 10 + 8);
        readout->setSizePolicy(QSizePolicy::Ignored, readout->sizePolicy().verticalPolicy());
        page->addWidget(readout);
      });
    // AR tools: two lists and a wrapped status line.
    add_page("AR tools", [this](QVBoxLayout * page) {
        lists.push_back(new QListWidget());
        lists.push_back(new QListWidget());
        page->addWidget(lists[0]);
        page->addWidget(lists[1]);
        page->addWidget(width_neutral_label());
      });
    // Picking: a form, a check box, a wrapped status line.
    add_page("Picking", [this](QVBoxLayout * page) {
        auto * form = new QFormLayout();
        form->addRow("Mode", new QComboBox());
        form->addRow("Frame", new QLineEdit("map"));
        page->addLayout(form);
        checkable = new QCheckBox("Publish without confirming");
        page->addWidget(checkable);
        page->addWidget(width_neutral_label());
      });
    // Settings: check boxes, and an advanced group that is hidden until it is asked for.
    add_page("Settings", [this](QVBoxLayout * page) {
        page->addWidget(new QCheckBox("Start the hidden Displays copy when connecting"));
        page->addWidget(new QCheckBox("Show the AR control panel when connecting"));
        auto * advanced = new QGroupBox("Advanced");
        auto * advanced_layout = new QVBoxLayout(advanced);
        advanced_layout->addWidget(new QCheckBox("Prime next frame"));
        advanced_layout->addWidget(new QComboBox());
        qar_rviz_streaming::retain_size_when_hidden(advanced);
        retained.push_back(advanced);
        page->addWidget(advanced);
      });
    // Diagnostics: a label, a button, a group that one mode hides, and a log.
    add_page("Diagnostics", [this](QVBoxLayout * page) {
        page->addWidget(new QLabel("DisplayContext: (not initialized)"));
        plain_button = new QPushButton("Refresh diagnostics");
        page->addWidget(plain_button);
        auto * peers = new QGroupBox("Simulated peers");
        auto * peers_layout = new QVBoxLayout(peers);
        peers_layout->addWidget(new QPushButton("Add simulated peer"));
        peers_layout->addWidget(new QListWidget());
        qar_rviz_streaming::retain_size_when_hidden(peers);
        retained.push_back(peers);
        page->addWidget(peers);
        log = new QPlainTextEdit();
        page->addWidget(log, 1);
      });

    hints = qar_rviz_streaming::compute_stable_hints(*tabs, contents, *strip.frame);
  }

  QSize sizeHint() const override { return hints.hint; }
  QSize minimumSizeHint() const override { return hints.minimum; }

  void present()
  {
    resize(420, 480);
    show();
    QApplication::processEvents();
  }

  template<class Build>
  QScrollArea * add_page(const QString & name, Build build)
  {
    auto * content = new QWidget();
    auto * layout = new QVBoxLayout(content);
    build(layout);
    contents.push_back(content);
    auto * page = qar_rviz_streaming::make_scroll_page(content, tabs);
    tabs->addTab(page, name);
    return page;
  }

  QLabel * width_neutral_label()
  {
    auto * label = new QLabel("short");
    label->setWordWrap(true);
    QSizePolicy policy = label->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    label->setSizePolicy(policy);
    status_labels.push_back(label);
    return label;
  }

  StatusStrip strip;
  QTabWidget * tabs = nullptr;
  std::vector<QWidget *> contents;
  std::vector<QLabel *> status_labels;
  std::vector<QGroupBox *> retained;
  std::vector<QListWidget *> lists;
  QCheckBox * checkable = nullptr;
  QPushButton * plain_button = nullptr;
  QPlainTextEdit * log = nullptr;
  StableHints hints;
};

/// Every size the outside world reads from a panel, as one comparable text.
std::string measure(Skeleton & panel)
{
  QApplication::processEvents();
  const QLayout * root = panel.layout();
  return "hint " + text_of(panel.sizeHint()) + " min " + text_of(panel.minimumSizeHint()) +
         " | layout hint " + text_of(root->totalSizeHint()) + " min " +
         text_of(root->totalMinimumSize()) + " | tabs min " +
         text_of(panel.tabs->minimumSizeHint()) + " | strip hint " +
         text_of(panel.strip.frame->sizeHint()) + " height " +
         std::to_string(panel.strip.frame->height());
}

TEST(StablePanelLayout, HintsConstantAcrossTabSwitches)
{
  Skeleton panel;
  panel.present();
  const std::string before = measure(panel);

  const int count = panel.tabs->count();
  ASSERT_EQ(count, 6);
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < count; ++i) {
      panel.tabs->setCurrentIndex(i);
      EXPECT_EQ(panel.tabs->currentIndex(), i);
      EXPECT_EQ(measure(panel), before) << "after switching to tab " << i;
    }
    for (int i = count - 1; i >= 0; --i) {
      panel.tabs->setCurrentIndex(i);
      EXPECT_EQ(measure(panel), before) << "after switching back to tab " << i;
    }
  }
}

TEST(StablePanelLayout, HintsConstantAcrossTextChanges)
{
  Skeleton panel;
  panel.present();
  const std::string before = measure(panel);
  const QSize page_hint_before = panel.contents[0]->sizeHint();

  // The wrapped status line of every page, with a text of many lines and a text of one long word.
  for (QLabel * label : panel.status_labels) {
    label->setText(many_words());
    EXPECT_EQ(measure(panel), before);
    label->setText(one_long_word());
    EXPECT_EQ(measure(panel), before);
    label->setText(QString());
    EXPECT_EQ(measure(panel), before);
  }
  // The text did reach the page: this test would prove nothing if it had not.
  panel.status_labels[0]->setText(many_words());
  QApplication::processEvents();
  EXPECT_NE(panel.contents[0]->sizeHint(), page_hint_before);
  EXPECT_EQ(measure(panel), before);

  // The two lines of the strip.
  panel.strip.first->setFullText(one_long_word());
  EXPECT_EQ(measure(panel), before);
  panel.strip.second->setFullText(many_words());
  EXPECT_EQ(measure(panel), before);
  panel.strip.first->setFullText("line one\nline two");
  panel.strip.second->setFullText(QString());
  EXPECT_EQ(measure(panel), before);

  // The contents of a list, a log and a check box caption grow too.
  for (int i = 0; i < 200; ++i) {
    panel.lists[0]->addItem(QString("topic %1 ").arg(i) + one_long_word());
    panel.log->appendPlainText(many_words());
  }
  panel.checkable->setText(one_long_word());
  EXPECT_EQ(measure(panel), before);

  // The strip's lines in the colours and weights the panel gives them.
  panel.strip.first->setStyleSheet("font-weight: bold; color: #27ae60;");
  panel.strip.second->setStyleSheet("color: #c0392b;");
  panel.strip.first->setFullText("Qar runtime: RUNNING (live)");
  EXPECT_EQ(measure(panel), before);
}

TEST(StablePanelLayout, HintsConstantAcrossHideShow)
{
  Skeleton panel;
  panel.present();
  const std::string before = measure(panel);

  // A group that keeps its room while hidden: the page it sits on does not change either.
  for (QGroupBox * group : panel.retained) {
    QWidget * page = group->parentWidget();
    const QSize page_hint = page->sizeHint();
    const QSize page_minimum = page->minimumSizeHint();
    group->hide();
    EXPECT_EQ(measure(panel), before);
    EXPECT_EQ(page->sizeHint(), page_hint) << "a hidden group kept its room";
    EXPECT_EQ(page->minimumSizeHint(), page_minimum) << "a hidden group kept its room";
    group->show();
    EXPECT_EQ(measure(panel), before);
  }

  // A widget that gives its room up: the page does change, the panel does not.
  QWidget * page = panel.plain_button->parentWidget();
  const QSize page_minimum = page->minimumSizeHint();
  panel.plain_button->hide();
  EXPECT_EQ(measure(panel), before);
  EXPECT_NE(page->minimumSizeHint(), page_minimum) << "the hide did reach the page";
  panel.plain_button->show();
  EXPECT_EQ(measure(panel), before);

  // Enabled or not changes no size.
  panel.plain_button->setEnabled(false);
  panel.checkable->setEnabled(false);
  EXPECT_EQ(measure(panel), before);
  panel.plain_button->setEnabled(true);
  panel.checkable->setEnabled(true);
  EXPECT_EQ(measure(panel), before);
}

TEST(StablePanelLayout, ScrollPageNeverScrollsSideways)
{
  Skeleton panel;
  // A page that wants far more width than any dock has, and one that wants far more height.
  panel.add_page("Wide", [](QVBoxLayout * page) {
      page->addWidget(new QPushButton(QString(300, QLatin1Char('W'))));
    });
  panel.add_page("Tall", [](QVBoxLayout * page) {
      for (int i = 0; i < 80; ++i) {
        page->addWidget(new QCheckBox(QString("check box %1").arg(i)));
      }
    });
  panel.resize(200, 300);
  panel.show();
  QApplication::processEvents();

  for (int i = 0; i < panel.tabs->count(); ++i) {
    panel.tabs->setCurrentIndex(i);
    QApplication::processEvents();
    auto * page = qobject_cast<QScrollArea *>(panel.tabs->widget(i));
    ASSERT_NE(page, nullptr) << "tab " << i;
    EXPECT_EQ(page->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff) << "tab " << i;
    EXPECT_EQ(page->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded) << "tab " << i;
    EXPECT_EQ(page->frameShape(), QFrame::NoFrame) << "tab " << i;
    EXPECT_TRUE(page->widgetResizable()) << "tab " << i;
    EXPECT_EQ(page->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored) << "tab " << i;
    EXPECT_EQ(page->sizePolicy().verticalPolicy(), QSizePolicy::Ignored) << "tab " << i;
    EXPECT_FALSE(page->horizontalScrollBar()->isVisible()) << "tab " << i << " scrolls sideways";
  }

  // The tall page does scroll, vertically.
  panel.tabs->setCurrentIndex(panel.tabs->count() - 1);
  QApplication::processEvents();
  auto * tall = qobject_cast<QScrollArea *>(panel.tabs->currentWidget());
  ASSERT_NE(tall, nullptr);
  EXPECT_TRUE(tall->verticalScrollBar()->isVisible());
  EXPECT_FALSE(tall->horizontalScrollBar()->isVisible());

  // The wide page is the one the report names: it cannot scroll sideways, so it says by how much
  // it is wider than the room it has.
  const std::vector<std::string> lines =
    qar_rviz_streaming::describe_page_fit(*panel.tabs, panel.contents);
  ASSERT_EQ(lines.size(), 8u);
  EXPECT_NE(lines[6].find("page 'Wide':"), std::string::npos) << lines[6];
  EXPECT_NE(lines[6].find("px wider than the viewport"), std::string::npos) << lines[6];
  EXPECT_NE(lines[7].find("page 'Tall':"), std::string::npos) << lines[7];
  EXPECT_NE(lines[7].find("SCROLLS by"), std::string::npos) << lines[7];
}

TEST(StablePanelLayout, StatusStripHasFixedHeight)
{
  QWidget host;
  auto * host_layout = new QVBoxLayout(&host);
  host_layout->setContentsMargins(0, 0, 0, 0);
  host_layout->setSpacing(0);
  const StatusStrip strip = qar_rviz_streaming::make_status_strip(&host);
  host_layout->addWidget(strip.frame);
  host_layout->addStretch(1);
  strip.first->setFullText("Qar runtime: STOPPED");
  strip.second->setFullText("Last error: <none>");
  host.resize(300, 200);
  host.show();
  QApplication::processEvents();

  const int height = strip.frame->height();
  EXPECT_GT(height, 0);
  EXPECT_EQ(strip.frame->minimumHeight(), strip.frame->maximumHeight());
  EXPECT_EQ(strip.frame->minimumHeight(), height);
  EXPECT_EQ(strip.frame->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  EXPECT_EQ(strip.frame->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
  const QSize hint = strip.frame->sizeHint();
  const QSize minimum = strip.frame->minimumSizeHint();

  const QString texts[] = {one_long_word(), many_words(), QString("a\nb\nc"), QString()};
  for (const QString & text : texts) {
    strip.first->setFullText(text);
    strip.second->setFullText(text);
    QApplication::processEvents();
    EXPECT_EQ(strip.frame->height(), height);
    EXPECT_EQ(strip.frame->sizeHint(), hint) << "a text widened or heightened the strip";
    EXPECT_EQ(strip.frame->minimumSizeHint(), minimum);
  }

  // The bold weight and the colours of the panel's lines need no more room than one line each.
  strip.first->setStyleSheet("font-weight: bold; color: #d35400;");
  strip.second->setStyleSheet("color: #c0392b;");
  strip.first->setFullText("Qar runtime: CONNECTING...");
  strip.second->setFullText("Last error: something went wrong");
  QApplication::processEvents();
  EXPECT_EQ(strip.frame->height(), height);
  EXPECT_LE(strip.first->fontMetrics().lineSpacing(), strip.first->height());
  EXPECT_LE(strip.second->fontMetrics().lineSpacing(), strip.second->height());
  EXPECT_EQ(strip.first->height(), strip.second->height());
}

TEST(StablePanelLayout, ElidedLabelKeepsFullText)
{
  ElidedLabel label;
  label.setFixedWidth(60);
  label.show();
  const QString text = "Last error: " + many_words();
  label.setFullText(text);
  QApplication::processEvents();

  EXPECT_EQ(label.fullText(), text);
  EXPECT_EQ(label.toolTip(), text);
  EXPECT_LT(label.text().size(), text.size()) << "the text was not elided to 60 pixels";
  EXPECT_LE(label.fontMetrics().horizontalAdvance(label.text()), label.contentsRect().width());

  // More room shows more of the same text; the whole text is still the one that was given.
  label.setFixedWidth(60000);
  QApplication::processEvents();
  EXPECT_EQ(label.text(), text);
  EXPECT_EQ(label.fullText(), text);
  label.setFixedWidth(60);
  QApplication::processEvents();
  EXPECT_LT(label.text().size(), text.size());
  EXPECT_EQ(label.fullText(), text);

  // A line break is shown as a space, and the tool tip keeps the text as it was given.
  label.setFixedWidth(60000);
  label.setFullText("one\ntwo");
  QApplication::processEvents();
  EXPECT_EQ(label.text(), "one two");
  EXPECT_EQ(label.fullText(), "one\ntwo");
  EXPECT_EQ(label.toolTip(), "one\ntwo");

  // Text that looks like a tag is text.
  label.setFullText("Last error: <none> <b>bold</b>");
  QApplication::processEvents();
  EXPECT_EQ(label.text(), "Last error: <none> <b>bold</b>");
  EXPECT_EQ(label.textFormat(), Qt::PlainText);

  // The label is one line high and adds nothing to the width of its parent.
  EXPECT_EQ(label.sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  EXPECT_EQ(label.minimumHeight(), label.maximumHeight());
}

TEST(StablePanelLayout, TabWidgetTakesTheHeightTheStripLeaves)
{
  Skeleton panel;
  panel.present();
  EXPECT_EQ(panel.tabs->width(), panel.width());
  EXPECT_EQ(panel.tabs->height(), panel.height() - panel.strip.frame->height());
  panel.resize(300, 700);
  QApplication::processEvents();
  EXPECT_EQ(panel.tabs->height(), panel.height() - panel.strip.frame->height());
  EXPECT_EQ(panel.tabs->width(), 300);
}

TEST(StablePanelLayout, TabWidgetAddsNothingToTheLayoutHints)
{
  // The layout that holds the strip and the tab widget asks for the strip and nothing else: the
  // tab widget's policy is Ignored in both directions, so whatever its pages hold, now or at the
  // first layout, cannot reach the hints of the layout above it.
  Skeleton panel;
  panel.present();
  const QLayout * root = panel.layout();
  const int strip = panel.strip.frame->height();
  EXPECT_EQ(root->totalSizeHint(), QSize(0, strip));
  EXPECT_EQ(root->totalMinimumSize(), QSize(0, strip));

  panel.status_labels[0]->setText(many_words());
  panel.checkable->setText(one_long_word());
  for (int i = 0; i < 100; ++i) {
    panel.lists[0]->addItem(QString("topic %1").arg(i));
  }
  QApplication::processEvents();
  EXPECT_EQ(root->totalSizeHint(), QSize(0, strip));
  EXPECT_EQ(root->totalMinimumSize(), QSize(0, strip));
}

TEST(StablePanelLayout, StableHintsFollowThePagesOnceAndHonourTheCap)
{
  Skeleton panel;
  panel.present();
  const int strip = panel.strip.frame->height();
  const int bar = qar_rviz_streaming::tab_bar_height(*panel.tabs);
  EXPECT_GT(bar, 0);

  const StableHints uncapped = qar_rviz_streaming::compute_stable_hints(
    *panel.tabs, panel.contents, *panel.strip.frame, 100000);
  EXPECT_EQ(uncapped.minimum.width(), 200);
  EXPECT_EQ(uncapped.minimum.height(), strip + bar + 120);
  EXPECT_GE(uncapped.hint.width(), 200);
  EXPECT_GE(uncapped.hint.height(), strip + bar);

  const StableHints capped = qar_rviz_streaming::compute_stable_hints(
    *panel.tabs, panel.contents, *panel.strip.frame, strip + bar + 10);
  EXPECT_EQ(capped.hint.height(), strip + bar + 10);
  EXPECT_EQ(capped.hint.width(), uncapped.hint.width());
  EXPECT_EQ(capped.minimum, uncapped.minimum);

  // Measuring again after the pages changed gives a different answer, which is why the panel
  // measures once and keeps the result.
  const QSize first = panel.hints.hint;
  panel.checkable->setText(one_long_word());
  const StableHints again = qar_rviz_streaming::compute_stable_hints(
    *panel.tabs, panel.contents, *panel.strip.frame);
  EXPECT_EQ(panel.sizeHint(), first);
  EXPECT_GE(again.hint.width(), first.width());
}

/// Sends a wheel step (one notch towards the user) to @p target; returns whether it was accepted.
bool send_wheel(QWidget * target)
{
  QWheelEvent event(
    QPointF(5, 5), QPointF(5, 5), QPoint(0, 0), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
    Qt::NoScrollPhase, false);
  event.ignore();
  QApplication::sendEvent(target, &event);
  return event.isAccepted();
}

TEST(StablePanelLayout, WheelDoesNotChangeAnUnfocusedControl)
{
  Skeleton panel;
  // A guarded combo and spin box and an unguarded combo, on a tall page.
  QComboBox * guarded = nullptr;
  QDoubleSpinBox * spin = nullptr;
  QComboBox * plain = nullptr;
  QScrollArea * page = panel.add_page("Controls", [&](QVBoxLayout * layout) {
      guarded = new QComboBox();
      guarded->addItems({"a", "b", "c"});
      guarded->setCurrentIndex(1);
      spin = new QDoubleSpinBox();
      spin->setRange(0.0, 10.0);
      spin->setValue(5.0);
      plain = new QComboBox();
      plain->addItems({"a", "b", "c"});
      plain->setCurrentIndex(1);
      layout->addWidget(guarded);
      layout->addWidget(spin);
      layout->addWidget(plain);
      for (int i = 0; i < 80; ++i) {
        layout->addWidget(new QCheckBox(QString("check box %1").arg(i)));
      }
    });
  qar_rviz_streaming::guard_wheel(guarded);
  qar_rviz_streaming::guard_wheel(spin);
  panel.resize(420, 300);
  panel.show();
  panel.tabs->setCurrentWidget(page);
  QApplication::processEvents();
  ASSERT_TRUE(page->verticalScrollBar()->isVisible());
  ASSERT_EQ(page->verticalScrollBar()->value(), 0);

  // The control of the contrast takes the wheel: the test's wheel event does change a combo.
  EXPECT_FALSE(plain->hasFocus());
  send_wheel(plain);
  EXPECT_NE(plain->currentIndex(), 1) << "the wheel event of this test changes nothing";

  // The guarded ones do not: the value stays, and the event is left unaccepted, which is what
  // lets Qt hand a real (spontaneous) wheel event on to the page under the control.
  EXPECT_FALSE(guarded->hasFocus());
  EXPECT_FALSE(send_wheel(guarded));
  EXPECT_EQ(guarded->currentIndex(), 1);
  EXPECT_FALSE(send_wheel(spin));
  EXPECT_EQ(spin->value(), 5.0);

  // With the keyboard focus the wheel changes the value as usual (when the platform gives the
  // focus: the offscreen platform may not activate the window).
  guarded->setFocus(Qt::MouseFocusReason);
  panel.activateWindow();
  QApplication::processEvents();
  if (guarded->hasFocus()) {
    EXPECT_TRUE(send_wheel(guarded));
    EXPECT_NE(guarded->currentIndex(), 1);
  }
  EXPECT_EQ(guarded->focusPolicy(), Qt::StrongFocus);
}


TEST(StablePanelLayout, StartSizeLeavesRoomForAScrollBar)
{
  Skeleton panel;
  int widest = 0;
  for (const QWidget * content : panel.contents) {
    widest = std::max(widest, content->sizeHint().width());
  }
  const int bar = panel.tabs->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, panel.tabs);
  EXPECT_GE(panel.sizeHint().width(), widest + bar);
}

/// The six tab titles of the panel, in its order.
const char * const kPanelTabTitles[] = {
  "Connection", "AR panels", "AR tools", "Picking", "Settings", "Diagnostics"};

/// The status strip over a tab widget that carries the panel's six titles on pages that are much
/// narrower than the titles are together: nothing but the tab bar can decide the width.
class NarrowSkeleton : public QWidget
{
public:
  NarrowSkeleton()
  {
    auto * root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    strip = qar_rviz_streaming::make_status_strip(this);
    root->addWidget(strip.frame);
    tabs = qar_rviz_streaming::make_tab_widget(this);
    root->addWidget(tabs, 1);
    for (const char * title : kPanelTabTitles) {
      add_page(title, new QLabel("x"));
    }
    hints = qar_rviz_streaming::compute_stable_hints(*tabs, contents, *strip.frame);
  }

  QSize sizeHint() const override { return hints.hint; }
  QSize minimumSizeHint() const override { return hints.minimum; }

  void add_page(const QString & title, QWidget * body)
  {
    auto * content = new QWidget();
    auto * layout = new QVBoxLayout(content);
    layout->addWidget(body);
    contents.push_back(content);
    tabs->addTab(qar_rviz_streaming::make_scroll_page(content, tabs), title);
  }

  /// Shows the panel at @p size and lets the layout settle.
  void present(const QSize & size)
  {
    resize(size);
    show();
    QApplication::processEvents();
  }

  /// True when the tab bar shows a scroll button.
  bool bar_scrolls() const
  {
    const QTabBar * bar = tabs->findChild<QTabBar *>();
    for (const QToolButton * button : bar->findChildren<QToolButton *>()) {
      if (button->isVisible()) {
        return true;
      }
    }
    return false;
  }

  StatusStrip strip;
  QTabWidget * tabs = nullptr;
  std::vector<QWidget *> contents;
  StableHints hints;
};

TEST(StablePanelLayout, HintWidthCoversTheTabBar)
{
  NarrowSkeleton panel;
  const int bar = qar_rviz_streaming::tab_bar_width(*panel.tabs);
  // The premise: the six titles are wider than the minimum width and than every page, so that
  // only the rule for the tab bar can make the hint wide enough.
  ASSERT_GT(bar, 200) << "six tab titles in this font are narrower than the minimum width";
  ASSERT_EQ(panel.tabs->count(), 6);
  std::cout << "[ INFO ] the six titles need " << bar << " px; hint " << text_of(panel.sizeHint())
            << ", minimum " << text_of(panel.minimumSizeHint()) << ", style "
            << panel.tabs->style()->objectName().toStdString() << "\n";
  RecordProperty("tab_bar_width_px", bar);
  RecordProperty("hint_width_px", panel.sizeHint().width());
  EXPECT_GE(panel.sizeHint().width(), bar);
  EXPECT_GT(panel.sizeHint().width(), panel.hints.minimum.width());

  // At the width of the hint every title shows whole, and the tab bar has no scroll button.
  panel.present(panel.sizeHint());
  const QTabBar * tab_bar = panel.tabs->findChild<QTabBar *>();
  ASSERT_NE(tab_bar, nullptr);
  EXPECT_GE(tab_bar->width(), tab_bar->sizeHint().width());
  for (int i = 0; i < tab_bar->count(); ++i) {
    EXPECT_TRUE(tab_bar->rect().contains(tab_bar->tabRect(i)))
      << "the title '" << kPanelTabTitles[i] << "' is cut off";
  }
  EXPECT_FALSE(panel.bar_scrolls());

  // The control: narrower than the hint, at the width the pages alone would ask for, the same
  // tab bar does scroll. The check above is about the hint and not about a roomy test window.
  panel.present(QSize(200, panel.sizeHint().height()));
  EXPECT_TRUE(panel.bar_scrolls());
  panel.present(panel.sizeHint());
  EXPECT_FALSE(panel.bar_scrolls());
}

TEST(StablePanelLayout, HintWidthKeepsTheWidestPageWhenThePagesAreWider)
{
  // A page that wants more than the tab bar: the hint follows the page, with the room of a
  // vertical scroll bar.
  NarrowSkeleton panel;
  panel.add_page("Wide", new QPushButton(QString(120, QLatin1Char('W'))));
  const int bar = qar_rviz_streaming::tab_bar_width(*panel.tabs);
  const int page_width = panel.contents.back()->sizeHint().width();
  ASSERT_GT(page_width, bar + 100) << "the test page is not wider than the tab bar";

  const StableHints hints = qar_rviz_streaming::compute_stable_hints(
    *panel.tabs, panel.contents, *panel.strip.frame);
  const int scroll_bar =
    panel.tabs->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, panel.tabs);
  EXPECT_GE(hints.hint.width(), page_width + scroll_bar);
  EXPECT_GT(hints.hint.width(), bar + 100) << "the page, not the tab bar, sets the width";
}

TEST(StablePanelLayout, TabBarWidthDoesNotFollowThePanelWidth)
{
  // The hint is measured once, before the panel is shown. What it is made of must not depend on
  // the width the tab bar happens to have when it is asked, or measuring again would answer
  // differently in a wide window than in a narrow one.
  NarrowSkeleton panel;
  const int bar_before = qar_rviz_streaming::tab_bar_width(*panel.tabs);
  const int hint_before = panel.hints.hint.width();
  for (const QSize & size : {QSize(900, 600), QSize(150, 400), QSize(420, 480)}) {
    panel.present(size);
    EXPECT_EQ(qar_rviz_streaming::tab_bar_width(*panel.tabs), bar_before) << text_of(size);
    const StableHints again = qar_rviz_streaming::compute_stable_hints(
      *panel.tabs, panel.contents, *panel.strip.frame);
    EXPECT_EQ(again.hint.width(), hint_before) << text_of(size);
  }
  EXPECT_EQ(panel.sizeHint().width(), hint_before) << "the answer of the panel stays";
}

TEST(StablePanelLayout, DefaultDockSizeFitsTheHeightOfTheHint)
{
  using qar_rviz_streaming::default_dock_size;
  EXPECT_EQ(qar_rviz_streaming::kDefaultDockWidth, 475);
  EXPECT_EQ(qar_rviz_streaming::kDefaultDockHeight, 420);
  EXPECT_EQ(qar_rviz_streaming::kDockHeightSlack, 8);
  EXPECT_EQ(qar_rviz_streaming::kDockClearance, 40);

  // The height is the hint's plus 8 pixels, and 420 pixels when that is less.
  const QSize roomy(1920, 1080);
  EXPECT_EQ(default_dock_size(QSize(378, 605), roomy), QSize(475, 613));
  EXPECT_EQ(default_dock_size(QSize(417, 413), roomy), QSize(475, 421));
  EXPECT_EQ(default_dock_size(QSize(475, 300), roomy), QSize(475, 420));
  EXPECT_EQ(default_dock_size(QSize(0, 0), roomy), QSize(475, 420));
  EXPECT_EQ(default_dock_size(QSize(), roomy), QSize(475, 420));
  // Where the hint starts to decide: 412 + 8 is 420, 413 + 8 is 421.
  EXPECT_EQ(default_dock_size(QSize(417, 411), roomy), QSize(475, 420));
  EXPECT_EQ(default_dock_size(QSize(417, 412), roomy), QSize(475, 420));
  EXPECT_EQ(default_dock_size(QSize(417, 413), roomy), QSize(475, 421));
  EXPECT_EQ(default_dock_size(QSize(417, 414), roomy), QSize(475, 422));
  // A hint that is wider than 475 sets the width.
  EXPECT_EQ(default_dock_size(QSize(520, 605), roomy), QSize(520, 613));
  EXPECT_EQ(default_dock_size(QSize(476, 605), roomy), QSize(476, 613));
}

TEST(StablePanelLayout, DefaultDockSizeIsClampedToTheAvailableRoom)
{
  using qar_rviz_streaming::default_dock_size;
  const QSize hint(378, 605);
  // 40 pixels stay free on each side.
  EXPECT_EQ(default_dock_size(hint, QSize(900, 400)), QSize(475, 360));
  EXPECT_EQ(default_dock_size(hint, QSize(500, 1000)), QSize(460, 613));
  EXPECT_EQ(default_dock_size(hint, QSize(500, 400)), QSize(460, 360));
  EXPECT_EQ(default_dock_size(QSize(520, 605), QSize(540, 1000)), QSize(500, 613));
  // Exactly enough room, and one pixel too little.
  EXPECT_EQ(default_dock_size(hint, QSize(515, 653)), QSize(475, 613));
  EXPECT_EQ(default_dock_size(hint, QSize(514, 652)), QSize(474, 612));
  // The least height is clamped too.
  EXPECT_EQ(default_dock_size(QSize(417, 300), QSize(900, 440)), QSize(475, 400));

  // A side of the available room that is not known (not positive) clamps nothing.
  EXPECT_EQ(default_dock_size(hint, QSize()), QSize(475, 613));
  EXPECT_EQ(default_dock_size(hint, QSize(0, 0)), QSize(475, 613));
  EXPECT_EQ(default_dock_size(hint, QSize(300, 0)), QSize(260, 613));
  EXPECT_EQ(default_dock_size(hint, QSize(0, 300)), QSize(475, 260));
  EXPECT_EQ(default_dock_size(hint, QSize(-5, 1000)), QSize(475, 613));
}

TEST(StablePanelLayout, DefaultDockSizeIsNeverBelowTheMinimum)
{
  using qar_rviz_streaming::default_dock_size;
  const QSize hint(378, 605);
  const QSize minimum(200, 250);
  // A window too small for the clamp: the minimum wins.
  EXPECT_EQ(default_dock_size(hint, QSize(150, 120), minimum), minimum);
  EXPECT_EQ(default_dock_size(hint, QSize(230, 1000), minimum), QSize(200, 613));
  EXPECT_EQ(default_dock_size(hint, QSize(900, 270), minimum), QSize(475, 250));
  // A minimum below the clamped size changes nothing.
  EXPECT_EQ(default_dock_size(hint, QSize(900, 400), minimum), QSize(475, 360));
  // A minimum above the default is the size.
  EXPECT_EQ(default_dock_size(hint, QSize(1920, 1080), QSize(600, 700)), QSize(600, 700));
  // Without a minimum a tiny window does not give a negative size.
  EXPECT_EQ(default_dock_size(hint, QSize(30, 30)), QSize(0, 0));

  // With the minimum of a real panel.
  NarrowSkeleton panel;
  const QSize tiny(100, 100);
  EXPECT_EQ(default_dock_size(panel.hints.hint, tiny, panel.hints.minimum), panel.hints.minimum);
  const QSize dock = default_dock_size(panel.hints.hint, QSize(1920, 1080), panel.hints.minimum);
  EXPECT_GE(dock.width(), panel.hints.hint.width()) << "the dock holds the six titles";
  EXPECT_GE(dock.width(), panel.hints.minimum.width());
  EXPECT_GE(dock.height(), panel.hints.minimum.height());
}

TEST(StablePanelLayout, TheDefaultDockHeightGivesEveryPageItsRoom)
{
  using qar_rviz_streaming::default_dock_size;
  Skeleton panel;
  const QSize dock = default_dock_size(panel.hints.hint, QSize(1920, 1080), panel.hints.minimum);
  EXPECT_GE(dock.height(), panel.hints.hint.height() + qar_rviz_streaming::kDockHeightSlack);

  // The stand-in at the default size: the tallest page needs what the hint measured, and the
  // dock is 8 pixels higher than the hint, so no page scrolls.
  panel.resize(dock);
  panel.show();
  QApplication::processEvents();
  const std::vector<std::string> lines =
    qar_rviz_streaming::describe_page_fit(*panel.tabs, panel.contents);
  ASSERT_EQ(lines.size(), 6u);
  for (const std::string & line : lines) {
    EXPECT_EQ(line.find("SCROLLS"), std::string::npos) << line << " at " << text_of(dock);
  }

  // At the hint's own height the room of the tallest page is exactly what it needs; a panel that
  // is lower than the hint scrolls it.
  panel.resize(dock.width(), panel.hints.hint.height() - 60);
  QApplication::processEvents();
  bool any_scrolls = false;
  const std::vector<std::string> low =
    qar_rviz_streaming::describe_page_fit(*panel.tabs, panel.contents);
  for (const std::string & line : low) {
    any_scrolls = any_scrolls || line.find("SCROLLS") != std::string::npos;
  }
  EXPECT_TRUE(any_scrolls) << "this check proves nothing if a lower panel still fits";
}

/// A main window with a shared left column: a tall "Displays" stand-in, the panel's dock and a
/// third dock, each with a size hint, so that together they ask for more height than the window
/// has. The central widget takes the rest.
class SharedColumn
{
public:
  explicit SharedColumn(int window_height = 800, int third_minimum = 40)
  {
    window.resize(1200, window_height);
    window.setCentralWidget(new QLabel("scene"));

    displays.setWindowTitle("Displays");
    displays.setWidget(tall_widget(300, 60));
    window.addDockWidget(Qt::LeftDockWidgetArea, &displays);

    panel_dock.setWindowTitle("Qar Streaming");
    panel_dock.setWidget(tall_widget(300, 60));
    window.addDockWidget(Qt::LeftDockWidgetArea, &panel_dock);

    third.setWindowTitle("Views");
    third.setWidget(tall_widget(300, third_minimum));
    window.addDockWidget(Qt::LeftDockWidgetArea, &third);

    window.show();
    settle();
  }

  static QWidget * tall_widget(int hint_height, int minimum_height)
  {
    auto * widget = new SizedWidget(QSize(300, hint_height), QSize(100, minimum_height));
    return widget;
  }

  void settle()
  {
    for (int i = 0; i < 5; ++i) {
      QApplication::processEvents();
    }
  }

  /// A widget with the size hints it is given.
  class SizedWidget : public QWidget
  {
  public:
    SizedWidget(const QSize & hint, const QSize & minimum)
    : hint_(hint), minimum_(minimum) {}
    QSize sizeHint() const override { return hint_; }
    QSize minimumSizeHint() const override { return minimum_; }

  private:
    QSize hint_;
    QSize minimum_;
  };

  QMainWindow window;
  QDockWidget displays;
  QDockWidget panel_dock;
  QDockWidget third;
};

/// Options with a room no screen limits.
qar_rviz_streaming::DockHeightOptions free_room()
{
  qar_rviz_streaming::DockHeightOptions options;
  options.room = QRect(-5000, -5000, 20000, 20000);
  return options;
}

/// True when the layout can hold the minimum of every dock at the height the window has.
bool layout_holds(const QMainWindow & window)
{
  return window.layout()->totalMinimumSize().height() <= window.height();
}

TEST(StablePanelLayout, SetDockHeightGivesASharedColumnDockTheHeightAsked)
{
  SharedColumn column;
  const int asked = 421;
  const QSize window_before = column.window.size();
  const int minimum_before = column.panel_dock.minimumHeight();
  const int maximum_before = column.panel_dock.maximumHeight();
  // The column asks for more than the window has, so every dock is short of its hint and a plain
  // resizeDocks() could not give this one 421 pixels.
  ASSERT_LT(column.panel_dock.height(), 300);

  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, asked, hold, free_room());
  EXPECT_GE(result.got, asked) << "the dock has the height asked for";
  EXPECT_TRUE(result.held) << "the column is shared, so the dock was held";
  EXPECT_TRUE(hold.active());
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kNotNeeded);
  EXPECT_EQ(column.panel_dock.height(), result.got);
  EXPECT_EQ(column.panel_dock.minimumHeight(), asked) << "held by a temporary minimum height";
  EXPECT_EQ(column.window.size(), window_before) << "the window was not made bigger";

  // After the hold is released the dock keeps the height the layout gave it.
  qar_rviz_streaming::release_dock_hold(hold);
  column.settle();
  EXPECT_FALSE(hold.active());
  EXPECT_EQ(column.panel_dock.minimumHeight(), minimum_before);
  EXPECT_EQ(column.panel_dock.maximumHeight(), maximum_before);
  EXPECT_GE(column.panel_dock.height(), asked);
  EXPECT_EQ(column.window.size(), window_before);
}

/// Two docks in the left column that fit the window with room to spare.
class TwoDocks
{
public:
  TwoDocks()
  : displays("Displays"), dock("Qar Streaming")
  {
    window.resize(1200, 800);
    window.setCentralWidget(new QLabel("scene"));
    displays.setWidget(new SharedColumn::SizedWidget(QSize(300, 150), QSize(100, 60)));
    window.addDockWidget(Qt::LeftDockWidgetArea, &displays);
    dock.setWidget(new SharedColumn::SizedWidget(QSize(300, 150), QSize(100, 60)));
    window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
    window.show();
    for (int i = 0; i < 5; ++i) {
      QApplication::processEvents();
    }
  }

  QMainWindow window;
  QDockWidget displays;
  QDockWidget dock;
};

TEST(StablePanelLayout, SetDockHeightHoldsADockEvenWhenTheColumnHasRoomToSpare)
{
  // resizeDocks() alone does not give a dock in a shared column the height asked, however much
  // room the column has: the layout takes the missing height from both docks in proportion, so
  // the dock ends up with a part of the change only. This is why a plain resizeDocks() was not
  // enough, and what the hold is for.
  TwoDocks plain;
  const int before = plain.dock.height();
  const int asked = before + 150;
  plain.window.resizeDocks({&plain.dock}, {asked}, Qt::Vertical);
  plain.window.layout()->invalidate();
  plain.window.layout()->activate();
  ASSERT_LT(plain.dock.height(), asked) << "a plain resizeDocks() is short of the height asked";

  TwoDocks column;
  const QSize window_before = column.window.size();
  const int minimum_before = column.dock.minimumHeight();
  ASSERT_EQ(column.dock.height(), before) << "the second window starts as the first did";
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.dock, asked, hold, free_room());
  EXPECT_TRUE(result.held);
  EXPECT_EQ(result.got, asked);
  EXPECT_EQ(column.dock.height(), asked);
  EXPECT_EQ(column.dock.minimumHeight(), asked);
  EXPECT_LT(column.displays.height(), 800 - asked + 30) << "the other dock gave way";
  EXPECT_EQ(column.window.size(), window_before);

  qar_rviz_streaming::release_dock_hold(hold);
  for (int i = 0; i < 5; ++i) {
    QApplication::processEvents();
  }
  EXPECT_EQ(column.dock.minimumHeight(), minimum_before);
  EXPECT_EQ(column.dock.height(), asked) << "the dock keeps its height after the hold is released";
}

TEST(StablePanelLayout, SetDockHeightAppliesTheLayoutAndDoesNotHoldADockThatIsTallEnough)
{
  // A request for less than the dock has: no hold, and the answer is what the layout made of it,
  // read at once (resizeDocks() alone changes nothing until the layout is applied).
  TwoDocks column;
  const int minimum_before = column.dock.minimumHeight();
  const int asked = column.dock.height() - 100;
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.dock, asked, hold, free_room());
  EXPECT_FALSE(result.held);
  EXPECT_FALSE(hold.active());
  EXPECT_EQ(column.dock.minimumHeight(), minimum_before);
  EXPECT_LT(result.got, result.before) << "the dock did shrink, and the answer says so at once";
  EXPECT_GE(result.got, asked);
  EXPECT_EQ(result.got, column.dock.height());
}

TEST(StablePanelLayout, SetDockHeightGrowsTheWindowByWhatTheOthersCannotGive)
{
  // The other docks have minimums that leave less than the asked height: the window is made
  // higher by the missing height, and by no more.
  SharedColumn column(/*window_height=*/500, /*third_minimum=*/260);
  const int window_before = column.window.height();
  const int minimum_before = column.panel_dock.minimumHeight();
  const int asked = 480;
  const int third_minimum = column.third.minimumHeight();

  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, asked, hold, free_room());
  column.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kGrown);
  EXPECT_TRUE(result.held);
  EXPECT_EQ(result.got, asked) << "the dock has the height asked for";
  EXPECT_EQ(column.panel_dock.height(), asked);
  EXPECT_EQ(result.window_before, window_before);
  EXPECT_GT(result.window_after, window_before);
  EXPECT_EQ(column.window.height(), result.window_after);
  // The window grew by the deficit: the layout needs exactly the height the window has now.
  EXPECT_EQ(column.window.layout()->totalMinimumSize().height(), column.window.height());
  EXPECT_GE(column.third.height(), third_minimum);

  // The window stays as it is when the hold is released, and so does the dock.
  qar_rviz_streaming::release_dock_hold(hold);
  column.settle();
  EXPECT_EQ(column.panel_dock.minimumHeight(), minimum_before);
  EXPECT_EQ(column.window.height(), result.window_after);
  EXPECT_GE(column.panel_dock.height(), asked);
}

TEST(StablePanelLayout, SetDockHeightLeavesAMaximizedOrFullScreenWindowAlone)
{
  for (const Qt::WindowState state : {Qt::WindowMaximized, Qt::WindowFullScreen}) {
    SharedColumn column(/*window_height=*/500, /*third_minimum=*/260);
    column.window.setWindowState(state);
    column.settle();
    ASSERT_EQ(column.window.isMaximized(), state == Qt::WindowMaximized);
    ASSERT_EQ(column.window.isFullScreen(), state == Qt::WindowFullScreen);
    const QSize window_before = column.window.size();
    // More than the dock and the minimum of the other two can ever have in this window.
    const int asked = window_before.height() - 100;

    qar_rviz_streaming::DockHold hold;
    const qar_rviz_streaming::DockHeightResult result =
      qar_rviz_streaming::set_dock_height(column.panel_dock, asked, hold, free_room());
    column.settle();
    EXPECT_EQ(
      result.window,
      state == Qt::WindowMaximized ? qar_rviz_streaming::WindowGrowth::kMaximized :
      qar_rviz_streaming::WindowGrowth::kFullScreen);
    EXPECT_EQ(column.window.size(), window_before) << "the window was not resized";
    EXPECT_EQ(result.window_after, window_before.height());
    EXPECT_LT(result.got, asked);
    EXPECT_TRUE(layout_holds(column.window)) << "no minimum is left that the layout cannot hold";
    qar_rviz_streaming::release_dock_hold(hold);
  }
}

TEST(StablePanelLayout, SetDockHeightGrowsTheWindowOnlyAsFarAsTheRoomGoes)
{
  SharedColumn column(/*window_height=*/500, /*third_minimum=*/260);
  const QRect frame = column.window.frameGeometry();
  const int window_before = column.window.height();

  // Room for 30 pixels more than the window has: it grows by 30 and the dock gets less than asked.
  qar_rviz_streaming::DockHeightOptions options;
  options.room = QRect(frame.left(), frame.top(), frame.width(), frame.height() + 30);
  qar_rviz_streaming::DockHold hold;
  qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, 480, hold, options);
  column.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kScreenBottom);
  EXPECT_EQ(column.window.height(), window_before + 30);
  EXPECT_LT(result.got, 480);
  EXPECT_TRUE(layout_holds(column.window));
  EXPECT_LE(column.window.frameGeometry().bottom(), options.room.bottom());
  qar_rviz_streaming::release_dock_hold(hold);

  // No room at all: the window stays, and says why.
  SharedColumn tight(/*window_height=*/500, /*third_minimum=*/260);
  const QRect tight_frame = tight.window.frameGeometry();
  options.room = tight_frame;
  result = qar_rviz_streaming::set_dock_height(tight.panel_dock, 480, hold, options);
  tight.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kScreenBottom);
  EXPECT_EQ(tight.window.height(), 500);
  EXPECT_LT(result.got, 480);
  EXPECT_TRUE(layout_holds(tight.window));
  qar_rviz_streaming::release_dock_hold(hold);
}

TEST(StablePanelLayout, SetDockHeightMovesTheWindowUpWhenItsLowerEdgeWouldLeaveTheRoom)
{
  // How much higher the window has to be for the dock to get 480 pixels.
  int needed = 0;
  {
    SharedColumn probe(/*window_height=*/500, /*third_minimum=*/260);
    qar_rviz_streaming::DockHold probe_hold;
    const qar_rviz_streaming::DockHeightResult probed =
      qar_rviz_streaming::set_dock_height(probe.panel_dock, 480, probe_hold, free_room());
    needed = probed.window_after - probed.window_before;
    qar_rviz_streaming::release_dock_hold(probe_hold);
  }
  ASSERT_GT(needed, 0);

  SharedColumn column(/*window_height=*/500, /*third_minimum=*/260);
  column.window.move(0, 200);
  column.settle();
  const QRect frame = column.window.frameGeometry();
  // The room has all the height the window needs, but ends 100 pixels short of its new lower edge
  // when the window stays where it is.
  qar_rviz_streaming::DockHeightOptions options;
  options.room = QRect(0, 0, 4000, frame.height() + needed + 100);
  ASSERT_GT(frame.bottom() + needed, options.room.bottom());
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, 480, hold, options);
  column.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kGrown);
  EXPECT_EQ(column.panel_dock.height(), 480);
  const QRect after = column.window.frameGeometry();
  EXPECT_EQ(after.height(), frame.height() + needed);
  EXPECT_EQ(after.bottom(), options.room.bottom()) << "the lower edge is at the end of the room";
  EXPECT_EQ(after.top(), frame.top() - 100) << "the window moved up by the overshoot";
  EXPECT_GE(after.top(), options.room.top());
  qar_rviz_streaming::release_dock_hold(hold);
}
TEST(StablePanelLayout, SetDockHeightTakesTheRoomFromTheScreenOfTheWindowByDefault)
{
  SharedColumn column(/*window_height=*/500, /*third_minimum=*/260);
  const QScreen * screen = column.window.screen();
  ASSERT_NE(screen, nullptr);
  const QRect available = screen->availableGeometry();
  const QRect frame = column.window.frameGeometry();
  ASSERT_LT(frame.height(), available.height()) << "the window fits the screen of the test";

  // Far more than the screen has: the window takes the room and not a pixel more.
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, 5000, hold);
  column.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kScreenBottom);
  const QRect after = column.window.frameGeometry();
  EXPECT_GT(after.height(), frame.height());
  EXPECT_LE(after.bottom(), available.bottom());
  EXPECT_GE(after.top(), available.top());
  EXPECT_EQ(after.height(), available.height()) << "the window fills the room";
  EXPECT_TRUE(layout_holds(column.window));
  qar_rviz_streaming::release_dock_hold(hold);
}

TEST(StablePanelLayout, KeptDocksKeepTheirHeightWhileAnotherDockGrows)
{
  // Control: a dock that is not kept gives some of its height to the dock that grows.
  int third_without_keep = 0;
  int third_before = 0;
  {
    SharedColumn column;
    third_before = column.third.height();
    qar_rviz_streaming::DockHold hold;
    qar_rviz_streaming::set_dock_height(column.panel_dock, 421, hold, free_room());
    column.settle();
    third_without_keep = column.third.height();
    qar_rviz_streaming::release_dock_hold(hold);
  }
  ASSERT_LT(third_without_keep, third_before) << "this check proves nothing if it did not give";

  SharedColumn column;
  ASSERT_EQ(column.third.height(), third_before);
  const QSize window_before = column.window.size();
  const int minimum_before = column.third.minimumHeight();
  const int maximum_before = column.third.maximumHeight();
  qar_rviz_streaming::DockHeightOptions options = free_room();
  options.keep = {&column.third, &column.panel_dock, nullptr};
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, 421, hold, options);
  column.settle();
  EXPECT_GE(result.got, 421);
  EXPECT_EQ(column.third.height(), third_before) << "the kept dock did not change";
  EXPECT_EQ(column.third.minimumHeight(), third_before);
  EXPECT_EQ(column.third.maximumHeight(), third_before);
  EXPECT_EQ(column.window.size(), window_before);

  qar_rviz_streaming::release_dock_hold(hold);
  column.settle();
  EXPECT_EQ(column.third.minimumHeight(), minimum_before);
  EXPECT_EQ(column.third.maximumHeight(), maximum_before);
  EXPECT_EQ(column.third.height(), third_before) << "its height stays as it was";
  EXPECT_EQ(column.panel_dock.maximumHeight(), maximum_before);
}

TEST(StablePanelLayout, KeptDocksMakeTheWindowGrowWhenNothingElseCanGiveWay)
{
  // The dock that grows has one neighbour and it is kept: the room comes from the window.
  SharedColumn column(/*window_height=*/700, /*third_minimum=*/40);
  qar_rviz_streaming::DockHeightOptions options = free_room();
  options.keep = {&column.displays, &column.third};
  const int displays_before = column.displays.height();
  const int third_before = column.third.height();
  const int window_before = column.window.height();
  const int asked = column.panel_dock.height() + 120;

  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, asked, hold, options);
  column.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kGrown);
  EXPECT_EQ(column.panel_dock.height(), asked);
  EXPECT_EQ(column.displays.height(), displays_before);
  EXPECT_EQ(column.third.height(), third_before);
  EXPECT_GE(column.window.height(), window_before + 120);
  qar_rviz_streaming::release_dock_hold(hold);
}

/// The shared left column of SharedColumn, and a dock alone in the right column.
class TwoColumns : public SharedColumn
{
public:
  explicit TwoColumns(int window_height = 500, int third_minimum = 260)
  : SharedColumn(window_height, third_minimum), right("Streamed")
  {
    right.setWidget(new SizedWidget(QSize(300, 200), QSize(100, 60)));
    window.addDockWidget(Qt::RightDockWidgetArea, &right);
    settle();
  }

  QDockWidget right;
};

TEST(StablePanelLayout, SetDockHeightDoesNotGrowTheWindowWhenAKeptDockSitsInAnotherColumn)
{
  const int asked = 480;
  // Control: the dock of the other column is not kept, so the window grows by the deficit.
  {
    TwoColumns column;
    const int window_before = column.window.height();
    qar_rviz_streaming::DockHold hold;
    const qar_rviz_streaming::DockHeightResult result =
      qar_rviz_streaming::set_dock_height(column.panel_dock, asked, hold, free_room());
    column.settle();
    ASSERT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kGrown)
      << "this check proves nothing if the window did not need to grow";
    EXPECT_GT(column.window.height(), window_before);
    qar_rviz_streaming::release_dock_hold(hold);
  }

  // A kept dock alone in the right column: the window stays as it is, and so does that dock,
  // while the hold lasts and after it has been released.
  TwoColumns column;
  const int window_before = column.window.height();
  const int right_before = column.right.height();
  const int right_minimum = column.right.minimumHeight();
  const int right_maximum = column.right.maximumHeight();
  const int minimum_before = column.panel_dock.minimumHeight();
  qar_rviz_streaming::DockHeightOptions options = free_room();
  options.keep = {&column.right};
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(column.panel_dock, asked, hold, options);
  column.settle();
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kStreamedElsewhere);
  EXPECT_EQ(column.window.height(), window_before) << "the window was not made higher";
  EXPECT_EQ(result.window_before, window_before);
  EXPECT_EQ(result.window_after, window_before);
  EXPECT_LT(result.got, asked) << "the dock got what the window can give it, and no more";
  EXPECT_TRUE(layout_holds(column.window)) << "no minimum is left that the layout cannot hold";
  EXPECT_EQ(column.right.height(), right_before) << "the kept dock kept its height";

  qar_rviz_streaming::release_dock_hold(hold);
  column.settle();
  EXPECT_EQ(column.right.height(), right_before) << "...and keeps it after the hold ended";
  EXPECT_EQ(column.right.minimumHeight(), right_minimum);
  EXPECT_EQ(column.right.maximumHeight(), right_maximum);
  EXPECT_EQ(column.window.height(), window_before);
  EXPECT_EQ(column.panel_dock.minimumHeight(), minimum_before);
}

TEST(StablePanelLayout, AKeptDockThatIsFloatingOrHiddenDoesNotStopTheWindowFromGrowing)
{
  for (const bool floating : {true, false}) {
    TwoColumns column;
    if (floating) {
      column.right.setFloating(true);
    } else {
      column.right.hide();
    }
    column.settle();
    const int window_before = column.window.height();
    qar_rviz_streaming::DockHeightOptions options = free_room();
    options.keep = {&column.right};
    qar_rviz_streaming::DockHold hold;
    const qar_rviz_streaming::DockHeightResult result =
      qar_rviz_streaming::set_dock_height(column.panel_dock, 480, hold, options);
    column.settle();
    EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kGrown)
      << (floating ? "floating" : "hidden");
    EXPECT_GT(column.window.height(), window_before) << (floating ? "floating" : "hidden");
    qar_rviz_streaming::release_dock_hold(hold);
  }
}

TEST(StablePanelLayout, HoldsOnOneDockEndWhateverTheOrderOfTheirReleases)
{
  for (const bool first_released_first : {true, false}) {
    SharedColumn column;
    const int minimum_before = column.third.minimumHeight();
    const int maximum_before = column.third.maximumHeight();
    qar_rviz_streaming::DockHeightOptions options = free_room();
    options.keep = {&column.third};
    qar_rviz_streaming::DockHold first;
    qar_rviz_streaming::DockHold second;
    qar_rviz_streaming::set_dock_height(column.panel_dock, 400, first, options);
    qar_rviz_streaming::set_dock_height(column.displays, 150, second, options);
    ASSERT_TRUE(first.active());
    ASSERT_TRUE(second.active());
    const int third_height = column.third.height();
    EXPECT_EQ(column.third.minimumHeight(), third_height);

    qar_rviz_streaming::DockHold & one = first_released_first ? first : second;
    qar_rviz_streaming::DockHold & other = first_released_first ? second : first;
    qar_rviz_streaming::release_dock_hold(one);
    EXPECT_EQ(column.third.minimumHeight(), third_height)
      << "the other hold still keeps the dock";
    EXPECT_EQ(column.third.maximumHeight(), third_height);
    qar_rviz_streaming::release_dock_hold(other);
    column.settle();
    EXPECT_EQ(column.third.minimumHeight(), minimum_before);
    EXPECT_EQ(column.third.maximumHeight(), maximum_before);

    // A hold that is empty changes nothing, and neither does a second release.
    qar_rviz_streaming::release_dock_hold(first);
    qar_rviz_streaming::release_dock_hold(second);
    EXPECT_EQ(column.third.minimumHeight(), minimum_before);
    EXPECT_EQ(column.third.maximumHeight(), maximum_before);
  }
}

TEST(StablePanelLayout, ReleasingAHoldOnADockThatIsGoneIsHarmless)
{
  qar_rviz_streaming::DockHold hold;
  {
    SharedColumn column;
    qar_rviz_streaming::DockHeightOptions options = free_room();
    options.keep = {&column.third};
    qar_rviz_streaming::set_dock_height(column.panel_dock, 421, hold, options);
    ASSERT_TRUE(hold.active());
  }
  qar_rviz_streaming::release_dock_hold(hold);
  EXPECT_FALSE(hold.active());
}

TEST(StablePanelLayout, SetDockHeightResizesAFloatingDockDirectly)
{
  QMainWindow window;
  window.resize(1200, 800);
  window.setCentralWidget(new QLabel("scene"));
  QDockWidget dock("Qar Streaming");
  dock.setWidget(new SharedColumn::SizedWidget(QSize(300, 200), QSize(100, 60)));
  window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
  QDockWidget other("Views");
  other.setWidget(new SharedColumn::SizedWidget(QSize(300, 200), QSize(100, 60)));
  window.addDockWidget(Qt::LeftDockWidgetArea, &other);
  window.show();
  dock.setFloating(true);
  for (int i = 0; i < 5; ++i) {
    QApplication::processEvents();
  }

  const int minimum_before = dock.minimumHeight();
  const int other_height = other.height();
  qar_rviz_streaming::DockHeightOptions options = free_room();
  options.keep = {&other};
  qar_rviz_streaming::DockHold hold;
  const qar_rviz_streaming::DockHeightResult result =
    qar_rviz_streaming::set_dock_height(dock, 421, hold, options);
  EXPECT_FALSE(result.held);
  EXPECT_FALSE(hold.active()) << "a floating dock takes nothing from the window's docks";
  EXPECT_EQ(result.window_before, 0);
  EXPECT_EQ(result.window, qar_rviz_streaming::WindowGrowth::kNotNeeded);
  EXPECT_EQ(dock.height(), 421);
  EXPECT_EQ(result.got, 421);
  EXPECT_EQ(dock.minimumHeight(), minimum_before);
  EXPECT_EQ(other.height(), other_height);
}

/// A scroll area (per-pixel scrolling) whose content is @p content_height pixels high, in a
/// viewport that is lower than that.
class PixelArea : public QScrollArea
{
public:
  explicit PixelArea(int content_height)
  {
    auto * content = new QWidget;
    content->setFixedSize(200, content_height);
    setWidget(content);
    resize(260, 300);
    show();
    QApplication::processEvents();
  }
};

/// A tree of @p rows rows in a window of @p height pixels, scrolling as @p mode says.
class RowTree : public QTreeWidget
{
public:
  RowTree(int rows, int height, QAbstractItemView::ScrollMode mode)
  {
    setHeaderHidden(true);
    for (int i = 0; i < rows; ++i) {
      addTopLevelItem(new QTreeWidgetItem(QStringList(QString("row %1").arg(i))));
    }
    setVerticalScrollMode(mode);
    resize(300, height);
    show();
    QApplication::processEvents();
    doItemsLayout();
  }
};

TEST(StablePanelLayout, VerticalOverflowOfAPerPixelAreaIsTheRangeOfItsBar)
{
  PixelArea area(600);
  ASSERT_TRUE(area.verticalScrollBar()->isVisible());
  const int overflow = qar_rviz_streaming::vertical_overflow(area);
  EXPECT_EQ(overflow, 600 - area.viewport()->height());
  EXPECT_EQ(qar_rviz_streaming::missing_content_height(area), overflow);

  // Made that much higher, it needs no bar.
  area.resize(area.width(), area.height() + overflow);
  QApplication::processEvents();
  EXPECT_FALSE(area.verticalScrollBar()->isVisible());
  EXPECT_EQ(qar_rviz_streaming::vertical_overflow(area), 0);
}

TEST(StablePanelLayout, VerticalOverflowIsZeroForABarThatIsAlwaysOffAndForAnAreaNotOnShow)
{
  PixelArea area(600);
  ASSERT_GT(qar_rviz_streaming::vertical_overflow(area), 0);
  area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  QApplication::processEvents();
  ASSERT_GT(area.verticalScrollBar()->maximum(), 0) << "the range is there, the bar is not";
  EXPECT_EQ(qar_rviz_streaming::vertical_overflow(area), 0);
  area.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  QApplication::processEvents();
  EXPECT_GT(qar_rviz_streaming::vertical_overflow(area), 0);
  area.hide();
  EXPECT_EQ(qar_rviz_streaming::vertical_overflow(area), 0) << "an area that is not on show";
}

TEST(StablePanelLayout, VerticalOverflowFollowsTheRangeBeforeTheBarIsShown)
{
  // A bar is shown a moment after its range changed (a queued call): the measure does not wait.
  RowTree tree(5, 300, QAbstractItemView::ScrollPerItem);
  ASSERT_FALSE(tree.verticalScrollBar()->isVisible());
  for (int i = 0; i < 100; ++i) {
    tree.addTopLevelItem(new QTreeWidgetItem(QStringList(QString("row %1").arg(i))));
  }
  tree.doItemsLayout();
  EXPECT_GT(tree.verticalScrollBar()->maximum(), 0);
  EXPECT_GT(qar_rviz_streaming::vertical_overflow(tree), 0);
  QApplication::processEvents();
  EXPECT_TRUE(tree.verticalScrollBar()->isVisible());
}

TEST(StablePanelLayout, VerticalOverflowOfAnItemViewThatScrollsPerItemCountsRows)
{
  RowTree tree(60, 200, QAbstractItemView::ScrollPerItem);
  ASSERT_TRUE(tree.verticalScrollBar()->isVisible());
  const int hidden_rows = tree.verticalScrollBar()->maximum() - tree.verticalScrollBar()->minimum();
  const int row = tree.sizeHintForRow(0);
  ASSERT_GT(hidden_rows, 0);
  ASSERT_GT(row, 0);
  const int overflow = qar_rviz_streaming::vertical_overflow(tree);
  EXPECT_EQ(overflow, hidden_rows * row) << "the rows still hidden times the height of a row";
  EXPECT_NE(overflow, hidden_rows) << "the bar counts rows, not pixels";

  // Made that much higher, it shows every row.
  tree.resize(tree.width(), tree.height() + overflow);
  QApplication::processEvents();
  tree.doItemsLayout();
  EXPECT_FALSE(tree.verticalScrollBar()->isVisible());
  EXPECT_EQ(qar_rviz_streaming::vertical_overflow(tree), 0);
}

TEST(StablePanelLayout, VerticalOverflowOfAnItemViewThatScrollsPerPixelIsTheRangeOfItsBar)
{
  RowTree tree(60, 200, QAbstractItemView::ScrollPerPixel);
  ASSERT_TRUE(tree.verticalScrollBar()->isVisible());
  EXPECT_EQ(
    qar_rviz_streaming::vertical_overflow(tree),
    tree.verticalScrollBar()->maximum() - tree.verticalScrollBar()->minimum());
}

TEST(StablePanelLayout, MissingContentHeightIsTheLargestOverflowOfTheVisibleAreasInside)
{
  QWidget content;
  auto * layout = new QVBoxLayout(&content);
  auto * small = new QScrollArea;
  auto * small_content = new QWidget;
  small_content->setFixedSize(100, 400);
  small->setWidget(small_content);
  auto * large = new QScrollArea;
  auto * large_content = new QWidget;
  large_content->setFixedSize(100, 900);
  large->setWidget(large_content);
  layout->addWidget(small);
  layout->addWidget(large);
  content.resize(300, 500);
  content.show();
  QApplication::processEvents();
  ASSERT_TRUE(small->verticalScrollBar()->isVisible());
  ASSERT_TRUE(large->verticalScrollBar()->isVisible());
  const int small_overflow = qar_rviz_streaming::vertical_overflow(*small);
  const int large_overflow = qar_rviz_streaming::vertical_overflow(*large);
  ASSERT_GT(large_overflow, small_overflow);
  EXPECT_EQ(qar_rviz_streaming::missing_content_height(content), large_overflow);

  // A bar that is hidden (another tab) counts for nothing.
  QTabWidget tabs;
  auto * first = new PixelArea(600);
  auto * second = new PixelArea(1500);
  tabs.addTab(first, "first");
  tabs.addTab(second, "second");
  tabs.resize(300, 300);
  tabs.show();
  QApplication::processEvents();
  EXPECT_EQ(
    qar_rviz_streaming::missing_content_height(tabs),
    qar_rviz_streaming::vertical_overflow(*first));
  tabs.setCurrentIndex(1);
  QApplication::processEvents();
  EXPECT_EQ(
    qar_rviz_streaming::missing_content_height(tabs),
    qar_rviz_streaming::vertical_overflow(*second));

  QWidget plain;
  plain.resize(100, 100);
  plain.show();
  EXPECT_EQ(qar_rviz_streaming::missing_content_height(plain), 0);
}

/// The stand-in of a Displays panel: a splitter with a tree and a help area over a row of
/// buttons, a top-level widget of a fixed size, as the hidden copy is.
class DisplaysStandIn : public QWidget
{
public:
  explicit DisplaysStandIn(int rows)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto * splitter = new QSplitter(Qt::Vertical);
    tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    for (int i = 0; i < rows; ++i) {
      tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("display %1").arg(i))));
    }
    help = new QTextBrowser;
    splitter->addWidget(tree);
    splitter->addWidget(help);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    layout->addWidget(splitter);
    auto * buttons = new QHBoxLayout;
    buttons->addWidget(new QPushButton("Add"));
    buttons->addWidget(new QPushButton("Remove"));
    layout->addLayout(buttons);
    help->hide();
    setFixedSize(480, 756);
    show();
    QApplication::processEvents();
  }

  QTreeWidget * tree = nullptr;
  QTextBrowser * help = nullptr;
};

TEST(StablePanelLayout, AFitAddsEightPixelsToWhatIsMissingAndResizesSixTimesAtMost)
{
  EXPECT_EQ(qar_rviz_streaming::kFitSlack, 8);
  EXPECT_EQ(qar_rviz_streaming::kMaxFitRounds, 6);
}

/// The height of one row of the stand-in's tree.
int standin_row_height()
{
  DisplaysStandIn probe(3);
  probe.tree->doItemsLayout();
  return probe.tree->sizeHintForRow(0);
}

TEST(StablePanelLayout, FitHeightToViewGrowsUntilTheTreeNeedsNoBar)
{
  // Rows for about a thousand pixels: more than the 756 the panel starts with, less than 1400.
  const int rows = 1000 / standin_row_height();
  DisplaysStandIn panel(rows);
  panel.tree->doItemsLayout();
  ASSERT_TRUE(panel.tree->verticalScrollBar()->isVisible()) << "this check proves nothing "
    "if the rows fit";
  const int rows_height = rows * panel.tree->sizeHintForRow(0);
  const qar_rviz_streaming::HeightFit fit =
    qar_rviz_streaming::fit_height_to_view(panel, *panel.tree, 480, 400, 1400);
  EXPECT_FALSE(fit.scroll_left);
  EXPECT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  EXPECT_EQ(panel.height(), fit.height);
  EXPECT_EQ(panel.width(), 480);
  EXPECT_GT(fit.height, 756);
  EXPECT_LE(fit.height, 1400);
  EXPECT_GE(panel.tree->viewport()->height(), rows_height);
  EXPECT_LE(fit.rounds, qar_rviz_streaming::kMaxFitRounds + 2);
  // No more than the rows and the slack: the tree has at most one row and the slack to spare.
  EXPECT_LE(
    panel.tree->viewport()->height() - rows_height,
    panel.tree->sizeHintForRow(0) + qar_rviz_streaming::kFitSlack);
}

TEST(StablePanelLayout, FitHeightToViewMakesAShortTreeLowerButNotBelowTheMinimum)
{
  DisplaysStandIn panel(5);
  panel.tree->doItemsLayout();
  ASSERT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  const qar_rviz_streaming::HeightFit fit =
    qar_rviz_streaming::fit_height_to_view(panel, *panel.tree, 480, 400, 1400);
  EXPECT_FALSE(fit.scroll_left);
  EXPECT_EQ(fit.height, 400) << "five rows need far less: the minimum";
  EXPECT_EQ(panel.height(), 400);

  // With a lower minimum the panel is fitted to its rows.
  DisplaysStandIn fitted(30);
  fitted.tree->doItemsLayout();
  const int row = fitted.tree->sizeHintForRow(0);
  const qar_rviz_streaming::HeightFit tight =
    qar_rviz_streaming::fit_height_to_view(fitted, *fitted.tree, 480, 100, 1400);
  EXPECT_FALSE(tight.scroll_left);
  EXPECT_FALSE(fitted.tree->verticalScrollBar()->isVisible());
  EXPECT_LT(tight.height, 756);
  EXPECT_GE(fitted.tree->viewport()->height(), 30 * row);
  EXPECT_LE(fitted.tree->viewport()->height(), 30 * row + row + qar_rviz_streaming::kFitSlack);
}

TEST(StablePanelLayout, FitHeightToViewStopsAtTheMaximum)
{
  DisplaysStandIn panel(5000);
  panel.tree->doItemsLayout();
  const qar_rviz_streaming::HeightFit fit =
    qar_rviz_streaming::fit_height_to_view(panel, *panel.tree, 480, 400, 1400);
  EXPECT_EQ(fit.height, 1400);
  EXPECT_EQ(panel.height(), 1400);
  EXPECT_TRUE(fit.scroll_left) << "more rows than the maximum holds";
  EXPECT_LE(fit.rounds, qar_rviz_streaming::kMaxFitRounds);
}

TEST(StablePanelLayout, FitHeightToViewCountsRowsThatWereAddedJustBefore)
{
  // An item view lays its items out on the next turn of the event loop; the fit does not wait for
  // it. Rows added a moment ago count.
  const int rows = 1000 / standin_row_height();
  DisplaysStandIn panel(3);
  panel.tree->doItemsLayout();
  ASSERT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  for (int i = 3; i < rows; ++i) {
    panel.tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("display %1").arg(i))));
  }
  const qar_rviz_streaming::HeightFit fit =
    qar_rviz_streaming::fit_height_to_view(panel, *panel.tree, 480, 400, 1400);
  EXPECT_FALSE(fit.scroll_left);
  EXPECT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  EXPECT_GE(panel.tree->viewport()->height(), rows * panel.tree->sizeHintForRow(0));
  EXPECT_GT(fit.height, 756);
}

TEST(StablePanelLayout, FitHeightToViewAddsTheSlackToWhatTheBarSaysIsMissing)
{
  // A list scrolls per pixel and is not a tree, so the fit only grows it: by exactly the missing
  // height and the slack.
  auto * window = new QWidget;
  auto * layout = new QVBoxLayout(window);
  layout->setContentsMargins(0, 0, 0, 0);
  auto * list = new QListWidget;
  for (int i = 0; i < 60; ++i) {
    list->addItem(QString("item %1").arg(i));
  }
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  layout->addWidget(list);
  layout->addWidget(new QPushButton("Add"));
  window->setFixedSize(300, 300);
  window->show();
  QApplication::processEvents();
  list->doItemsLayout();
  ASSERT_TRUE(list->verticalScrollBar()->isVisible());
  const int content = list->viewport()->height() + list->verticalScrollBar()->maximum();

  const qar_rviz_streaming::HeightFit fit =
    qar_rviz_streaming::fit_height_to_view(*window, *list, 300, 100, 2000);
  EXPECT_FALSE(fit.scroll_left);
  EXPECT_FALSE(list->verticalScrollBar()->isVisible());
  EXPECT_EQ(list->viewport()->height(), content + qar_rviz_streaming::kFitSlack);
  EXPECT_EQ(fit.rounds, 1);
  delete window;
}

TEST(StablePanelLayout, FitHeightToViewMeasuresOnlyTheViewItIsGiven)
{
  // A help area with a bar of its own does not make the panel grow for it.
  DisplaysStandIn panel(5);
  panel.help->setPlainText(QString("help line\n").repeated(400));
  panel.help->show();
  QApplication::processEvents();
  ASSERT_GT(qar_rviz_streaming::missing_content_height(*panel.help), 0);
  const qar_rviz_streaming::HeightFit fit =
    qar_rviz_streaming::fit_height_to_view(panel, *panel.tree, 480, 400, 1400);
  EXPECT_FALSE(fit.scroll_left);
  EXPECT_LT(fit.height, 1400);
}

TEST(StablePanelLayout, DescribePageFitSaysFitsAndScrolls)
{
  Skeleton panel;
  panel.add_page("Few", [](QVBoxLayout * page) { page->addWidget(new QLabel("one line")); });
  panel.add_page("Many", [](QVBoxLayout * page) {
      for (int i = 0; i < 80; ++i) {
        page->addWidget(new QPushButton(QString("button %1").arg(i)));
      }
    });
  panel.resize(420, 480);
  panel.show();
  QApplication::processEvents();

  const std::vector<std::string> lines =
    qar_rviz_streaming::describe_page_fit(*panel.tabs, panel.contents);
  ASSERT_EQ(lines.size(), 8u);
  EXPECT_EQ(lines[0].rfind("page 'Connection': content ", 0), 0u) << lines[0];
  EXPECT_NE(lines[0].find(" (min "), std::string::npos) << lines[0];
  EXPECT_NE(lines[0].find("), viewport "), std::string::npos) << lines[0];
  EXPECT_EQ(lines[6].rfind("page 'Few': ", 0), 0u) << lines[6];
  EXPECT_NE(lines[6].find(", fits"), std::string::npos) << lines[6];
  EXPECT_EQ(lines[6].find("SCROLLS"), std::string::npos) << lines[6];
  EXPECT_EQ(lines[7].rfind("page 'Many': ", 0), 0u) << lines[7];
  EXPECT_NE(lines[7].find(", SCROLLS by "), std::string::npos) << lines[7];
  EXPECT_NE(lines[7].find(" px"), std::string::npos) << lines[7];
}

// ---------------------------------------------------------------------------------------------
// The page shortfall
// ---------------------------------------------------------------------------------------------

TEST(StablePanelLayout, PageShortfallIsTheWorstScrollOfAllPagesWhetherOrNotOnShow)
{
  Skeleton panel;
  panel.add_page("Many", [](QVBoxLayout * page) {
      for (int i = 0; i < 80; ++i) {
        page->addWidget(new QPushButton(QString("button %1").arg(i)));
      }
    });
  panel.resize(420, 480);
  panel.show();
  QApplication::processEvents();
  ASSERT_EQ(panel.tabs->currentIndex(), 0) << "the page that scrolls is not the one on show";

  const std::vector<std::string> lines =
    qar_rviz_streaming::describe_page_fit(*panel.tabs, panel.contents);
  const std::string & many = lines.back();
  const std::size_t at = many.find("SCROLLS by ");
  ASSERT_NE(at, std::string::npos) << many;
  const int scrolls_by = std::stoi(many.substr(at + std::string("SCROLLS by ").size()));

  const qar_rviz_streaming::PageShortfall shortfall =
    qar_rviz_streaming::page_shortfall(*panel.tabs, panel.contents);
  EXPECT_EQ(shortfall.page, "page 'Many'");
  EXPECT_EQ(shortfall.px, scrolls_by) << "the same number as the log line of the page";

  // The census sees the page on show only, and that page fits.
  EXPECT_EQ(qar_qt_streaming::take_scroll_census(panel).stacked_overflow_px, 0);
}

TEST(StablePanelLayout, PageShortfallIsZeroWhenEveryPageFits)
{
  Skeleton panel;
  panel.resize(420, 1400);
  panel.show();
  QApplication::processEvents();
  const qar_rviz_streaming::PageShortfall shortfall =
    qar_rviz_streaming::page_shortfall(*panel.tabs, panel.contents);
  EXPECT_EQ(shortfall.px, 0);
  EXPECT_TRUE(shortfall.page.empty());
}

// ---------------------------------------------------------------------------------------------
// SettledHeightFit
// ---------------------------------------------------------------------------------------------

using qar_rviz_streaming::FitPolicy;
using qar_rviz_streaming::SettledHeightFit;
using FitAction = SettledHeightFit::Action;

/// One period of the member timer a caller of the fit runs: the event loop turns for a moment, so
/// that queued calls, delayed layouts and the ranges of the bars catch up.
void turn_event_loop(int milliseconds = 25)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
}

/// What a run of the fit did.
struct FitRun
{
  SettledHeightFit::Step last;
  int resizes = 0;
  int looks = 0;
  /// For every resize: the height before it and the height asked.
  std::vector<std::pair<int, int>> resized;
};

/// Runs @p fit the way the panel does: one look per period, the resize it asks for made before the
/// next look, up to @p limit looks. The widget gets @p give_percent of the height it is asked for.
FitRun run_fit(
  SettledHeightFit & fit, QWidget & panel, int width, int limit = 60, int give_percent = 100)
{
  FitRun run;
  for (int i = 0; i < limit; ++i) {
    turn_event_loop();
    const int before = panel.height();
    run.last = fit.advance(panel, before);
    ++run.looks;
    if (run.last.action == FitAction::Resize) {
      ++run.resizes;
      run.resized.emplace_back(before, run.last.target);
      panel.setFixedSize(width, before + (run.last.target - before) * give_percent / 100);
    } else if (run.last.action == FitAction::Done || run.last.action == FitAction::GaveUp) {
      break;
    }
  }
  return run;
}

/// The policy of the hidden Displays copy.
FitPolicy displays_policy()
{
  FitPolicy policy;
  policy.min_h = 400;
  policy.max_h = 1400;
  policy.row_slack = 1;
  policy.trim_to_rows = true;
  return policy;
}

TEST(SettledHeightFit, DoneNeedsTwoLooksThatAgree)
{
  DisplaysStandIn panel(5);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{FitPolicy()};

  const SettledHeightFit::Step first = fit.advance(panel, panel.height());
  EXPECT_EQ(first.action, FitAction::Wait) << "one look is no confirmation";
  EXPECT_TRUE(first.no_bar_seen);
  EXPECT_FALSE(first.honest_no_bar);
  EXPECT_EQ(first.missing_px, 0);
  EXPECT_EQ(first.rounds, 1);

  const SettledHeightFit::Step second = fit.advance(panel, panel.height());
  EXPECT_EQ(second.action, FitAction::Done);
  EXPECT_TRUE(second.honest_no_bar);
  EXPECT_EQ(second.rounds, 2);
  EXPECT_TRUE(second.reason.empty());
}

TEST(SettledHeightFit, TwoLooksAtDifferentHeightsDoNotAgree)
{
  DisplaysStandIn panel(5);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{FitPolicy()};

  EXPECT_EQ(fit.advance(panel, panel.height()).action, FitAction::Wait);
  // The height changed between the looks (a layout took some back): that is not a confirmation.
  panel.setFixedSize(480, 700);
  turn_event_loop();
  EXPECT_EQ(fit.advance(panel, panel.height()).action, FitAction::Wait);
  EXPECT_EQ(fit.advance(panel, panel.height()).action, FitAction::Done);
}

TEST(SettledHeightFit, RowsAddedOnAQueuedCallAreFoundByTheSettledFit)
{
  const int rows = 1000 / standin_row_height();
  DisplaysStandIn panel(3);
  panel.tree->doItemsLayout();
  turn_event_loop();
  ASSERT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  QMetaObject::invokeMethod(
    &panel,
    [&panel, rows]() {
      for (int i = 3; i < rows; ++i) {
        panel.tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("display %1").arg(i))));
      }
    },
    Qt::QueuedConnection);

  // A look right now finds nothing: the rows are not in the tree yet.
  SettledHeightFit early{FitPolicy()};
  const SettledHeightFit::Step immediate = early.advance(panel, panel.height());
  EXPECT_TRUE(immediate.no_bar_seen);
  EXPECT_EQ(immediate.missing_px, 0);

  FitPolicy policy;
  policy.min_h = 400;
  policy.max_h = 1400;
  SettledHeightFit fit{policy};
  const FitRun run = run_fit(fit, panel, 480);
  EXPECT_EQ(run.last.action, FitAction::Done);
  EXPECT_TRUE(run.last.honest_no_bar);
  EXPECT_GE(run.resizes, 1);
  EXPECT_GT(panel.height(), 756);
  EXPECT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  EXPECT_GE(panel.tree->viewport()->height(), rows * panel.tree->sizeHintForRow(0));
}

/// A list whose first row reports no height: the rows its bar counts cannot be turned into pixels.
class UnmeasurableList : public QListWidget
{
public:
  int sizeHintForRow(int) const override {return 0;}
};

TEST(SettledHeightFit, NoBarIsNeverSaidWhileAnAreaCannotBeMeasured)
{
  UnmeasurableList list;
  for (int i = 0; i < 60; ++i) {
    list.addItem(QString("item %1").arg(i));
  }
  list.setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  list.resize(200, 150);
  list.show();
  QApplication::processEvents();
  list.doItemsLayout();
  ASSERT_GT(list.verticalScrollBar()->maximum(), 0);

  SettledHeightFit fit{FitPolicy()};
  bool done_said = false;
  bool no_bar_said = false;
  SettledHeightFit::Step last;
  for (int i = 0; i < 40; ++i) {
    turn_event_loop(5);
    last = fit.advance(list, list.height());
    done_said = done_said || last.action == FitAction::Done;
    no_bar_said = no_bar_said || last.no_bar_seen;
    if (last.action == FitAction::GaveUp) {
      break;
    }
  }
  EXPECT_FALSE(done_said);
  EXPECT_FALSE(no_bar_said);
  EXPECT_EQ(last.action, FitAction::GaveUp);
  EXPECT_EQ(last.reason, qar_rviz_streaming::kFitReasonUnmeasured);
  EXPECT_GT(last.census.unmeasured, 0);
  EXPECT_FALSE(last.honest_no_bar);
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(last, 150),
    "fitted to 150 px in " + std::to_string(last.rounds) +
    " round(s): not confirmed (an area could not be measured)");
}

/// Two trees one above the other in a splitter, each with more rows than its share holds.
class TwoTreesInASplitter : public QWidget
{
public:
  explicit TwoTreesInASplitter(int rows_each)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto * splitter = new QSplitter(Qt::Vertical);
    for (QTreeWidget ** slot : {&first, &second}) {
      *slot = new QTreeWidget;
      (*slot)->setHeaderHidden(true);
      for (int i = 0; i < rows_each; ++i) {
        (*slot)->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("row %1").arg(i))));
      }
      splitter->addWidget(*slot);
    }
    layout->addWidget(splitter);
    setFixedSize(300, 300);
    show();
    QApplication::processEvents();
    first->doItemsLayout();
    second->doItemsLayout();
    QApplication::processEvents();
  }

  QTreeWidget * first = nullptr;
  QTreeWidget * second = nullptr;
};

TEST(SettledHeightFit, TwoTreesInASplitterAreSummedNotMaximised)
{
  TwoTreesInASplitter panel(40);
  turn_event_loop();
  const int a = qar_rviz_streaming::vertical_overflow(*panel.first);
  const int b = qar_rviz_streaming::vertical_overflow(*panel.second);
  ASSERT_GT(a, 0);
  ASSERT_GT(b, 0);

  FitPolicy policy;
  policy.max_h = 5000;
  SettledHeightFit fit{policy};
  const SettledHeightFit::Step look = fit.advance(panel, panel.height());
  ASSERT_EQ(look.action, FitAction::Resize);
  EXPECT_EQ(look.census.stacked_overflow_px, a + b);
  EXPECT_EQ(look.census.largest_overflow_px, std::max(a, b));
  EXPECT_EQ(look.missing_px, a + b + qar_rviz_streaming::kFitSlack)
    << "the bars of two trees one above the other add up";
  EXPECT_GT(look.missing_px, std::max(a, b) + qar_rviz_streaming::kFitSlack);
  EXPECT_EQ(look.target, panel.height() + look.missing_px);

  panel.setFixedSize(300, look.target);
  const FitRun run = run_fit(fit, panel, 300);
  EXPECT_EQ(run.last.action, FitAction::Done);
  EXPECT_FALSE(panel.first->verticalScrollBar()->isVisible());
  EXPECT_FALSE(panel.second->verticalScrollBar()->isVisible());
  EXPECT_EQ(qar_qt_streaming::take_scroll_census(panel).stacked_overflow_px, 0);
}

TEST(SettledHeightFit, AnAreaThatScrollsByDesignNeverMakesTheWidgetTaller)
{
  DisplaysStandIn panel(5);
  panel.help->setPlainText(QString("help line\n").repeated(400));
  panel.help->show();
  turn_event_loop();
  ASSERT_GT(qar_rviz_streaming::missing_content_height(*panel.help), 0);
  SettledHeightFit fit{displays_policy()};
  const FitRun run = run_fit(fit, panel, 480);
  EXPECT_EQ(run.last.action, FitAction::Done);
  EXPECT_LT(panel.height(), 1400);

  // A tree that is marked as scrolling by design is left alone as well.
  DisplaysStandIn marked(5000);
  marked.tree->setProperty(qar_qt_streaming::kScrollsByDesignProperty, true);
  turn_event_loop();
  SettledHeightFit quiet{displays_policy()};
  const FitRun marked_run = run_fit(quiet, marked, 480);
  EXPECT_EQ(marked_run.last.action, FitAction::Done);
  EXPECT_EQ(marked_run.last.missing_px, 0);
}

TEST(SettledHeightFit, GivesUpAtTheHeightLimitAndSaysSo)
{
  DisplaysStandIn panel(5000);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{displays_policy()};
  const FitRun run = run_fit(fit, panel, 480);
  EXPECT_EQ(run.last.action, FitAction::GaveUp);
  EXPECT_EQ(run.last.reason, qar_rviz_streaming::kFitReasonHeight);
  EXPECT_EQ(panel.height(), 1400);
  EXPECT_GT(run.last.largest_px, 0);
  const std::string result = qar_rviz_streaming::describe_fit_result(run.last, panel.height());
  EXPECT_NE(result.find("fitted to 1400 px in "), std::string::npos) << result;
  EXPECT_NE(result.find(": a scroll bar of "), std::string::npos) << result;
  EXPECT_NE(result.find(" px is left (the height limit)"), std::string::npos) << result;
}

TEST(SettledHeightFit, GivesUpAtTheRoundLimitAndSaysSo)
{
  DisplaysStandIn panel(5000);
  panel.tree->doItemsLayout();
  turn_event_loop();
  FitPolicy policy;
  policy.min_h = 400;
  policy.max_h = 100000;
  policy.max_rounds = 2;
  SettledHeightFit fit{policy};
  // The widget gets a fifth of the height it is asked for, so no round is enough.
  const FitRun run = run_fit(fit, panel, 480, 60, 20);
  EXPECT_EQ(run.last.action, FitAction::GaveUp);
  EXPECT_EQ(run.last.reason, qar_rviz_streaming::kFitReasonRounds);
  EXPECT_EQ(run.resizes, 2) << "no resize beyond the bound";
  EXPECT_NE(
    qar_rviz_streaming::describe_fit_result(run.last, panel.height()).find("(the round limit)"),
    std::string::npos);
}

TEST(SettledHeightFit, GivesUpWhenTheWidgetDoesNotGrowAtAll)
{
  DisplaysStandIn panel(5000);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{displays_policy()};
  const FitRun run = run_fit(fit, panel, 480, 60, 0);
  EXPECT_EQ(run.last.action, FitAction::GaveUp);
  EXPECT_EQ(run.last.reason, qar_rviz_streaming::kFitReasonNoGrowth);
  EXPECT_EQ(run.resizes, 1) << "a widget that does not grow is not asked again";
  // A caller that knows better puts its own words in.
  EXPECT_NE(
    qar_rviz_streaming::describe_fit_result(run.last, panel.height(), "the window is maximized")
    .find(" px is left (the window is maximized)"),
    std::string::npos);
}

TEST(SettledHeightFit, ATreeThatNeedsLessIsMadeLowerOnceButNotBelowTheMinimum)
{
  DisplaysStandIn panel(5);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{displays_policy()};
  const FitRun run = run_fit(fit, panel, 480);
  EXPECT_EQ(run.last.action, FitAction::Done);
  EXPECT_EQ(panel.height(), 400) << "five rows need far less: the minimum";
  EXPECT_EQ(run.resizes, 1);
  ASSERT_EQ(run.resized.size(), 1u);
  EXPECT_LT(run.resized[0].second, run.resized[0].first) << "the one resize was a trim";
}

TEST(SettledHeightFit, ATrimLeavesTheSlackAndOneSpareRow)
{
  DisplaysStandIn panel(30);
  panel.tree->doItemsLayout();
  turn_event_loop();
  const int row = panel.tree->sizeHintForRow(0);
  FitPolicy policy = displays_policy();
  policy.min_h = 100;
  SettledHeightFit fit{policy};
  const FitRun run = run_fit(fit, panel, 480);
  EXPECT_EQ(run.last.action, FitAction::Done);
  EXPECT_FALSE(panel.tree->verticalScrollBar()->isVisible());
  EXPECT_LT(panel.height(), 756);
  EXPECT_GE(panel.tree->viewport()->height(), 30 * row + row) << "one spare row";
  EXPECT_LE(
    panel.tree->viewport()->height(), 30 * row + row + qar_rviz_streaming::kFitSlack + row);
}

TEST(SettledHeightFit, ASpareRowIsAddedToEveryGrowth)
{
  const int row = standin_row_height();
  const int rows = 1000 / row;
  const auto fitted_viewport = [rows](int row_slack) {
      DisplaysStandIn panel(rows);
      panel.tree->doItemsLayout();
      turn_event_loop();
      FitPolicy policy = displays_policy();
      policy.trim_to_rows = false;
      policy.row_slack = row_slack;
      SettledHeightFit fit{policy};
      const FitRun run = run_fit(fit, panel, 480);
      EXPECT_EQ(run.last.action, FitAction::Done);
      return panel.tree->viewport()->height();
    };
  const int plain = fitted_viewport(0);
  const int roomy = fitted_viewport(1);
  EXPECT_EQ(roomy - plain, row) << "one row more than a fit without a spare row";
}

TEST(SettledHeightFit, AWidgetThatHadToGrowIsNeverTrimmed)
{
  const int rows = 1000 / standin_row_height();
  DisplaysStandIn panel(rows);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{displays_policy()};
  const FitRun run = run_fit(fit, panel, 480);
  EXPECT_EQ(run.last.action, FitAction::Done);
  ASSERT_FALSE(run.resized.empty());
  for (const auto & resize : run.resized) {
    EXPECT_GT(resize.second, resize.first) << "every resize made the widget higher";
  }
}

/// A shared left column: a dock above and below the one that holds a tree.
class ColumnWithATree
{
public:
  ColumnWithATree()
  {
    window.resize(1000, 900);
    window.setCentralWidget(new QLabel("scene"));
    above.setWindowTitle("Above");
    above.setWidget(new SharedColumn::SizedWidget(QSize(300, 150), QSize(100, 60)));
    window.addDockWidget(Qt::LeftDockWidgetArea, &above);
    views.setWindowTitle("Views");
    tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    for (int i = 0; i < 40; ++i) {
      tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("row %1").arg(i))));
    }
    views.setWidget(tree);
    window.addDockWidget(Qt::LeftDockWidgetArea, &views);
    below.setWindowTitle("Below");
    below.setWidget(new SharedColumn::SizedWidget(QSize(300, 150), QSize(100, 60)));
    window.addDockWidget(Qt::LeftDockWidgetArea, &below);
    window.show();
    for (int i = 0; i < 5; ++i) {
      QApplication::processEvents();
    }
    tree->doItemsLayout();
  }

  QMainWindow window;
  QDockWidget above;
  QDockWidget views;
  QDockWidget below;
  QTreeWidget * tree = nullptr;
};

TEST(SettledHeightFit, ADockIsLookedAtAgainAfterItsHoldIsReleased)
{
  // Grows the dock under a hold until a look finds no bar, as the panel does, and returns that
  // look. The hold is still on when it returns.
  const auto fit_under_a_hold = [](ColumnWithATree & column, SettledHeightFit & fit,
      qar_rviz_streaming::DockHold & hold) {
      SettledHeightFit::Step step;
      for (int i = 0; i < 20; ++i) {
        turn_event_loop();
        step = fit.advance(*column.tree, column.views.height());
        if (step.action != FitAction::Resize) {
          break;
        }
        qar_rviz_streaming::set_dock_height(column.views, step.target, hold, free_room());
      }
      return step;
    };

  FitPolicy policy;
  policy.max_h = 3000;
  {
    // Nothing takes the height back: the look after the release agrees with the one before it.
    ColumnWithATree column;
    SettledHeightFit fit{policy};
    qar_rviz_streaming::DockHold hold;
    const SettledHeightFit::Step held = fit_under_a_hold(column, fit, hold);
    ASSERT_EQ(held.action, FitAction::Wait);
    ASSERT_TRUE(held.no_bar_seen);
    qar_rviz_streaming::release_dock_hold(hold);
    EXPECT_FALSE(hold.active());
    turn_event_loop();
    const SettledHeightFit::Step after = fit.advance(*column.tree, column.views.height());
    EXPECT_EQ(after.action, FitAction::Done);
    EXPECT_TRUE(after.honest_no_bar);
  }
  {
    // The layout takes height back once the hold is gone: the look after the release finds a bar,
    // and the fit is not done.
    ColumnWithATree column;
    SettledHeightFit fit{policy};
    qar_rviz_streaming::DockHold hold;
    const SettledHeightFit::Step held = fit_under_a_hold(column, fit, hold);
    ASSERT_TRUE(held.no_bar_seen);
    qar_rviz_streaming::release_dock_hold(hold);
    column.window.resizeDocks({&column.views}, {column.views.height() - 200}, Qt::Vertical);
    turn_event_loop();
    const SettledHeightFit::Step after = fit.advance(*column.tree, column.views.height());
    EXPECT_GT(after.census.stacked_overflow_px, 0);
    EXPECT_EQ(after.action, FitAction::Resize);
    EXPECT_FALSE(after.honest_no_bar);
  }
}

TEST(SettledHeightFit, ANeedTheCensusCannotSeeCountsAndIsNamed)
{
  DisplaysStandIn panel(5);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{FitPolicy()};
  qar_rviz_streaming::ExtraNeed extra;
  extra.px = 45;
  extra.what = "page 'AR tools'";
  const SettledHeightFit::Step look = fit.advance(panel, panel.height(), extra);
  EXPECT_EQ(look.action, FitAction::Resize);
  EXPECT_EQ(look.missing_px, 45 + qar_rviz_streaming::kFitSlack);
  EXPECT_FALSE(look.no_bar_seen);
  EXPECT_EQ(look.largest_px, 45);
  EXPECT_EQ(look.largest_what, "page 'AR tools'");
  EXPECT_NE(
    qar_rviz_streaming::describe_fit_round(look).find("largest page 'AR tools' 45 px)"),
    std::string::npos);
}

TEST(DockGrowthReason, AWindowThatLimitedTheDockIsNamed)
{
  using qar_rviz_streaming::dock_growth_reason;
  using qar_rviz_streaming::WindowGrowth;
  EXPECT_EQ(dock_growth_reason(WindowGrowth::kMaximized, false), "the window is maximized");
  EXPECT_EQ(dock_growth_reason(WindowGrowth::kFullScreen, false), "the window is full screen");
  EXPECT_EQ(
    dock_growth_reason(WindowGrowth::kStreamedElsewhere, false),
    "a streamed dock in another column");
  EXPECT_EQ(
    dock_growth_reason(WindowGrowth::kScreenBottom, false), "its window gave it no more room");
}

TEST(DockGrowthReason, AWindowThatGrewOrDidNotHaveToIsNotBlamed)
{
  using qar_rviz_streaming::dock_growth_reason;
  using qar_rviz_streaming::WindowGrowth;
  // "its window gave it no more room" is said when the window really limited the dock, not for a
  // dock that stayed low although the window grew.
  EXPECT_TRUE(dock_growth_reason(WindowGrowth::kGrown, false).empty());
  EXPECT_TRUE(dock_growth_reason(WindowGrowth::kNotNeeded, false).empty());
}

TEST(DockGrowthReason, ADockThatLostItsHeightAfterTheHoldWasReleasedSaysSo)
{
  using qar_rviz_streaming::dock_growth_reason;
  using qar_rviz_streaming::WindowGrowth;
  const std::string words = "the layout took the height back after the hold was released";
  EXPECT_EQ(dock_growth_reason(WindowGrowth::kGrown, true), words);
  EXPECT_EQ(dock_growth_reason(WindowGrowth::kNotNeeded, true), words);
  // A window that limited the dock stays the reason: it is why the layout took the height back.
  EXPECT_EQ(dock_growth_reason(WindowGrowth::kMaximized, true), "the window is maximized");
  EXPECT_EQ(
    dock_growth_reason(WindowGrowth::kScreenBottom, true), "its window gave it no more room");
}

TEST(DockGrowthReason, TheWordsTakeThePlaceOfTheReasonOfTheFitInItsLastLine)
{
  using qar_rviz_streaming::dock_growth_reason;
  using qar_rviz_streaming::WindowGrowth;
  SettledHeightFit::Step step;
  step.action = FitAction::GaveUp;
  step.reason = qar_rviz_streaming::kFitReasonNoGrowth;
  step.rounds = 5;
  step.largest_px = 120;
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(
      step, 400, dock_growth_reason(WindowGrowth::kGrown, true)),
    "fitted to 400 px in 5 round(s): a scroll bar of 120 px is left (the layout took the height "
    "back after the hold was released)");
  // Nothing more is known: the words of the fit itself stay.
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(
      step, 400, dock_growth_reason(WindowGrowth::kGrown, false)),
    "fitted to 400 px in 5 round(s): a scroll bar of 120 px is left (it did not grow)");
}

TEST(StartFit, TheReasonsNotToFitTheDockAreGivenInOrder)
{
  using qar_rviz_streaming::start_fit_skip_reason;
  using qar_rviz_streaming::StartFitFacts;
  const std::string streaming = "an AR panel is already streaming and a resize would rebuild it";

  StartFitFacts facts;
  facts.in_dock = true;
  facts.dock_visible = true;
  EXPECT_TRUE(start_fit_skip_reason(facts).empty());

  EXPECT_EQ(start_fit_skip_reason(StartFitFacts()), "the panel is not in a dock");
  StartFitFacts hidden = facts;
  hidden.dock_visible = false;
  EXPECT_EQ(start_fit_skip_reason(hidden), "its dock is not visible");
  StartFitFacts busy = facts;
  busy.ar_panel_streams = true;
  EXPECT_EQ(start_fit_skip_reason(busy), streaming);
  StartFitFacts fitting = facts;
  fitting.stream_fit_runs = true;
  EXPECT_EQ(
    start_fit_skip_reason(fitting), "the fit for its stream is already looking at the dock");

  // Several reasons at once: the first one in the order above is the one given.
  StartFitFacts all = facts;
  all.ar_panel_streams = true;
  all.stream_fit_runs = true;
  EXPECT_EQ(start_fit_skip_reason(all), streaming);
  all.dock_visible = false;
  EXPECT_EQ(start_fit_skip_reason(all), "its dock is not visible");
  all.in_dock = false;
  EXPECT_EQ(start_fit_skip_reason(all), "the panel is not in a dock");
}

TEST(StartFit, TheLineSaysWhatBecameOfTheDock)
{
  using qar_rviz_streaming::describe_start_fit;
  const std::string confirmed = "fitted to 640 px in 3 round(s), confirmed: no scroll bar";
  EXPECT_EQ(
    describe_start_fit(421, 640, true, confirmed), "dock 421 -> 640 px (" + confirmed + ")");
  EXPECT_EQ(
    describe_start_fit(640, 640, true, confirmed),
    "dock 640 px already high enough (" + confirmed + ")");

  // A bar that is left is not "high enough", whether the dock grew or not.
  const std::string left =
    "fitted to 560 px in 4 round(s): a scroll bar of 85 px is left (the height limit)";
  EXPECT_EQ(describe_start_fit(421, 560, false, left), "dock 421 -> 560 px (" + left + ")");
  EXPECT_EQ(describe_start_fit(560, 560, false, left), "dock 560 px (" + left + ")");
  // A fit with nothing to say is the heights alone.
  EXPECT_EQ(describe_start_fit(300, 300, false, std::string()), "dock 300 px");
}

TEST(SettledHeightFit, TheLogLinesHaveTheShapeTheScorecardReads)
{
  SettledHeightFit::Step step;
  step.rounds = 1;
  step.height = 161;
  step.missing_px = 127;
  qar_qt_streaming::ScrollAreaCensus area;
  area.class_name = "rviz_common::properties::PropertyTreeWidget";
  area.overflow_px = 119;
  step.census.areas.push_back(area);
  step.largest_px = 119;
  step.largest_what = "rviz_common::properties::PropertyTreeWidget ''";
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_round(step),
    "fit round 1: 161 px, missing 127 px (1 area(s) measured, 0 unmeasured; largest "
    "rviz_common::properties::PropertyTreeWidget '' 119 px)");

  step.rounds = 2;
  step.height = 288;
  step.missing_px = 0;
  step.largest_px = 0;
  step.largest_what.clear();
  step.census.unmeasured = 1;
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_round(step),
    "fit round 2: 288 px, missing 0 px (0 area(s) measured, 1 unmeasured; largest none)");

  step.census.unmeasured = 0;
  step.action = FitAction::Done;
  step.honest_no_bar = true;
  step.rounds = 3;
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(step, 288),
    "fitted to 288 px in 3 round(s), confirmed: no scroll bar");

  step.action = FitAction::GaveUp;
  step.honest_no_bar = false;
  step.largest_px = 20;
  step.reason = qar_rviz_streaming::kFitReasonRounds;
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(step, 288),
    "fitted to 288 px in 3 round(s): a scroll bar of 20 px is left (the round limit)");
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(step, 288, "its window gave it no more room"),
    "fitted to 288 px in 3 round(s): a scroll bar of 20 px is left (its window gave it no more "
    "room)");

  step.largest_px = 0;
  step.reason = qar_rviz_streaming::kFitReasonUnsettled;
  EXPECT_EQ(
    qar_rviz_streaming::describe_fit_result(step, 288),
    "fitted to 288 px in 3 round(s): not confirmed (it did not settle)");
}

/// A scroll area with a content of @p content_width by @p content_height pixels in a window of
/// @p width by @p height.
class ContentArea : public QScrollArea
{
public:
  ContentArea(int content_width, int content_height, int width, int height)
  {
    auto * content = new QWidget;
    content->setFixedSize(content_width, content_height);
    setWidget(content);
    resize(width, height);
    show();
    QApplication::processEvents();
  }
};

TEST(SettledHeightFit, AHorizontalBarIsNoReasonToGrowAndIsNotSaidToBeNoBar)
{
  // Wider than its viewport and much lower: a horizontal bar only.
  ContentArea area(600, 100, 200, 300);
  turn_event_loop();
  ASSERT_TRUE(area.horizontalScrollBar()->isVisible());
  ASSERT_EQ(area.verticalScrollBar()->maximum(), 0);
  SettledHeightFit fit{FitPolicy()};

  const SettledHeightFit::Step first = fit.advance(area, area.height());
  EXPECT_EQ(first.action, FitAction::Wait);
  EXPECT_TRUE(first.height_ok)
    << "a taller widget would change nothing: a hold on the height can go";
  EXPECT_FALSE(first.no_bar_seen) << "a horizontal bar is a bar";
  EXPECT_EQ(first.missing_px, 0) << "nothing to make the widget taller by";
  EXPECT_GT(first.largest_px, 0);
  // The census leaves a bar that a taller widget keeps out of its sums, and the log names it all
  // the same: it is a bar the operator would see.
  EXPECT_EQ(first.census.stacked_overflow_px, 0);
  EXPECT_EQ(first.census.largest_overflow_px, 0);
  EXPECT_EQ(first.largest_px, first.census.horizontal_only_px);
  EXPECT_EQ(first.largest_what, "QScrollArea ''");

  const SettledHeightFit::Step second = fit.advance(area, area.height());
  EXPECT_EQ(second.action, FitAction::GaveUp);
  EXPECT_EQ(second.reason, qar_rviz_streaming::kFitReasonHorizontal);
  EXPECT_FALSE(second.honest_no_bar);
  EXPECT_EQ(second.resizes, 0) << "the widget was never asked to grow";
  const std::string result = qar_rviz_streaming::describe_fit_result(second, area.height());
  EXPECT_NE(result.find(": a scroll bar of "), std::string::npos) << result;
  EXPECT_NE(
    result.find(" px is left (a horizontal bar: a taller widget does not remove it)"),
    std::string::npos) << result;
}

TEST(SettledHeightFit, AHorizontalBarNextToAVerticalOverflowCountsWithIt)
{
  ContentArea area(600, 700, 200, 300);   // both bars
  turn_event_loop();
  SettledHeightFit fit{FitPolicy()};
  const SettledHeightFit::Step look = fit.advance(area, area.height());
  ASSERT_EQ(look.census.areas.size(), 1u);
  const qar_qt_streaming::ScrollAreaCensus & a = look.census.areas[0];
  ASSERT_TRUE(a.h_bar);
  ASSERT_GT(a.overflow_px, 0);
  EXPECT_EQ(look.action, FitAction::Resize);
  EXPECT_EQ(look.missing_px, a.overflow_px + a.h_bar_extent + qar_rviz_streaming::kFitSlack);
  EXPECT_EQ(look.census.stacked_overflow_px, a.overflow_px + a.h_bar_extent)
    << "the census and the fit agree on what a taller widget removes";
}

TEST(SettledHeightFit, AWidgetThatCannotBeMadeHigherIsGivenUpOnlyWhenTwoLooksAgree)
{
  DisplaysStandIn panel(5000);
  panel.tree->doItemsLayout();
  turn_event_loop();
  SettledHeightFit fit{displays_policy()};

  const SettledHeightFit::Step grow = fit.advance(panel, panel.height());
  ASSERT_EQ(grow.action, FitAction::Resize);
  EXPECT_EQ(grow.target, 1400) << "as high as the policy allows";
  panel.setFixedSize(480, grow.target);
  turn_event_loop();

  const SettledHeightFit::Step first = fit.advance(panel, panel.height());
  EXPECT_EQ(first.action, FitAction::Wait)
    << "the first look at the limit may have caught the layout on its way";
  EXPECT_FALSE(first.height_ok);
  EXPECT_GT(first.missing_px, 0);
  const SettledHeightFit::Step second = fit.advance(panel, panel.height());
  EXPECT_EQ(second.action, FitAction::GaveUp);
  EXPECT_EQ(second.reason, qar_rviz_streaming::kFitReasonHeight);
  EXPECT_EQ(second.rounds, 3);
}

TEST(SettledHeightFit, AStuckLookThatTheNextOneDoesNotRepeatIsNotAGiveUp)
{
  // The first look at the limit still sees a bar, the next one does not: the layout had not
  // settled.
  DisplaysStandIn panel(5000);
  panel.tree->doItemsLayout();
  turn_event_loop();
  FitPolicy policy = displays_policy();
  policy.trim_to_rows = false;
  SettledHeightFit fit{policy};
  const SettledHeightFit::Step grow = fit.advance(panel, panel.height());
  panel.setFixedSize(480, grow.target);
  turn_event_loop();
  EXPECT_EQ(fit.advance(panel, panel.height()).action, FitAction::Wait);
  // Most of the rows go away before the next look.
  while (panel.tree->topLevelItemCount() > 5) {
    delete panel.tree->takeTopLevelItem(0);
  }
  turn_event_loop();
  panel.tree->doItemsLayout();
  const SettledHeightFit::Step after = fit.advance(panel, panel.height());
  EXPECT_NE(after.action, FitAction::GaveUp);
  EXPECT_TRUE(after.no_bar_seen);
}

// ---------------------------------------------------------------------------------------------
// The readout box
// ---------------------------------------------------------------------------------------------

constexpr const char * kMoreFormat = "... and %1 more panel(s); all counters are in the log";

/// A page-like parent for the readout box, as wide as the panel.
class ReadoutHost : public QWidget
{
public:
  explicit ReadoutHost(int width = 460)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    box = qar_rviz_streaming::make_readout_box(
      this, QStringLiteral("AR panels: none"), QStringLiteral("tip"));
    layout->addWidget(box.area);
    layout->addStretch(1);
    resize(width, 500);
    show();
    QApplication::processEvents();
  }

  qar_rviz_streaming::ReadoutBox box;
};

QStringList panel_lines(int count, const QString & suffix = QString())
{
  QStringList lines;
  for (int i = 0; i < count; ++i) {
    lines.append(QString("dock:Panel%1: 475x421 px, sent %2%3").arg(i).arg(1000 + i).arg(suffix));
  }
  return lines;
}

QStringList readout_head()
{
  return {"AR panels:", "input: in 0, missed 0, dropped 0",
    "panels: live=4 closed=0 (destroyed) notice=0"};
}

TEST(ReadoutBox, IsBuiltSoThatNothingCanMoveItOrScrollIt)
{
  ReadoutHost host;
  const qar_rviz_streaming::ReadoutBox & box = host.box;
  EXPECT_EQ(
    box.area->height(),
    box.area->fontMetrics().lineSpacing() * qar_rviz_streaming::kReadoutLines + 8);
  EXPECT_EQ(box.area->minimumHeight(), box.area->maximumHeight()) << "a fixed height";
  EXPECT_EQ(box.area->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  EXPECT_EQ(box.area->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  EXPECT_EQ(box.area->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  EXPECT_EQ(box.label->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  EXPECT_EQ(box.label->textFormat(), Qt::PlainText);
  EXPECT_TRUE(box.label->wordWrap());
  EXPECT_TRUE(box.area->property(qar_qt_streaming::kScrollsByDesignProperty).toBool());
  EXPECT_EQ(box.area->toolTip(), "tip");
  EXPECT_EQ(box.label->text(), "AR panels: none");
}

TEST(ReadoutBox, FourPanelsFitWithoutABarOrAMoreLine)
{
  ReadoutHost host;
  const int dropped = qar_rviz_streaming::set_readout_text(
    host.box, readout_head(), panel_lines(4), kMoreFormat);
  QApplication::processEvents();
  EXPECT_EQ(dropped, 0);
  EXPECT_EQ(host.box.label->text().split('\n').size(), 7);
  EXPECT_FALSE(host.box.label->text().contains("more panel(s)"));
  EXPECT_EQ(host.box.area->verticalScrollBar()->maximum(), 0) << "nothing to scroll";
  EXPECT_FALSE(host.box.area->verticalScrollBar()->isVisible());
  EXPECT_EQ(qar_qt_streaming::take_scroll_census(host).stacked_overflow_px, 0);
}

TEST(ReadoutBox, PanelLinesThatDoNotFitAreDroppedFromTheEndAndCounted)
{
  ReadoutHost host;
  const int total = 60;
  const int dropped = qar_rviz_streaming::set_readout_text(
    host.box, readout_head(), panel_lines(total), kMoreFormat);
  QApplication::processEvents();
  ASSERT_GT(dropped, 0);
  ASSERT_LT(dropped, total);
  const QStringList shown = host.box.label->text().split('\n');
  EXPECT_EQ(shown.last(), QString(kMoreFormat).arg(dropped));
  EXPECT_EQ(shown.size(), readout_head().size() + (total - dropped) + 1);
  EXPECT_EQ(shown[readout_head().size()], "dock:Panel0: 475x421 px, sent 1000")
    << "the first panels stay";
  EXPECT_FALSE(host.box.label->text().contains(QString("dock:Panel%1:").arg(total - 1)));
  EXPECT_EQ(host.box.area->verticalScrollBar()->maximum(), 0) << "the shortened text fits";
}

TEST(ReadoutBox, AWrappedLineCountsForAllTheLinesItTakes)
{
  ReadoutHost host;
  const QString tail = QString(" [") + QString(120, QLatin1Char('x')) + QStringLiteral("]");
  const int plain = qar_rviz_streaming::set_readout_text(
    host.box, readout_head(), panel_lines(30), kMoreFormat);
  const int wrapped = qar_rviz_streaming::set_readout_text(
    host.box, readout_head(), panel_lines(30, tail), kMoreFormat);
  QApplication::processEvents();
  EXPECT_GT(wrapped, plain) << "lines that wrap take more room, so fewer fit";
  EXPECT_EQ(host.box.area->verticalScrollBar()->maximum(), 0);

  // The width of the text is a parameter: a narrower box drops more.
  const int wide = qar_rviz_streaming::set_readout_text(
    host.box, readout_head(), panel_lines(30), kMoreFormat, 600);
  const int narrow = qar_rviz_streaming::set_readout_text(
    host.box, readout_head(), panel_lines(30), kMoreFormat, 160);
  EXPECT_GT(narrow, wide);
}

TEST(ReadoutBox, TheMeasureAgreesWithTheLabelItself)
{
  ReadoutHost host;
  for (const int count : {0, 1, 4, 8, 11, 12, 13, 14, 15, 16, 20, 40}) {
    qar_rviz_streaming::set_readout_text(host.box, readout_head(), panel_lines(count), kMoreFormat);
    turn_event_loop(5);
    EXPECT_EQ(host.box.area->verticalScrollBar()->maximum(), 0) << count << " panels";
    EXPECT_LE(
      host.box.label->heightForWidth(host.box.label->width()),
      host.box.area->viewport()->height()) << count << " panels";
  }
}

TEST(ReadoutBox, EveryLineThatFitsIsKeptAndOneMoreIsNot)
{
  ReadoutHost host;
  // The most panels that fit with no line dropped.
  int most = 0;
  while (most < 40 &&
    qar_rviz_streaming::set_readout_text(
      host.box, readout_head(), panel_lines(most + 1), kMoreFormat) == 0)
  {
    ++most;
  }
  ASSERT_GT(most, 4);
  ASSERT_LT(most, 40);
  EXPECT_EQ(
    qar_rviz_streaming::set_readout_text(
      host.box, readout_head(), panel_lines(most), kMoreFormat), 0);
  EXPECT_GE(
    qar_rviz_streaming::set_readout_text(
      host.box, readout_head(), panel_lines(most + 1), kMoreFormat), 1);
}

}  // namespace
