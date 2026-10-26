// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__STABLE_PANEL_LAYOUT_HPP_
#define QAR_RVIZ_STREAMING__STABLE_PANEL_LAYOUT_HPP_

/// @file stable_panel_layout.hpp
/// @brief The building blocks that keep the size of a tabbed panel independent of its content.
/// Qt Widgets only: no RViz, no ROS, and no QObject subclass with a meta object.
///
/// The panel streams itself to AR, and the streamed buffer is rebuilt whenever the panel's size
/// changes by more than a few pixels. So nothing a page shows, hides or rewrites may reach the
/// geometry of the panel. The pieces here are what the panel is made of:
///
///  * @ref make_scroll_page puts one tab page into a frameless scroll area whose size policy is
///    `Ignored` in both directions, so the tab widget's minimum size no longer depends on any
///    page. A page that is taller than the panel scrolls; it never scrolls sideways.
///  * @ref ElidedLabel is a one-line label that elides its text to its width, whatever the text.
///  * @ref make_status_strip builds the two fixed lines above the tabs from two of them.
///  * @ref compute_stable_hints measures the pages and the tab bar once; the panel returns the
///    result from `sizeHint()` and `minimumSizeHint()` for the rest of its life.
///  * @ref default_dock_size is the size the dock of the panel gets when no saved size is to be
///    kept, and @ref set_dock_height gives a dock a height even when its column is shared or
///    its neighbours cannot give way (the main window grows then, unless a dock that has to keep
///    its height sits in another dock area).
///  * @ref missing_content_height and @ref fit_height_to_view measure what a scroll bar says is
///    missing and size a widget by it.
///  * @ref describe_page_fit says, for every page, whether it fits the room it is given, and
///    @ref page_shortfall says by how much the worst page is short.
///  * @ref SettledHeightFit sizes a widget so that none of its scroll areas shows a bar, looking
///    again after the layout has run and only calling the fit done when two looks agree.
///  * @ref make_readout_box and @ref set_readout_text build the text box of live counters that
///    never shows a scroll bar, whatever the counters say.

#include <string>
#include <vector>

#include <QLabel>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include "qar_qt_streaming/scroll_census.hpp"

class QAbstractItemView;
class QAbstractScrollArea;
class QDockWidget;
class QFrame;
class QResizeEvent;
class QScrollArea;
class QTabWidget;
class QWidget;

namespace qar_rviz_streaming
{

/// @brief One tab page: @p content in a frameless QScrollArea, widgetResizable, the horizontal
/// bar always off, the vertical bar as needed, size policy (Ignored, Ignored).
/// The scroll area takes ownership of @p content.
QScrollArea * make_scroll_page(QWidget * content, QWidget * parent);

/// @brief The tab widget of the panel: size policy (Ignored, Ignored), scroll buttons on, so that
/// six tabs may be wider than a narrow dock. Add it to a vertical layout with a stretch factor
/// of 1 and it takes all the height the status strip leaves. Its policy is `Ignored` in both
/// directions on purpose: QTabWidget::sizeHint() reads the size hints of its pages directly, so
/// with a vertical policy of `Expanding` the layout above it would follow what the pages hold.
QTabWidget * make_tab_widget(QWidget * parent);

/// @brief Keeps the room of @p widget in its layout while it is hidden, so that hiding or showing
/// it does not move the widgets around it.
void retain_size_when_hidden(QWidget * widget);

/// @brief Keeps the mouse wheel from changing @p widget, a combo box or a spin box, while the
/// widget does not have the keyboard focus: the wheel scrolls the page under the pointer instead.
/// A click gives the focus, and then the wheel changes the value as usual. Without it, scrolling
/// a page past a control would change the setting under the pointer.
void guard_wheel(QWidget * widget);

/// @brief One line of text, elided to the width of the label, with the whole text as its tool tip.
///
/// The height is fixed to one line (of the bold font, the taller of the two the panel uses) and
/// the horizontal size policy is `Ignored`, so the text can neither widen nor heighten the
/// label. Use @ref setFullText, never `setText`: the text of a label that elides is the elided
/// text, and `fullText()` is the only place that keeps the whole of it.
class ElidedLabel : public QLabel
{
public:
  explicit ElidedLabel(QWidget * parent = nullptr);

