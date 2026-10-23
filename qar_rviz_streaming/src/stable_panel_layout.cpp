// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/stable_panel_layout.hpp"

#include <algorithm>
#include <utility>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QLayout>
#include <QMainWindow>
#include <QObject>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionTabWidgetFrame>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeView>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

namespace qar_rviz_streaming
{
namespace
{

/// The room the strip leaves around its two lines.
constexpr int kStripMarginX = 6;
constexpr int kStripMarginY = 3;

/// The narrowest and the lowest the panel may be made, by the design of the pages.
constexpr int kMinimumWidth = 200;
constexpr int kMinimumPageHeight = 120;

/// Hands a wheel event that reaches an unfocused control on to the page under it.
class WheelGuard : public QObject
{
public:
  explicit WheelGuard(QObject * parent)
  : QObject(parent) {}

protected:
  bool eventFilter(QObject * watched, QEvent * event) override
  {
    if (event->type() == QEvent::Wheel) {
      const auto * widget = qobject_cast<const QWidget *>(watched);
      if (widget != nullptr && !widget->hasFocus()) {
        // Handled, but not accepted: the event goes on to the parent, which scrolls.
        event->ignore();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }
};

/// One line of text in the bold font: the taller of the two the strip's lines use.
int bold_line_height(const QWidget & widget)
{
  QFont bold = widget.font();
  bold.setBold(true);
  return QFontMetrics(bold).lineSpacing();
}

std::string size_text(const QSize & size)
{
  return std::to_string(size.width()) + "x" + std::to_string(size.height());
}

/// What a tab widget adds around a page: the frame of its pane on every side and the tab bar
/// above it, as the style lays them out (the way QTabWidget::setUpLayout() asks for it).
QSize tab_widget_chrome(const QTabWidget & tabs)
{
  const QStyle * style = tabs.style();
  const int frame = style->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, &tabs);
  const QTabBar * bar = tabs.findChild<QTabBar *>();
  const QSize bar_size = bar != nullptr ? bar->sizeHint() : QSize(0, 0);

  QStyleOptionTabWidgetFrame option;
  option.initFrom(&tabs);
  option.shape = bar != nullptr ? bar->shape() : QTabBar::RoundedNorth;
  option.lineWidth = frame;
  option.tabBarSize = bar_size;
  constexpr int kProbe = 4000;
  option.rect = QRect(0, 0, kProbe, kProbe);
  option.tabBarRect = QRect(QPoint(0, 0), QSize(kProbe, bar_size.height()));
  const QRect contents = style->subElementRect(QStyle::SE_TabWidgetTabContents, &option, &tabs);
  if (!contents.isValid() || contents.width() >= kProbe || contents.height() >= kProbe) {
    // A style that gives no answer: the frame on every side and the bar.
    return QSize(2 * frame, 2 * frame + bar_size.height());
  }
  return QSize(kProbe - contents.width(), kProbe - contents.height());
}

/// The size a QScrollArea would give @p content in a viewport of @p viewport: never smaller than
/// the room the content needs. Returns the room the content needs in @p needed.
QSize scroll_area_content_size(QWidget & content, const QSize & viewport, QSize & needed)
{
  QSize minimum = content.minimumSizeHint().expandedTo(content.minimumSize());
  minimum = minimum.expandedTo(QSize(0, 0));
  const int width = std::max(viewport.width(), minimum.width());
  int height = minimum.height();
  if (content.hasHeightForWidth()) {
    height = std::max(height, content.heightForWidth(width));
  }
  needed = QSize(minimum.width(), height);
  return QSize(width, std::max(viewport.height(), height));
}

/// How often set_dock_height() grows the main window for one request: the first measure of what
/// is missing can leave out part of the window's own chrome, and a second one finds the rest.
constexpr int kGrowRounds = 3;

/// A dock that a hold has limited keeps what it had before in these properties of its own, with
/// the number of holds on it: the limits come back when the last hold is released.
constexpr const char * kHoldCountProperty = "qar_hold_count";
constexpr const char * kHoldMinimumProperty = "qar_hold_minimum";
constexpr const char * kHoldMaximumProperty = "qar_hold_maximum";

/// Makes @p dock a dock of @p hold: before the first change a hold makes to it, its limits are
/// remembered. A dock is taken once by one hold.
void take_dock(DockHold & hold, QDockWidget & dock)
{
  for (const QPointer<QDockWidget> & taken : hold.docks) {
    if (taken == &dock) {
      return;
    }
  }
  const int count = dock.property(kHoldCountProperty).toInt();
  if (count <= 0) {
    dock.setProperty(kHoldMinimumProperty, dock.minimumHeight());
    dock.setProperty(kHoldMaximumProperty, dock.maximumHeight());
  }
  dock.setProperty(kHoldCountProperty, count + 1);
  hold.docks.push_back(&dock);
}

/// Takes one hold off @p dock; the last one puts the limits back.
void give_back_dock(QDockWidget & dock)
{
  const int count = dock.property(kHoldCountProperty).toInt();
  if (count <= 0) {
    return;
  }
  if (count > 1) {
    dock.setProperty(kHoldCountProperty, count - 1);
    return;
  }
  const int minimum = dock.property(kHoldMinimumProperty).toInt();
  const int maximum = dock.property(kHoldMaximumProperty).toInt();
  dock.setMinimumHeight(minimum);
  dock.setMaximumHeight(maximum);
  // An invalid value removes a dynamic property.
  dock.setProperty(kHoldCountProperty, QVariant());
  dock.setProperty(kHoldMinimumProperty, QVariant());
  dock.setProperty(kHoldMaximumProperty, QVariant());
}

/// Fixes @p dock at the height it has until the hold is released.
void pin_dock_height(DockHold & hold, QDockWidget & dock)
{
  take_dock(hold, dock);
  const int height = std::max(dock.height(), dock.minimumHeight());
  dock.setMinimumHeight(height);
  dock.setMaximumHeight(height);
}

/// The room @p window may take: @p room_option, or the available geometry of its screen. Null
/// when nothing says.
QRect window_room(const QMainWindow & window, const QRect & room_option)
{
  if (!room_option.isNull()) {
    return room_option;
  }
  const QScreen * screen = window.screen();
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  return screen != nullptr ? screen->availableGeometry() : QRect();
}

/// True when one of the @p keep docks is docked in @p window, on show, and in another dock area
/// than @p dock. A window that grows gives the new height to its dock areas as well; a kept dock
/// alone in its area would be stretched once its hold ends, and its AR panel rebuilt.
bool kept_dock_in_another_area(
  const QMainWindow & window, const QDockWidget & dock, const std::vector<QDockWidget *> & keep)
{
  const Qt::DockWidgetArea own = window.dockWidgetArea(const_cast<QDockWidget *>(&dock));
  for (const QDockWidget * other : keep) {
    if (other != nullptr && other != &dock && other->parentWidget() == &window &&
      !other->isFloating() && other->isVisible() &&
      window.dockWidgetArea(const_cast<QDockWidget *>(other)) != own)
    {
      return true;
    }
  }
  return false;
}

/// How many of @p missing pixels @p window may grow by. A maximized or full screen window does
/// not grow, nor does one with a kept dock in another dock area than @p dock's, and one that
/// sits in a room grows only as far as the room goes (it can move up). When that is less than
/// @p missing, @p limit says why.
int window_growth_allowed(
  const QMainWindow & window, const QDockWidget & dock, int missing,
  const DockHeightOptions & options, WindowGrowth & limit)
{
  if (window.isFullScreen()) {
    limit = WindowGrowth::kFullScreen;
    return 0;
  }
  if (window.isMaximized()) {
    limit = WindowGrowth::kMaximized;
    return 0;
  }
  if (kept_dock_in_another_area(window, dock, options.keep)) {
    limit = WindowGrowth::kStreamedElsewhere;
    return 0;
  }
  const QRect room_option = options.room;
  const QRect room = window_room(window, room_option);
  if (room.isNull()) {
    return missing;
  }
  // The most the window can grow when it sits at the top of the room.
  const int possible = std::max(0, room.height() - window.frameGeometry().height());
  if (possible < missing) {
    limit = WindowGrowth::kScreenBottom;
  }
  return std::min(missing, possible);
}

/// Makes @p window higher by @p grow pixels. When its lower edge would leave the room, the window
/// moves up by the overshoot, and never above the room.
void grow_window(QMainWindow & window, int grow, const QRect & room_option)
{
  const QRect frame = window.frameGeometry();
  QRect geometry = window.geometry();
  const QRect room = window_room(window, room_option);
  if (!room.isNull()) {
    const int overshoot = frame.bottom() + grow - room.bottom();
    if (overshoot > 0) {
      const int top = std::max(room.top(), frame.top() - overshoot);
      geometry.translate(0, top - frame.top());
    }
  }
  geometry.setHeight(geometry.height() + grow);
  window.setGeometry(geometry);
}

/// The height all visible rows of @p tree take, in pixels; 0 when it has none.
int rows_height(const QTreeView & tree)
{
  const QAbstractItemModel * model = tree.model();
  if (model == nullptr || model->rowCount(tree.rootIndex()) == 0) {
    return 0;
  }
  int total = 0;
  for (QModelIndex index = model->index(0, 0, tree.rootIndex()); index.isValid();
    index = tree.indexBelow(index))
  {
    total += tree.visualRect(index).height();
  }
  return total;
}

}  // namespace

QScrollArea * make_scroll_page(QWidget * content, QWidget * parent)
{
  auto * page = new QScrollArea(parent);
  page->setFrameShape(QFrame::NoFrame);
  page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  page->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  // Ignored in both directions: the tab widget's minimum size, and so the panel's, must not be
  // able to follow what the page holds.
  page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  page->setWidgetResizable(true);
  if (content != nullptr) {
    page->setWidget(content);
  }
  return page;
}

QTabWidget * make_tab_widget(QWidget * parent)
{
  auto * tabs = new QTabWidget(parent);
  tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  tabs->setUsesScrollButtons(true);
  return tabs;
}

void retain_size_when_hidden(QWidget * widget)
{
  if (widget == nullptr) {
    return;
  }
  QSizePolicy policy = widget->sizePolicy();
  policy.setRetainSizeWhenHidden(true);
  widget->setSizePolicy(policy);
}

void guard_wheel(QWidget * widget)
{
  if (widget == nullptr) {
    return;
  }
  widget->setFocusPolicy(Qt::StrongFocus);
  // A child of the widget: the filter goes with it.
  widget->installEventFilter(new WheelGuard(widget));
}

ElidedLabel::ElidedLabel(QWidget * parent)
: QLabel(parent)
{
  // Plain text: an error message may hold anything, including text that looks like a tag.
  setTextFormat(Qt::PlainText);
  setWordWrap(false);
  setMinimumWidth(0);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  setFixedHeight(bold_line_height(*this));
}

void ElidedLabel::setFullText(const QString & text)
{
  if (text != full_text_) {
    full_text_ = text;
    setToolTip(text);
  }
  refreshElided();
}

void ElidedLabel::resizeEvent(QResizeEvent * event)
{
  QLabel::resizeEvent(event);
  refreshElided();
}

void ElidedLabel::refreshElided()
{
  // A style sheet can make the font bold; measure with the font the label will paint with.
  ensurePolished();
  QString line = full_text_;
  line.replace(QLatin1Char('\n'), QLatin1Char(' '));
  line.replace(QLatin1Char('\r'), QLatin1Char(' '));
  line.replace(QLatin1Char('\t'), QLatin1Char(' '));
  const int available = std::max(0, contentsRect().width());
  const QString shown = fontMetrics().elidedText(line, Qt::ElideRight, available);
  if (shown != text()) {
    QLabel::setText(shown);
  }
}

StatusStrip make_status_strip(QWidget * parent)
{
  StatusStrip strip;
  strip.frame = new QFrame(parent);
  strip.frame->setFrameShape(QFrame::StyledPanel);
  strip.frame->setFrameShadow(QFrame::Sunken);
  strip.frame->setLineWidth(1);
  strip.first = new ElidedLabel(strip.frame);
  strip.second = new ElidedLabel(strip.frame);

  auto * layout = new QVBoxLayout(strip.frame);
  layout->setContentsMargins(kStripMarginX, kStripMarginY, kStripMarginX, kStripMarginY);
  layout->setSpacing(0);
  layout->addWidget(strip.first);
  layout->addWidget(strip.second);

  strip.frame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  strip.frame->setFixedHeight(
    2 * bold_line_height(*strip.frame) + 2 * kStripMarginY + 2 * strip.frame->frameWidth());
  return strip;
}

int tab_bar_height(const QTabWidget & tabs)
{
  // QTabWidget::tabBar() is protected; the bar is the only QTabBar the widget owns.
  const QTabBar * bar = tabs.findChild<QTabBar *>();
  return bar != nullptr ? bar->sizeHint().height() : 0;
}

int tab_bar_width(const QTabWidget & tabs)
{
  const QTabBar * bar = tabs.findChild<QTabBar *>();
  return bar != nullptr ? bar->sizeHint().width() : 0;
}

QSize default_dock_size(const QSize & hint, const QSize & available, const QSize & minimum)
{
  int width = std::max(kDefaultDockWidth, hint.width());
  int height = std::max(kDefaultDockHeight, hint.height() + kDockHeightSlack);
  if (available.width() > 0) {
    width = std::min(width, available.width() - kDockClearance);
  }
  if (available.height() > 0) {
    height = std::min(height, available.height() - kDockClearance);
  }
  return QSize(std::max({width, minimum.width(), 0}), std::max({height, minimum.height(), 0}));
}

DockHeightResult set_dock_height(
  QDockWidget & dock, int height, DockHold & hold, const DockHeightOptions & options)
{
  DockHeightResult result;
  result.before = dock.height();

  auto * window = dock.isFloating() ? nullptr : qobject_cast<QMainWindow *>(dock.parentWidget());
  if (window == nullptr || window->layout() == nullptr) {
    dock.resize(dock.width(), height);
    result.got = dock.height();
    return result;
  }
  QLayout * layout = window->layout();
  result.window_before = window->height();
  result.window_after = result.window_before;

  // The docks that keep their height go first, so that the layout takes what it needs from the
  // others.
  for (QDockWidget * other : options.keep) {
    if (other != nullptr && other != &dock && other->parentWidget() == window &&
      !other->isFloating() && other->isVisible())
    {
      pin_dock_height(hold, *other);
    }
  }

  window->resizeDocks({&dock}, {height}, Qt::Vertical);
  // resizeDocks() only asks for the new sizes; applying the layout is what gives them.
  layout->invalidate();
  layout->activate();
  result.got = dock.height();
  const int minimum_before = dock.minimumHeight();
  if (result.got >= height || minimum_before >= height || dock.maximumHeight() < height) {
    return result;
  }

  // The dock shares its column, and the layout took the height it was short of from all the docks
  // of the column in proportion. A minimum height of the dock makes the others give way instead.
  // The minimum is never left at more than the window can hold when the layout is applied: Qt
  // makes a window higher by itself for a minimum it cannot hold, past the room of its screen, and
  // a maximized one as well.
  take_dock(hold, dock);
  dock.setMinimumHeight(height);
  layout->invalidate();
  int grown = 0;
  bool short_of_room = false;
  WindowGrowth limit = WindowGrowth::kNotNeeded;
  for (int round = 0; round < kGrowRounds; ++round) {
    int missing = layout->totalMinimumSize().height() - window->height();
    if (missing <= 0) {
      layout->activate();
      // The layout can hold the minimum. If the dock is still short, the sum left out some of the
      // window's own height: grow by what the dock is short of.
      missing = height - dock.height();
      if (missing <= 0) {
        break;
      }
    }
    const int allowed = window_growth_allowed(*window, dock, missing, options, limit);
    if (allowed < missing) {
      dock.setMinimumHeight(std::max(minimum_before, dock.minimumHeight() - (missing - allowed)));
      layout->invalidate();
      short_of_room = true;
    }
    if (allowed <= 0) {
      break;
    }
    grow_window(*window, allowed, options.room);
    grown += allowed;
    layout->invalidate();
    if (short_of_room) {
      break;
    }
  }

  layout->invalidate();
  layout->activate();
  result.got = dock.height();
  if (dock.minimumHeight() > minimum_before && result.got > result.before) {
    result.held = true;
  } else {
    dock.setMinimumHeight(minimum_before);
    layout->invalidate();
    layout->activate();
    result.got = dock.height();
  }
  result.window_after = window->height();
  if (short_of_room) {
    result.window = limit;
  } else if (grown > 0) {
    result.window = WindowGrowth::kGrown;
  }
  return result;
}

void release_dock_hold(DockHold & hold)
{
  std::vector<QPointer<QDockWidget>> docks;
  docks.swap(hold.docks);
  for (const QPointer<QDockWidget> & dock : docks) {
    if (dock.isNull()) {
      continue;
    }
    // A dock whose window has lost its layout is being destroyed with it: nothing to put back.
    const QWidget * parent = dock->parentWidget();
    if (parent != nullptr && parent->layout() == nullptr) {
      continue;
    }
    give_back_dock(*dock);
  }
}

int vertical_overflow(const QAbstractScrollArea & area)
{
  // A bar is shown or hidden on a queued call after its range has changed, so the range says
  // whether the area needs one: right away, whatever the bar shows at this moment. An area that
  // is not on show (another tab) needs none, and neither does one that never has a bar.
  const QScrollBar * bar = area.verticalScrollBar();
  if (bar == nullptr || !area.isVisible() ||
    area.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff)
  {
    return 0;
  }
  const int range = bar->maximum() - bar->minimum();
  if (range <= 0) {
    return 0;
  }
  const auto * view = qobject_cast<const QAbstractItemView *>(&area);
  if (view == nullptr || view->verticalScrollMode() != QAbstractItemView::ScrollPerItem) {
    return range;
  }
  // The bar counts rows: the rows that do not fit, times what a row takes.
  int row = view->sizeHintForRow(0);
  if (row <= 0) {
    row = view->fontMetrics().height() + 4;
  }
  return range * row;
}

int missing_content_height(const QWidget & content)
{
  int missing = 0;
  if (const auto * self = qobject_cast<const QAbstractScrollArea *>(&content)) {
    missing = vertical_overflow(*self);
  }
  for (const QAbstractScrollArea * area : content.findChildren<QAbstractScrollArea *>()) {
    missing = std::max(missing, vertical_overflow(*area));
  }
  return missing;
}

HeightFit fit_height_to_view(
  QWidget & panel, QAbstractItemView & view, int width, int min_height, int max_height)
{
  const int low = std::max(1, min_height);
  const int high = std::max(low, max_height);
  HeightFit fit;
  auto resize_to = [&](int height) {
      fit.height = std::clamp(height, low, high);
      panel.setFixedSize(width, fit.height);
      if (panel.layout() != nullptr) {
        panel.layout()->activate();
      }
      view.doItemsLayout();
    };

  resize_to(panel.height());
  while (fit.rounds < kMaxFitRounds && fit.height < high) {
    const int missing = vertical_overflow(view);
    if (missing <= 0) {
      break;
    }
    resize_to(fit.height + missing + kFitSlack);
    ++fit.rounds;
  }

  // The rows take less room than the view has: the panel is made lower, and higher again by the
  // same measure if a bar shows after all.
  if (const auto * tree = qobject_cast<const QTreeView *>(&view)) {
    if (vertical_overflow(view) <= 0 && fit.height > low) {
      const int rows = rows_height(*tree);
      const int spare = view.viewport()->height() - rows - kFitSlack;
      if (rows > 0 && spare > 0) {
        resize_to(fit.height - spare);
        ++fit.rounds;
        const int missing = vertical_overflow(view);
        if (missing > 0 && fit.height < high) {
          resize_to(fit.height + missing + kFitSlack);
          ++fit.rounds;
        }
      }
    }
  }
  fit.scroll_left = vertical_overflow(view) > 0;
  return fit;
}

StableHints compute_stable_hints(
  const QTabWidget & tabs, const std::vector<QWidget *> & page_contents,
  const QWidget & status_strip, int max_height)
{
  int widest = 0;
  for (const QWidget * content : page_contents) {
    if (content != nullptr) {
      widest = std::max(widest, content->sizeHint().width());
    }
  }
  const QSize chrome = tab_widget_chrome(tabs);
  const int strip_height = std::max(status_strip.sizeHint().height(), status_strip.minimumHeight());
  const int bar_height = tab_bar_height(tabs);
  // A page that is taller than the panel shows a vertical bar, which takes its width from the
  // viewport: leave room for it, so that the widest page is not cut at the start size.
  const int scroll_bar = tabs.style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, &tabs);