  /// Replaces the text. Line breaks are shown as spaces; the tool tip is the text as given.
  void setFullText(const QString & text);
  const QString & fullText() const { return full_text_; }

protected:
  void resizeEvent(QResizeEvent * event) override;

private:
  void refreshElided();

  QString full_text_;
};

/// @brief The status strip: a frame of a fixed height with two @ref ElidedLabel lines.
struct StatusStrip
{
  QFrame * frame = nullptr;
  ElidedLabel * first = nullptr;
  ElidedLabel * second = nullptr;
};

/// @brief Builds the strip. The frame has a fixed height of two lines plus its margins, and a
/// horizontal size policy of `Ignored`, so it adds nothing to the width of the panel.
StatusStrip make_status_strip(QWidget * parent);

/// @brief The constant size hints of a panel.
struct StableHints
{
  QSize hint;
  QSize minimum;
};

/// @brief Measures the pages and the tab bar once.
///
/// `hint` is as wide as the widest preferred width of the pages (with the room of a vertical
/// scroll bar) or as the tab bar needs to show all its tabs without scroll buttons, whichever is
/// wider, with the frame of the tab widget added to either. It is as high as the strip, the tab
/// bar and the tallest page at its smallest (a page's list or log counts at its minimum, and a
/// wrapped label at the height its text needs at the hint's width), with the frame of the tab
/// widget added, capped at @p max_height. `minimum` is 200 pixels wide and the strip, the tab bar
/// and 120 pixels high. Call it when every page exists and holds its first content.
StableHints compute_stable_hints(
  const QTabWidget & tabs, const std::vector<QWidget *> & page_contents,
  const QWidget & status_strip, int max_height = 700);

/// @brief The height of the tab bar of @p tabs, or 0 when it has none.
int tab_bar_height(const QTabWidget & tabs);

/// @brief The width the tab bar of @p tabs needs to show all its tabs, or 0 when it has none.
int tab_bar_width(const QTabWidget & tabs);

/// The width of the default dock, in pixels, and the least height it gets.
constexpr int kDefaultDockWidth = 475;
constexpr int kDefaultDockHeight = 420;
/// What @ref default_dock_size adds to the height of the panel's hint: a little room, so that
/// the tallest page fits without a scroll bar.
constexpr int kDockHeightSlack = 8;
/// How much room @ref default_dock_size leaves free on each side of the available area.
constexpr int kDockClearance = 40;

/// @brief The size of the dock the panel gets when its saved size is not to be kept: 475 pixels
/// wide, or as wide as @p hint when that is wider, and as high as @p hint plus
/// @ref kDockHeightSlack pixels, or @ref kDefaultDockHeight pixels when that is more.
///
/// Each side is held @ref kDockClearance pixels short of @p available, the room the dock may
/// take (for example the size of the RViz window), and a side of @p available that is not
/// positive is not known and clamps nothing. The result is never smaller than @p minimum (the
/// minimum size hint of the panel), whatever the room, and never negative.
QSize default_dock_size(
  const QSize & hint, const QSize & available, const QSize & minimum = QSize(0, 0));

/// @brief The docks whose height limits a call of @ref set_dock_height has changed for a while.
///
/// Each dock remembers its own limits and how many holds changed them (in a dynamic property),
/// so holds may overlap on one dock and end in any order: the limits come back when the last
/// hold on the dock is released with @ref release_dock_hold.
struct DockHold
{
  std::vector<QPointer<QDockWidget>> docks;  ///< the docks this hold has limited