  // The tab bar needs its whole width for six titles to show without scroll buttons, and that
  // may be wider than any page.
  const int bar_width = tab_bar_width(tabs) + chrome.width();
  const int width = std::max({kMinimumWidth, widest + chrome.width() + scroll_bar, bar_width});
  int tallest = 0;
  for (QWidget * content : page_contents) {
    if (content == nullptr) {
      continue;
    }
    int height = content->minimumSizeHint().height();
    if (content->hasHeightForWidth()) {
      height = std::max(
        height, content->heightForWidth(std::max(0, width - chrome.width() - scroll_bar)));
    }
    tallest = std::max(tallest, height);
  }

  StableHints hints;
  hints.hint = QSize(width, std::min(max_height, strip_height + chrome.height() + tallest));
  hints.minimum = QSize(kMinimumWidth, strip_height + bar_height + kMinimumPageHeight);
  return hints;
}

PageShortfall page_shortfall(
  const QTabWidget & tabs, const std::vector<QWidget *> & page_contents)
{
  QSize viewport(0, 0);
  if (const auto * current = qobject_cast<const QScrollArea *>(tabs.currentWidget())) {
    viewport = current->viewport()->size();
  }

  PageShortfall worst;
  const int count = std::min(tabs.count(), static_cast<int>(page_contents.size()));
  for (int i = 0; i < count; ++i) {
    QWidget * content = page_contents[static_cast<std::size_t>(i)];
    if (content == nullptr) {
      continue;
    }
    QSize needed;
    scroll_area_content_size(*content, viewport, needed);
    const int missing = needed.height() - viewport.height();
    if (missing > worst.px) {
      worst.px = missing;
      worst.page = "page '" + tabs.tabText(i).toStdString() + "'";
    }
  }
  return worst;
}

std::vector<std::string> describe_page_fit(
  const QTabWidget & tabs, const std::vector<QWidget *> & page_contents)
{
  QSize viewport(0, 0);
  if (const auto * current = qobject_cast<const QScrollArea *>(tabs.currentWidget())) {
    viewport = current->viewport()->size();
  }

  std::vector<std::string> lines;
  const int count = std::min(tabs.count(), static_cast<int>(page_contents.size()));
  for (int i = 0; i < count; ++i) {
    QWidget * content = page_contents[static_cast<std::size_t>(i)];
    if (content == nullptr) {
      continue;
    }
    QSize needed;
    const QSize shown = scroll_area_content_size(*content, viewport, needed);
    std::string line = "page '" + tabs.tabText(i).toStdString() + "': content " +
      size_text(shown) + " (min " + size_text(needed) + "), viewport " + size_text(viewport) + ", ";
    if (needed.height() > viewport.height()) {
      line += "SCROLLS by " + std::to_string(needed.height() - viewport.height()) + " px";
    } else {
      line += "fits";
    }
    if (needed.width() > viewport.width()) {
      line += ", " + std::to_string(needed.width() - viewport.width()) +
        " px wider than the viewport";
    }
    lines.push_back(line);
  }
  return lines;
}

namespace
{

/// The height of one row of the item views under @p content, for the spare rows of a fit: the
/// tallest row any area of @p census reports, else the first row of the first item view.
int spare_row_height(const QWidget & content, const qar_qt_streaming::ScrollCensus & census)
{
  int row = 0;
  for (const qar_qt_streaming::ScrollAreaCensus & area : census.areas) {
    if (!area.by_design) {
      row = std::max(row, area.row_h);
    }
  }
  if (row <= 0) {
    if (const auto * view = content.findChild<QAbstractItemView *>()) {
      row = std::max(0, view->sizeHintForRow(0));
    }
  }
  return row;
}

}  // namespace

SettledHeightFit::SettledHeightFit(const FitPolicy & policy)
: policy_(policy)
{
}

SettledHeightFit::Step SettledHeightFit::finish(Step step, Action action, const char * reason)
{
  step.action = action;
  if (action == Action::Done) {
    step.honest_no_bar = true;
  } else {
    step.reason = reason != nullptr ? reason : "";
  }
  return step;
}

int SettledHeightFit::trim_target(const QWidget & content, int current_height) const
{
  const auto * tree = content.findChild<QTreeView *>();
  if (tree == nullptr || !tree->isVisibleTo(&content)) {
    return current_height;
  }
  const int rows = rows_height(*tree);
  const int row = std::max(0, tree->sizeHintForRow(0));
  const int spare = tree->viewport()->height() - rows - policy_.slack_px - policy_.row_slack * row;
  if (rows <= 0 || spare <= 0) {
    return current_height;
  }
  return std::max(policy_.min_h, current_height - spare);
}

SettledHeightFit::Step SettledHeightFit::advance(
  const QWidget & content, int current_height, const ExtraNeed & extra)
{
  Step step;
  step.height = current_height;
  step.target = current_height;
  step.census = qar_qt_streaming::take_scroll_census(content);
  step.rounds = ++looks_;

  const qar_qt_streaming::ScrollCensus & census = step.census;
  // The worst bar of an area that does not scroll by design, and the need that the census cannot
  // see: the larger of the two is what the log names. A horizontal bar counts here although a
  // taller widget does not remove it (the census leaves it out of its sums): it is a bar the
  // operator would see.
  for (const qar_qt_streaming::ScrollAreaCensus & area : census.areas) {
    const int shown = area.overflow_px + area.h_bar_extent;  // h_bar_extent is 0 without a bar
    if (!area.by_design && shown > step.largest_px) {
      step.largest_px = shown;
      step.largest_what = area.class_name + " '" + area.object_name + "'";
    }
  }
  if (extra.px > step.largest_px) {
    step.largest_px = extra.px;
    step.largest_what = extra.what.empty() ? std::string("content") : extra.what;
  }

  // What a taller widget removes: the vertical overflow of the areas that do not scroll by design
  // (summed, for areas one above the other), and the extent of a horizontal bar where there is a
  // vertical overflow too, because the bar takes room from the viewport. A horizontal bar on an
  // area without vertical overflow stays whatever the height is: it is no reason to grow.
  int vertical = 0;
  int horizontal = 0;
  for (const qar_qt_streaming::ScrollAreaCensus & area : census.areas) {
    if (area.by_design) {
      continue;
    }
    if (area.overflow_px > 0) {
      vertical += area.overflow_px + (area.h_bar ? area.h_bar_extent : 0);
    } else if (area.h_bar) {
      horizontal = std::max(horizontal, area.h_bar_extent);
    }
  }
  vertical = std::max(vertical, std::max(0, extra.px));
  const int row = policy_.row_slack > 0 ? spare_row_height(content, census) : 0;
  step.missing_px = vertical > 0 ? vertical + policy_.slack_px + policy_.row_slack * row : 0;
  step.height_ok = vertical == 0 && census.unmeasured == 0;
  step.no_bar_seen = step.height_ok && horizontal == 0;
  step.resizes = resizes_;
  // Looks without an end are a fit that never settles (a size that keeps changing).
  const int max_looks = 3 * policy_.max_rounds + 4;

  if (vertical > 0) {
    quiet_seen_ = false;
    // Where the widget cannot be made higher, the same look twice in a row says so: the first one
    // may have caught the layout on its way.
    const char * stuck = nullptr;
    int target = current_height;
    if (resizes_ >= policy_.max_rounds) {
      stuck = kFitReasonRounds;
    } else if (last_resize_grew_ && current_height <= resize_from_) {
      stuck = kFitReasonNoGrowth;
    } else {
      target = std::clamp(current_height + step.missing_px, policy_.min_h, policy_.max_h);
      if (target <= current_height) {
        stuck = kFitReasonHeight;
      }
    }
    if (stuck != nullptr) {
      const bool again = stuck_seen_ && stuck_height_ == current_height &&
        stuck_missing_ == step.missing_px;
      if (again || looks_ >= max_looks) {
        return finish(std::move(step), Action::GaveUp, stuck);
      }
      stuck_seen_ = true;
      stuck_height_ = current_height;
      stuck_missing_ = step.missing_px;
      step.action = Action::Wait;
      return step;
    }
    stuck_seen_ = false;
    ++resizes_;
    resize_from_ = current_height;
    last_resize_grew_ = true;
    trim_done_ = true;
    step.resizes = resizes_;
    step.target = target;
    step.action = Action::Resize;
    return step;
  }
  stuck_seen_ = false;

  if (census.unmeasured > 0) {
    // A bar whose size cannot be told yet: no "no bar" is said on that.
    quiet_seen_ = false;
    if (looks_ >= max_looks) {
      return finish(std::move(step), Action::GaveUp, kFitReasonUnmeasured);
    }
    step.action = Action::Wait;
    return step;
  }

  // Nothing a taller widget would remove, everything measured. The first quiet look may make a
  // tree lower.
  if (policy_.trim_to_rows && !trim_done_ && resizes_ == 0) {
    trim_done_ = true;
    const int target = trim_target(content, current_height);
    if (target < current_height && resizes_ < policy_.max_rounds) {
      ++resizes_;
      resize_from_ = current_height;
      last_resize_grew_ = false;
      quiet_seen_ = false;
      step.resizes = resizes_;
      step.target = target;
      step.action = Action::Resize;
      return step;
    }
  }
  if (quiet_seen_ && quiet_height_ == current_height) {
    if (horizontal > 0) {
      // Two looks agree, and a horizontal bar is there: no height removes it.
      return finish(std::move(step), Action::GaveUp, kFitReasonHorizontal);
    }
    return finish(std::move(step), Action::Done, nullptr);
  }
  quiet_seen_ = true;
  quiet_height_ = current_height;
  if (looks_ >= max_looks) {
    return finish(
      std::move(step), Action::GaveUp, horizontal > 0 ? kFitReasonHorizontal : kFitReasonUnsettled);
  }
  step.action = Action::Wait;
  return step;
}

std::string describe_fit_round(const SettledHeightFit::Step & step)
{
  const int areas = static_cast<int>(step.census.areas.size());
  const int unmeasured = step.census.unmeasured;
  std::string line = "fit round " + std::to_string(step.rounds) + ": " +
    std::to_string(step.height) + " px, missing " + std::to_string(step.missing_px) + " px (" +
    std::to_string(std::max(0, areas - unmeasured)) + " area(s) measured, " +
    std::to_string(unmeasured) + " unmeasured; largest ";
  if (step.largest_px > 0 && !step.largest_what.empty()) {
    line += step.largest_what + " " + std::to_string(step.largest_px) + " px";
  } else {
    line += "none";
  }
  return line + ")";
}

std::string describe_fit_result(
  const SettledHeightFit::Step & step, int height, const std::string & reason)
{
  std::string line = "fitted to " + std::to_string(height) + " px in " +
    std::to_string(step.rounds) + " round(s)";
  if (step.action == SettledHeightFit::Action::Done && step.honest_no_bar) {
    return line + ", confirmed: no scroll bar";
  }
  const std::string why = reason.empty() ? step.reason : reason;
  if (step.largest_px > 0) {
    return line + ": a scroll bar of " + std::to_string(step.largest_px) + " px is left (" + why +
           ")";
  }
  return line + ": not confirmed (" + why + ")";
}

std::string dock_growth_reason(WindowGrowth window, bool hold_released)
{
  switch (window) {
    case WindowGrowth::kMaximized:
      return "the window is maximized";
    case WindowGrowth::kFullScreen:
      return "the window is full screen";
    case WindowGrowth::kStreamedElsewhere:
      return "a streamed dock in another column";
    case WindowGrowth::kScreenBottom:
      return "its window gave it no more room";
    case WindowGrowth::kNotNeeded:
    case WindowGrowth::kGrown:
      break;
  }
  if (hold_released) {
    return "the layout took the height back after the hold was released";
  }
  return std::string();
}

std::string start_fit_skip_reason(const StartFitFacts & facts)
{
  if (!facts.in_dock) {
    return "the panel is not in a dock";
  }
  if (!facts.dock_visible) {
    return "its dock is not visible";
  }
  if (facts.ar_panel_streams) {
    return "an AR panel is already streaming and a resize would rebuild it";
  }
  if (facts.stream_fit_runs) {
    return "the fit for its stream is already looking at the dock";
  }
  return std::string();
}

std::string describe_start_fit(
  int height_before, int height_after, bool confirmed, const std::string & result)
{
  std::string line = "dock ";
  if (height_after != height_before) {
    line += std::to_string(height_before) + " -> " + std::to_string(height_after) + " px";
  } else {
    line += std::to_string(height_after) + " px";
    if (confirmed) {
      line += " already high enough";
    }
  }
  return result.empty() ? line : line + " (" + result + ")";
}

ReadoutBox make_readout_box(QWidget * parent, const QString & text, const QString & tool_tip)
{
  ReadoutBox box;
  box.label = new QLabel(text);
  // Plain text: a counter or an error may hold anything, including text that looks like a tag.
  box.label->setTextFormat(Qt::PlainText);
  box.label->setWordWrap(true);
  box.label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  box.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  box.label->setMinimumWidth(0);
  QSizePolicy label_policy = box.label->sizePolicy();
  label_policy.setHorizontalPolicy(QSizePolicy::Ignored);
  box.label->setSizePolicy(label_policy);

  box.area = new QScrollArea(parent);
  box.area->setWidget(box.label);
  box.area->setWidgetResizable(true);
  box.area->setFrameShape(QFrame::StyledPanel);
  // Nothing in here scrolls: a text that does not fit is shortened by set_readout_text().
  box.area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  box.area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  box.area->setProperty(qar_qt_streaming::kScrollsByDesignProperty, true);
  box.area->setToolTip(tool_tip);
  // The height comes from the font, and a fixed height pins the minimum and the maximum, so that
  // no text can move the geometry of the box by a pixel.
  box.area->setFixedHeight(box.area->fontMetrics().lineSpacing() * kReadoutLines + 8);
  box.area->setMinimumWidth(0);
  QSizePolicy area_policy = box.area->sizePolicy();
  area_policy.setHorizontalPolicy(QSizePolicy::Ignored);
  box.area->setSizePolicy(area_policy);
  return box;
}

int set_readout_text(
  const ReadoutBox & box, const QStringList & head, const QStringList & items,
  const QString & more_format, int width_px)
{
  if (box.area == nullptr || box.label == nullptr) {
    return 0;
  }
  // Two pixels short of the width: a line that fills it exactly may wrap in the label.
  const int width = std::max(1, (width_px > 0 ? width_px : box.area->viewport()->width()) - 2);
  const int height = box.area->maximumHeight() - 2 * box.area->frameWidth();
  const QFontMetrics metrics(box.label->font());
  const QRect room(0, 0, width, 1 << 20);
  const auto needed = [&](const QStringList & lines) {
      return metrics.boundingRect(room, Qt::AlignLeft | Qt::TextWordWrap, lines.join('\n'))
             .height();
    };

  int shown = static_cast<int>(items.size());
  for (;;) {
    QStringList lines = head + items.mid(0, shown);
    const int dropped = static_cast<int>(items.size()) - shown;
    if (dropped > 0) {
      lines.append(more_format.arg(dropped));
    }
    if (shown == 0 || needed(lines) <= height) {
      box.label->setText(lines.join('\n'));
      return dropped;
    }
    --shown;
  }
}

}  // namespace qar_rviz_streaming