  /// True while the hold has docks to give back.
  bool active() const { return !docks.empty(); }
};

/// @brief What the main window did when a dock needed more height than its column could give.
enum class WindowGrowth
{
  kNotNeeded,          ///< the other docks gave way; the window was not touched
  kGrown,              ///< the window was made higher by the missing height
  kMaximized,          ///< the window is maximized and was left alone
  kFullScreen,         ///< the window is full screen and was left alone
  kScreenBottom,       ///< the screen has no room for all of it: the window grew less or not at all
  kStreamedElsewhere,  ///< a kept dock sits in another dock area, which would take the height
};

/// @brief How @ref set_dock_height may go about it.
struct DockHeightOptions
{
  /// Docks that keep the height they have while the dock grows (the docks streamed to AR: a size
  /// change of a streamed dock rebuilds its AR panel). Their minimum and maximum height are set to
  /// their height until the hold is released. Floating docks and docks of other windows are left
  /// alone, and so is the dock that grows.
  ///
  /// A hold keeps a dock only while it lasts. A window that is made higher gives the new height
  /// to the dock areas as well, and a kept dock that sits alone in another area (another side
  /// column) would be stretched once the hold ends. So the window is not made higher while a
  /// kept dock that is docked and on show lies in a different dock area than the dock that grows
  /// (@ref WindowGrowth::kStreamedElsewhere).
  std::vector<QDockWidget *> keep;
  /// The room the main window may take, in global coordinates. Null: the available geometry of
  /// the screen of the window.
  QRect room;
};

/// @brief What @ref set_dock_height did.
struct DockHeightResult
{
  int before = 0;         ///< the height of the dock before
  int got = 0;            ///< its height once the layout had been applied
  bool held = false;      ///< a temporary minimum height keeps the dock at the height it got
  int window_before = 0;  ///< the height of the main window before (0: the dock is floating)
  int window_after = 0;   ///< and after
  WindowGrowth window = WindowGrowth::kNotNeeded;
};

/// @brief Gives @p dock the height @p height. A floating dock is resized. A docked one is resized
/// through its main window and the layout is applied at once, so that @c got is what the dock has.
///
/// A dock that shares its column with other docks gets only a part of a change from the layout,
/// however much room the column has: the layout takes the missing height from all docks of the
/// column in proportion. A dock that is still lower than @p height is therefore held there by a
/// temporary minimum height, which makes the other docks give way. When they cannot give way far
/// enough (they are at their minimum), the main window grows by the missing height, unless it is
/// maximized or full screen, and never beyond the room of its screen (it moves up when its lower
/// edge would leave the screen). It does not grow either while a dock of @ref
/// DockHeightOptions::keep lies in another dock area (see there). Without the room for all of
/// it the dock gets what the room allows, and @c window says why.
///
/// Every limit the call sets is recorded in @p hold. The caller gives them back with
/// @ref release_dock_hold once the layout has settled; the heights the layout gave stay.
DockHeightResult set_dock_height(
  QDockWidget & dock, int height, DockHold & hold,
  const DockHeightOptions & options = DockHeightOptions());

/// @brief Puts back the limits that @p hold changed and empties it. A second call changes nothing.
/// A dock whose main window has lost its layout (the window is being destroyed) is left alone.
void release_dock_hold(DockHold & hold);

/// What a fit adds to a measured height, and how many times it resizes a widget at most
/// (@ref fit_height_to_view, @ref SettledHeightFit).
constexpr int kFitSlack = 8;
constexpr int kMaxFitRounds = 6;

/// @brief The height that @p area is short of for its vertical scroll bar to be unnecessary, in
/// pixels, or 0 when it needs none. For per-pixel scrolling it is the range of the bar. An item
/// view that scrolls per item has a bar in rows: the rows still hidden times the height of a row.
/// The range decides, not what the bar shows at this moment (a bar is shown or hidden a little
/// after its range has changed). An area that is not on show, as a page of another tab, needs no
/// bar, and neither does one whose vertical bar is always off.
int vertical_overflow(const QAbstractScrollArea & area);

/// @brief The largest @ref vertical_overflow among @p content (when it is a scroll area itself)
/// and the scroll areas inside it. 0 when none shows a vertical scroll bar, as for a page that
/// is not on show.
int missing_content_height(const QWidget & content);

/// @brief What @ref fit_height_to_view made of a widget.
struct HeightFit
{
  int height = 0;            ///< the height the widget has now
  int rounds = 0;            ///< how often it was resized after the first measurement
  bool scroll_left = false;  ///< the view still shows a vertical scroll bar at that height
};

/// @brief Sizes the top-level widget @p panel, which holds @p view, to @p width pixels and the
/// height at which @p view needs no vertical scroll bar and its rows take no more room than they
/// need. The height stays between @p min_height and @p max_height.
///
/// The widget is resized (fixed size) and measured again up to @ref kMaxFitRounds times, each time
/// by what @ref vertical_overflow says is missing plus @ref kFitSlack. A tree whose rows take less
/// than it has is then made lower, to its rows plus @ref kFitSlack, and made higher again by the
/// same measure if a bar shows after all. The view is told to lay out its items before every
/// measurement, because an item view does that on the next turn of the event loop otherwise.
HeightFit fit_height_to_view(
  QWidget & panel, QAbstractItemView & view, int width, int min_height, int max_height);

/// @brief One sentence per page, in tab order:
/// `page '<tab>': content WxH (min WxH), viewport WxH, fits` or `... SCROLLS by N px`.
///
/// The viewport is the one of the page on show (all pages share one stack). `content` is the size
/// a scroll area gives the page, `min` the room the page needs at that width. A page whose
/// minimum is wider than the viewport, which cannot scroll sideways, ends with
/// `, N px wider than the viewport`.
std::vector<std::string> describe_page_fit(
  const QTabWidget & tabs, const std::vector<QWidget *> & page_contents);

/// @brief How far the worst page of @p tabs is short of room.
struct PageShortfall
{
  int px = 0;        ///< the most any page would scroll by, in pixels; 0 when every page fits
  std::string page;  ///< `page '<tab>'` of the page that is short by the most; empty when none is
};

/// @brief The room every page of @p tabs lacks, measured the way @ref describe_page_fit does:
/// the height a page needs at the width of the viewport against the height of that viewport. The
/// viewport is the one of the page on show, and all pages share it. A scroll-area census sees only
/// the page on show; this one sees them all, with their texts as they are now.
PageShortfall page_shortfall(
  const QTabWidget & tabs, const std::vector<QWidget *> & page_contents);

// ---------------------------------------------------------------------------------------------
// A fit that waits for the layout and confirms what it found
// ---------------------------------------------------------------------------------------------

/// What a @ref SettledHeightFit may do, and how far.
struct FitPolicy
{
  /// The widget is never made lower than @c min_h and never higher than @c max_h.
  int min_h = 0;
  int max_h = 1 << 20;
  /// Added to what the census finds missing every time the widget is made higher.
  int slack_px = kFitSlack;
  /// The most resizes a fit asks for. A trim (see @c trim_to_rows) is one of them.
  int max_rounds = kMaxFitRounds;
  /// Rows of the height of the first row of an item view, added to every height the fit gives:
  /// rows may appear after the fit, and a view with a spare row shows them without a bar.
  int row_slack = 0;
  /// A tree whose rows take less room than it has is made lower, once, as long as the fit has
  /// not made the widget higher.
  bool trim_to_rows = false;
};

/// A need that the scroll-area census cannot see, such as a tab page that is not on show.
struct ExtraNeed
{
  int px = 0;        ///< how far the widget is short of height, in pixels
  std::string what;  ///< what is short, for the log: `page 'AR tools'`
};

/// The reasons a fit gives up with. A caller that knows the real reason (the window that would not
/// grow) puts its own words in their place.
constexpr const char * kFitReasonRounds = "the round limit";
constexpr const char * kFitReasonHeight = "the height limit";
constexpr const char * kFitReasonNoGrowth = "it did not grow";
constexpr const char * kFitReasonUnmeasured = "an area could not be measured";
constexpr const char * kFitReasonUnsettled = "it did not settle";
constexpr const char * kFitReasonHorizontal =
  "a horizontal bar: a taller widget does not remove it";

/// @brief Sizes a widget so that none of its scroll areas shows a bar, and says so only when it
/// has looked twice.
///
/// A scroll bar's range and the rows of an item view follow a change of size a little later: on
/// the next turns of the event loop, after the layout has run. A measure right after a resize, or
/// right after a hold on a size was released, reads numbers that are about to change. So the fit
/// is a loop that the caller runs from a member timer:
///
///  1. call @ref advance, which takes a scroll-area census of the content (scroll_census.hpp) and
///     answers with an action;
///  2. @ref Action::Resize: make the widget the height asked for, and call advance again at the
///     next period of the timer, not before;
///  3. @ref Action::Wait: nothing to change now, call advance again at the next period;
///  4. @ref Action::Done or @ref Action::GaveUp: the fit is over.
///
/// `Done` needs two looks in a row that agree: the same height, no area with a bar, and nothing
/// that could not be measured. A look that finds a bar makes the widget higher by the sum of the
/// overflows of the areas that do not scroll by design, so two trees one above the other (a
/// splitter) are covered, plus the slack. `GaveUp` comes at the bound of rounds or of height, with
/// its reason, when two looks in a row agree that the widget cannot be made higher; the caller says
/// it in its log. A horizontal bar is not removed by a taller widget: it ends the fit the same way,
/// with that reason, once two looks agree.
///
/// A caller that holds a size while it fits (a dock) releases the hold at the first look that finds
/// nothing a taller widget would remove (@ref Step::height_ok) and calls advance again at the next
/// period: the look after the release is the one that confirms, because it differs from the held
/// one when the layout took the height back.
class SettledHeightFit
{
public:
  enum class Action
  {
    Resize,  ///< make the widget @ref Step::target high, then look again after a period
    Wait,    ///< look again after a period
    Done,    ///< two looks in a row agree: no bar, everything measured
    GaveUp,  ///< the fit is over without that; see @ref Step::reason
  };

  /// What one look found, and what to do about it.
  struct Step
  {
    Action action = Action::Wait;
    /// The height the look was made at.
    int height = 0;
    /// For @ref Action::Resize, the height to make the widget; else the same as @ref height.
    int target = 0;
    /// The census of the content at this look.
    qar_qt_streaming::ScrollCensus census;
    /// What the look found missing, slack included; 0 when nothing.
    int missing_px = 0;
    /// The worst bar at this look: the overflow of the area that has the most, with the extent of
    /// its horizontal bar, the need the census cannot see included.
    int largest_px = 0;
    /// Words for the area that is worst at this look: `<Class> '<name>'`, or the words of the
    /// extra need; empty when none overflows.
    std::string largest_what;
    /// Looks made so far (this one included) and resizes asked for so far.
    int rounds = 0;
    int resizes = 0;
    /// The look found nothing that a taller widget would remove, and nothing that could not be
    /// measured: a hold on the height can go. A horizontal bar may be left.
    bool height_ok = false;
    /// The look found no bar at all and nothing that could not be measured.
    bool no_bar_seen = false;
    /// @ref Action::Done only: it is confirmed, two looks agreed.
    bool honest_no_bar = false;
    /// @ref Action::GaveUp only.
    std::string reason;
  };

  explicit SettledHeightFit(const FitPolicy & policy);

  /// @brief Takes one look at @p content, which is @p current_height high now, and says what to do.
  /// @p extra is a need the census cannot see; the larger of it and the census decides.
  /// Call it from a member timer, at least one turn of the event loop after the last resize.
  Step advance(const QWidget & content, int current_height, const ExtraNeed & extra = ExtraNeed());

  const FitPolicy & policy() const { return policy_; }
  int rounds() const { return looks_; }

private:
  /// The height a tree under @p content should be made lower to, or @p current_height.
  int trim_target(const QWidget & content, int current_height) const;
  /// The step that ends the fit: @p action is Done or GaveUp.
  Step finish(Step step, Action action, const char * reason);

  FitPolicy policy_;
  int looks_ = 0;
  int resizes_ = 0;
  /// The height before the latest resize, and whether that resize made the widget higher.
  int resize_from_ = 0;
  bool last_resize_grew_ = false;
  /// A look where the widget could not be made higher, and what it found: the same look again
  /// says it is so.
  bool stuck_seen_ = false;
  int stuck_height_ = 0;
  int stuck_missing_ = 0;
  /// A trim is tried once, at the first quiet look.
  bool trim_done_ = false;
  /// The latest look found no bar, and the height it was made at.
  bool quiet_seen_ = false;
  int quiet_height_ = 0;
};

/// @brief `fit round <r>: <h> px, missing <m> px (<k> area(s) measured, <u> unmeasured; largest
/// <Class> '<name>' <o> px)` for the look of @p step, `largest none` when no area overflows.
std::string describe_fit_round(const SettledHeightFit::Step & step);

/// @brief The last words of a fit, from the final @p step (a Done or a GaveUp). One of:
///  * `fitted to <h> px in <r> round(s), confirmed: no scroll bar`
///  * `fitted to <h> px in <r> round(s): a scroll bar of <o> px is left (<reason>)`
///  * when no bar was found but two looks did not agree: `fitted to <h> px in <r> round(s): not
///    confirmed (<reason>)`
/// @p height is the height the widget has at the end. @p reason, when it is not empty, takes
/// the place of the reason of the step.
std::string describe_fit_result(
  const SettledHeightFit::Step & step, int height, const std::string & reason = std::string());

/// @brief The words for the last line of the fit of a dock that gave up with
/// @ref kFitReasonNoGrowth ("it did not grow"), or an empty text when nothing more is known.
///
/// @p window is what the main window said to the latest growth of the dock. A window that is
/// maximized or full screen, that has a streamed dock in another column, or that has no room left
/// on its screen limits the dock, and the words say which. A window that grew, or did not have to,
/// does not explain a dock that stayed low. @p hold_released says that the hold on the docks was
/// released at a look that found nothing to remove: a dock that is low again at the next look
/// lost its height to the layout, and the words say that.
std::string dock_growth_reason(WindowGrowth window, bool hold_released);

// ---------------------------------------------------------------------------------------------
// The fit of the panel's own dock when the panel starts
// ---------------------------------------------------------------------------------------------

/// What decides whether the dock of the panel is fitted to its pages when the panel starts.
struct StartFitFacts
{
  bool in_dock = false;           ///< the panel sits in a dock
  bool dock_visible = false;      ///< that dock is on show
  bool ar_panel_streams = false;  ///< an AR panel streams: a change of a size would rebuild it
  bool stream_fit_runs = false;   ///< the fit for the stream of the panel itself is running
};

/// @brief Why the dock is not fitted at start, in words for the log, or an empty text when it is.
/// The first reason that applies is given: no dock, a dock that is not on show, an AR panel that
/// streams, a fit for the stream of the panel itself that already looks at the dock (one fit per
/// dock at a time).
std::string start_fit_skip_reason(const StartFitFacts & facts);

/// @brief What the fit at start made of the dock, for the log: `dock <before> -> <after> px
/// (<result>)` when the height changed, `dock <after> px already high enough (<result>)` when it
/// did not and the fit is @p confirmed, and `dock <after> px (<result>)` when it did not and the
/// fit is not. @p result is the last words of the fit (@ref describe_fit_result), @p confirmed
/// that two looks agreed that no bar is left.
std::string describe_start_fit(
  int height_before, int height_after, bool confirmed, const std::string & result);

// ---------------------------------------------------------------------------------------------
// A box of live counters that never shows a scroll bar
// ---------------------------------------------------------------------------------------------

/// The number of lines of its own font the readout box holds.
constexpr int kReadoutLines = 16;

/// @brief The box and the label inside it.
struct ReadoutBox
{
  QScrollArea * area = nullptr;
  QLabel * label = nullptr;
};

/// @brief A text box for counters that change several times a second. It is built so that no text
/// can move its geometry or make it scroll: a fixed height of @ref kReadoutLines lines, both scroll
/// bars always off, a horizontal size policy of `Ignored`, plain text that wraps. It is marked as
/// an area that scrolls by design, so a census never counts it. The text is chosen by
/// @ref set_readout_text so that it fits.
ReadoutBox make_readout_box(QWidget * parent, const QString & text, const QString & tool_tip);

/// @brief Shows @p head, then as many of @p items as fit, one per line, in @p box. When some do not
/// fit, the last items are dropped and one more line stands in for the n that were: @p more_format
/// with `%1` replaced by n. A line may wrap: the height is measured with word wrap at the width of
/// the text, which is @p width_px when that is positive and the width of the viewport of the box
/// otherwise.
/// @return n, the number of items left out.
int set_readout_text(
  const ReadoutBox & box, const QStringList & head, const QStringList & items,
  const QString & more_format, int width_px = 0);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__STABLE_PANEL_LAYOUT_HPP_
