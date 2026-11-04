// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_capi_panels/qar_capi_panel_input_source.hpp"

#include "qar_capi_input_internal.hpp"
#include "qar_qar_adapter/gui_call_guard.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <utility>

namespace qar_capi_panels
{
namespace
{

/// Monotonic seconds. Panel gestures DO carry a timestamp
/// (qar_gui_panel_gesture_get_timestamp), but it is the recogniser's own clock
/// and the header is explicit that it is "comparable only within one source
/// peer id" — not across peers, and not against anything of ours. Downstream
/// compares timestamps for double-click detection and for the liveness sweep,
/// both of which must work across peers, so arrival time on OUR monotonic clock
/// stays the seam's clock. The gesture's own timestamp is traced, not used.
double
monotonic_seconds()
{
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

const char *
panel_gesture_kind_name(QarGuiPanelGestureKind kind)
{
  switch (kind) {
    case QAR_GUI_PANEL_GESTURE_KIND_HOVER: return "hover";
    case QAR_GUI_PANEL_GESTURE_KIND_TAP: return "tap";
    case QAR_GUI_PANEL_GESTURE_KIND_LONG_PRESS: return "long-press";
    case QAR_GUI_PANEL_GESTURE_KIND_DRAG: return "drag";
  }
  return "unknown-kind";
}

/// The kind of a gesture in the seam's vocabulary; the values are the runtime's. A kind outside the
/// enumeration (a newer runtime) becomes Unknown, which the mapper counts and drops.
seam::PanelGestureKind
gesture_kind_from_c(QarGuiPanelGestureKind kind)
{
  switch (kind) {
    case QAR_GUI_PANEL_GESTURE_KIND_HOVER: return seam::PanelGestureKind::Hover;
    case QAR_GUI_PANEL_GESTURE_KIND_TAP: return seam::PanelGestureKind::Tap;
    case QAR_GUI_PANEL_GESTURE_KIND_LONG_PRESS: return seam::PanelGestureKind::LongPress;
    case QAR_GUI_PANEL_GESTURE_KIND_DRAG: return seam::PanelGestureKind::Drag;
  }
  return seam::PanelGestureKind::Unknown;
}

/// The phase of a gesture in the seam's vocabulary; the values are the runtime's. A phase outside
/// the enumeration becomes Unknown.
seam::PanelGesturePhase
gesture_phase_from_c(QarGesturePhase phase)
{
  switch (phase) {
    case QAR_GESTURE_PHASE_STARTED: return seam::PanelGesturePhase::Started;
    case QAR_GESTURE_PHASE_UPDATED: return seam::PanelGesturePhase::Updated;
    case QAR_GESTURE_PHASE_ENDED: return seam::PanelGesturePhase::Ended;
    case QAR_GESTURE_PHASE_INSTANT: return seam::PanelGesturePhase::Instant;
    case QAR_GESTURE_PHASE_CANCELED: return seam::PanelGesturePhase::Canceled;
  }
  return seam::PanelGesturePhase::Unknown;
}

}  // namespace

std::uint32_t
panel_pointer_id_for(const QarPeerId & peer, qar_ar_core::PointerHand hand)
{
  return seam::PanelGestureMapper::pointer_id_for(to_seam(peer), to_seam(hand));
}

// ===========================================================================
//  QarCApiPanelInputSource
// ===========================================================================

QarCApiPanelInputSource::QarCApiPanelInputSource(QarSession * session)
: session_(session)
{
}

QarCApiPanelInputSource::~QarCApiPanelInputSource()
{
  unsubscribe_all();
}

std::string
QarCApiPanelInputSource::last_error() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

void
QarCApiPanelInputSource::set_last_error(std::string text)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  last_error_ = std::move(text);
}

bool
QarCApiPanelInputSource::subscribe_panel(
  const std::string & panel_key, const QarGuiPanelId & id, std::uint32_t content_width,
  std::uint32_t content_height, const std::string & our_stream_id)
{
  if (session_ == nullptr) {
    set_last_error("no QarSession (is QAR streaming started?)");
    return false;
  }
  if (panel_key.empty()) {
    set_last_error("panel input needs a panel key to report samples against");
    return false;
  }
  if (content_width == 0 || content_height == 0) {
    set_last_error("panel input needs a non-zero content size to normalise against");
    return false;
  }

  // Re-subscribing the same key (a widget resized, so the panel was recreated)
  // retires the old pair first: two live subscriptions would double every click.
  unsubscribe_panel(panel_key);

  PanelSubscription subscription;
  subscription.gesture_slot = qar_qar_adapter::detail::mint_slot(this);
  subscription.state_slot = qar_qar_adapter::detail::mint_slot(this);
  if (subscription.gesture_slot == nullptr || subscription.state_slot == nullptr) {
    // A minted slot is never freed, so a half-made set is simply disowned.
    qar_qar_adapter::detail::retire_slot(subscription.gesture_slot);
    qar_qar_adapter::detail::retire_slot(subscription.state_slot);
    set_last_error("panel input slot arena is full (too many subscriptions this process)");
    return false;
  }
  for (qar_qar_adapter::detail::PointerSlot * slot :
    {subscription.gesture_slot, subscription.state_slot})
  {
    slot->panel_key = panel_key;
    slot->content_width = content_width;
    slot->content_height = content_height;
    slot->pointer_id = kPanelPointerId;
  }

  // One token per subscription: cancelling one must not silence the other.
  QarResult r = QAR_CALL(qar_cancel_token_create, &subscription.gesture_token);
  if (qar_result_is_error(r) || subscription.gesture_token == nullptr) {
    set_last_error(qar_result_is_error(r) ?
      input_result_error("qar_cancel_token_create", r) :
      std::string("qar_cancel_token_create gave no token"));
    subscription.gesture_token = nullptr;
    retire(subscription);
    return false;
  }

  // ONE subscription for every kind, not one per kind. The header: "Prefer this
  // when you take more than one kind. Everything arrives on one reader, so a
  // hover and the drag that grows out of the same contact keep the order the
  // producer published them in." Order between a hover Ended and a drag Started
  // from the SAME contact is exactly what the state machine's grab depends on.
  r = QAR_CALL(
      qar_gui_panels_subscribe_all_gesture_events, session_, &id,
      &QarCApiPanelInputSource::on_gesture_event, subscription.gesture_slot,
      subscription.gesture_token);
  if (qar_result_is_error(r)) {
    set_last_error(input_result_error("qar_gui_panels_subscribe_all_gesture_events", r));
    retire(subscription);
    return false;
  }

  // The panel's own updates, which is how we learn the operator closed it in AR.
  // Subscribed HERE rather than in the sender adapter so it shares this class's
  // slot discipline: the callback arrives on a runtime thread, and the only safe
  // way to survive a retirement racing it is the disown-under-the-slot-mutex
  // dance the pointer callbacks already do.
  // Required for the panel's updates: the token owns the subscription.
  r = QAR_CALL(qar_cancel_token_create, &subscription.state_token);
  if (qar_result_is_error(r) || subscription.state_token == nullptr) {
    set_last_error(qar_result_is_error(r) ?
      input_result_error("qar_cancel_token_create", r) :
      std::string("qar_cancel_token_create gave no token"));
    subscription.state_token = nullptr;
    retire(subscription);
    return false;
  }
  // The record log knows the panel before the first update can arrive, so that the first record
  // of the runtime is not lost.
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    record_log_.begin(panel_key, our_stream_id);
  }
  r = QAR_CALL(
      qar_gui_panels_subscribe_panel_updates, session_, &id,
      &QarCApiPanelInputSource::on_panel_update, subscription.state_slot, subscription.state_token);
  if (qar_result_is_error(r)) {
    set_last_error(input_result_error("qar_gui_panels_subscribe_panel_updates", r));
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      record_log_.end(panel_key);
    }
    retire(subscription);
    return false;
  }

  {
    const std::lock_guard<std::mutex> lock(mutex_);
    panels_[panel_key] = subscription;
    // Re-subscribing a key that was closed in AR (the slot was revived onto a
    // new widget) starts its close history afresh.
    closed_panels_.erase(panel_key);
  }
  set_last_error(std::string());
  return true;
}

void
QarCApiPanelInputSource::unsubscribe_panel(const std::string & panel_key)
{
  PanelSubscription subscription;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = panels_.find(panel_key);
    if (it == panels_.end()) {
      return;
    }
    subscription = it->second;
    panels_.erase(it);
    // Records that the runtime still publishes for the retiring subscription are ignored.
    record_log_.end(panel_key);
  }
  // Anything still held down on that panel must be let go before the
  // subscription that would have ended it goes away: a widget left mid-press
  // never gets its release, and with DRAG that is a real possibility.
  cancel_lifecycles_for_panel(panel_key);
  // Retire OUTSIDE mutex_: retire() waits on the slot mutex, which a callback
  // may hold, and that callback wants mutex_ to bump the stats. Holding both in
  // the other order would be the deadlock.
  retire(subscription);
}

void
QarCApiPanelInputSource::unsubscribe_all()
{
  std::map<std::string, PanelSubscription> taken;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    taken.swap(panels_);
    for (const auto & entry : taken) {
      record_log_.end(entry.first);
    }
  }
  cancel_lifecycles_for_panel(std::string());
  for (auto & entry : taken) {
    retire(entry.second);
  }
}

void
QarCApiPanelInputSource::retire(PanelSubscription & subscription)
{
  // Cancel first so the runtime stops delivering, then disown the slots so
  // anything already in flight finds nothing to touch. Only then destroy the
  // token handles.
  if (subscription.gesture_token != nullptr) {
    QAR_CALL(qar_cancel_token_cancel, subscription.gesture_token);
  }
  if (subscription.state_token != nullptr) {
    QAR_CALL(qar_cancel_token_cancel, subscription.state_token);
  }
  qar_qar_adapter::detail::retire_slot(subscription.gesture_slot);
  qar_qar_adapter::detail::retire_slot(subscription.state_slot);
  if (subscription.gesture_token != nullptr) {
    QAR_CALL(qar_cancel_token_handle_destroy, subscription.gesture_token);
    subscription.gesture_token = nullptr;
  }
  if (subscription.state_token != nullptr) {
    QAR_CALL(qar_cancel_token_handle_destroy, subscription.state_token);
    subscription.state_token = nullptr;
  }
  subscription.gesture_slot = nullptr;
  subscription.state_slot = nullptr;
}

void
QarCApiPanelInputSource::on_panel_update(QarGuiPanel * handle, void * user_state)
{
  // Library background thread, same rules as on_gesture_event: short, no
  // blocking, no Qt, no calling back into a subscribe function.
  auto * slot = static_cast<qar_qar_adapter::detail::PointerSlot *>(user_state);
  if (slot == nullptr || handle == nullptr) {
    return;
  }
  const std::lock_guard<std::mutex> lock(slot->mutex);
  auto * self = static_cast<QarCApiPanelInputSource *>(slot->owner);
  if (self == nullptr) {
    return;  // retired; the owner may be long gone
  }
  QarGuiPanelState state = QAR_GUI_PANEL_STATE_VISIBLE;
  const QarResult r = QAR_CALL(qar_gui_panel_get_state, handle, &state);

  // What the runtime says about the panel, whatever its state: read here, because the handle is
  // valid only during this call, and queued for the render thread to print. Three getters and a
  // push under the lock the closed-panel path below takes as well; no I/O, no Qt.
  qar_qar_adapter::PanelRuntimeRecord record;
  record.state_known = !qar_result_is_error(r);
  record.state = state;
  QarStreamId stream = qar_stream_id_default();
  record.stream_known =
    !qar_result_is_error(QAR_CALL(qar_gui_panel_get_content_stream_id, handle, &stream));
  if (record.stream_known) {
    std::memcpy(record.stream_id, stream.data, sizeof(record.stream_id));
  }
  QarGuiPanelSize size{};
  record.size_known = !qar_result_is_error(QAR_CALL(qar_gui_panel_get_size, handle, &size));
  if (record.size_known) {
    record.width_meters = size.width_meters;
    record.height_meters = size.height_meters;
  }
  {
    const std::lock_guard<std::mutex> record_lock(self->mutex_);
    self->record_log_.note(slot->panel_key, record, std::chrono::system_clock::now());
  }

  if (qar_result_is_error(r) || state != QAR_GUI_PANEL_STATE_CLOSED) {
    // Every other state is a panel that is still there: minimized and hidden
    // are the operator putting it away, not finishing with it, and we keep
    // streaming in case they bring it back.
    return;
  }
  {
    const std::lock_guard<std::mutex> stats_lock(self->mutex_);
    if (!self->closed_panels_.insert(slot->panel_key).second) {
      return;  // already reported; the runtime republishes a closed panel freely
    }
  }
  self->queue_.push_closed_panel(slot->panel_key);
}

void
QarCApiPanelInputSource::drain_closed_panels(std::vector<std::string> & out)
{
  queue_.drain_closed_panels(out);
}

void
QarCApiPanelInputSource::on_gesture_event(QarGuiPanelGesture * gesture, void * user_state)
{
  // Library background thread. Everything this does must be short, must not
  // block, must not call back into a subscribe function, and must not touch Qt.
  auto * slot = static_cast<qar_qar_adapter::detail::PointerSlot *>(user_state);
  if (slot == nullptr || gesture == nullptr) {
    return;
  }
  const std::lock_guard<std::mutex> lock(slot->mutex);
  auto * self = static_cast<QarCApiPanelInputSource *>(slot->owner);
  if (self == nullptr) {
    return;  // retired; the owner may be long gone
  }
  self->offer(gesture, *slot);
}

void
QarCApiPanelInputSource::offer(
  QarGuiPanelGesture * gesture, const qar_qar_adapter::detail::PointerSlot & slot)
{
  const double now_s = monotonic_seconds();

  // ---------------------------------------------------------------------
  // Read EVERYTHING off the handle first.
  //
  // "valid only for the duration of this call" is not advice: the storage
  // behind it is the library's, and the moment this function returns it may be
  // reused. So the handle is touched only here, at the top, and nothing below
  // this block refers to it. The class already copied-and-queued; this keeps
  // that discipline against an API where it now actually matters.
  // ---------------------------------------------------------------------
  QarPeerId peer{};
  std::uint64_t gesture_id = 0;
  QarGuiPanelGestureKind kind = QAR_GUI_PANEL_GESTURE_KIND_HOVER;
  QarGesturePhase phase = QAR_GESTURE_PHASE_INSTANT;
  QarGuiPanelPoint position{0, 0};
  QarGuiPanelPoint start_position{0, 0};
  QarHandedness raw_hand = QAR_HANDEDNESS_UNSPECIFIED;
  bool has_hand = false;
  std::uint8_t tap_count = 0;

  bool readable = !qar_result_is_error(
    QAR_CALL(qar_gui_panel_gesture_get_lifecycle, gesture, &peer, &gesture_id));
  readable = readable && !qar_result_is_error(
    QAR_CALL(qar_gui_panel_gesture_get_classification, gesture, &kind, &phase));
  readable = readable && !qar_result_is_error(
    QAR_CALL(qar_gui_panel_gesture_get_positions, gesture, &position, &start_position));
  if (readable) {
    // Diagnostic only, and documented as such, so a runtime that cannot supply
    // it must not cost us the whole event. A value of UNSPECIFIED (no hand) maps to Unknown.
    has_hand = !qar_result_is_error(
      QAR_CALL(qar_gui_panel_gesture_get_handedness, gesture, &raw_hand));
    if (qar_result_is_error(QAR_CALL(qar_gui_panel_gesture_get_tap_count, gesture, &tap_count))) {
      tap_count = 0;
    }
  }

  const qar_ar_core::PointerHand hand =
    has_hand ? qar_ar_core::pointer_hand_from_qar(raw_hand)
             : qar_ar_core::PointerHand::Unknown;

  // What the mapper takes: the values the handle held, in the seam's vocabulary.
  seam::PanelGestureEvent event;
  event.readable = readable;
  event.peer = to_seam(peer);
  event.gesture_id = gesture_id;
  event.kind = gesture_kind_from_c(kind);
  event.phase = gesture_phase_from_c(phase);
  event.x_pixels = position.x_pixels;
  event.y_pixels = position.y_pixels;
  event.hand = to_seam(hand);
  event.tap_count = tap_count;

  seam::GestureSlot mapper_slot;
  mapper_slot.panel_key = slot.panel_key;
  mapper_slot.content_width = slot.content_width;
  mapper_slot.content_height = slot.content_height;

  seam::PanelPointerSample sample;
  bool queue_it = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const seam::GestureOutcome outcome = mapper_.offer(event, mapper_slot, now_s, sample);
    const std::uint64_t events_received = mapper_.stats().events_received;

    // Ground truth for a live debug session: what the runtime actually handed
    // us, before anything downstream can discard it. Recorded here (a push under
    // a lock already held) and PRINTED in drain(), because this is the runtime's
    // callback thread and it must not do I/O.
    if (should_trace_event(events_received) && pending_log_.size() < kMaxPendingTrace) {
      ReceivedEventLog record;
      record.index = events_received;
      record.panel_key = slot.panel_key;
      record.kind = readable ? panel_gesture_kind_name(kind) : "unreadable";
      record.phase = readable ? gesture_phase_name(phase) : "unreadable";
      record.x_pixels = position.x_pixels;
      record.y_pixels = position.y_pixels;
      record.gesture_id = gesture_id;
      record.hand = hand;
      record.tap_count = tap_count;
      switch (outcome) {
        case seam::GestureOutcome::Sampled:
          record.placed = true;
          record.u = sample.u;
          record.v = sample.v;
          break;
        case seam::GestureOutcome::Invalid:
          // There is no "invalid" flag; an accessor that fails IS the
          // invalid event, and skipping means skipping: not a Leave, not a guess.
          record.drop_reason = "the handle could not be read";
          break;
        case seam::GestureOutcome::UnknownKind:
          // A kind or phase this build does not know: a future SDK enumerator.
          // Counted separately so it reads as "newer runtime" rather than "broken".
          record.drop_reason = "gesture kind or phase outside the enums this build knows";
          break;
        case seam::GestureOutcome::NoContentSize:
          record.drop_reason = "subscription has no content size";
          break;
      }
      pending_log_.push_back(std::move(record));
    }
    queue_it = outcome == seam::GestureOutcome::Sampled;
  }

  if (queue_it) {
    queue_.push(sample);
  }
}

void
QarCApiPanelInputSource::sweep_lifecycles(double now_s, std::vector<seam::PanelPointerSample> & out)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  mapper_.sweep_lifecycles(now_s, out);
}

void
QarCApiPanelInputSource::cancel_lifecycles_for_panel(const std::string & panel_key)
{
  std::vector<seam::PanelPointerSample> cancels;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    mapper_.cancel_lifecycles_for_panel(panel_key, monotonic_seconds(), cancels);
  }
  // Queued rather than appended: this runs from unsubscribe, not from drain,
  // so the Qt thread collects them on its next tick like any other sample.
  for (const auto & cancel : cancels) {
    queue_.push(cancel);
  }
}

void
QarCApiPanelInputSource::flush_event_trace()
{
  std::vector<ReceivedEventLog> records;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (pending_log_.empty()) {
      return;
    }
    records.swap(pending_log_);
  }
  for (const ReceivedEventLog & r : records) {
    if (r.placed) {
      std::fprintf(
        stderr,
        "[qar-panel-input] #%llu panel='%s' %s/%s id=%llu hand=%s taps=%u x=%d y=%d "
        "-> u=%.4f v=%.4f\n",
        static_cast<unsigned long long>(r.index), r.panel_key.c_str(), r.kind, r.phase,
        static_cast<unsigned long long>(r.gesture_id), qar_ar_core::to_string(r.hand),
        static_cast<unsigned>(r.tap_count), r.x_pixels, r.y_pixels, static_cast<double>(r.u),
        static_cast<double>(r.v));
    } else {
      std::fprintf(
        stderr,
        "[qar-panel-input] #%llu panel='%s' %s/%s id=%llu x=%d y=%d -> SKIPPED (%s)\n",
        static_cast<unsigned long long>(r.index), r.panel_key.c_str(), r.kind, r.phase,
        static_cast<unsigned long long>(r.gesture_id), r.x_pixels, r.y_pixels,
        r.drop_reason.c_str());
    }
  }
  std::fflush(stderr);
}

void
QarCApiPanelInputSource::flush_runtime_records()
{
  std::vector<std::string> lines;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    lines = record_log_.take_lines();
  }
  if (lines.empty()) {
    return;
  }
  for (const std::string & line : lines) {
    std::fprintf(stderr, "%s\n", line.c_str());
  }
  std::fflush(stderr);
}

void
QarCApiPanelInputSource::drain(std::vector<seam::PanelPointerSample> & out)
{
  std::vector<seam::PanelPointerSample> samples;
  queue_.drain(samples);

  // Qt thread: safe to do I/O. Printing here rather than in the callback is the
  // whole point of pending_log_.
  flush_event_trace();
  flush_runtime_records();

  // The liveness backstop. A hover that ENDS says so (HOVER has a real
  // lifecycle), so this is not the ordinary way a
  // widget gets its Leave -- it is what happens when the producer stops talking
  // mid-gesture, which the header tells us to treat as a cancel after three
  // 250 ms heartbeats.
  sweep_lifecycles(monotonic_seconds(), samples);
  for (seam::PanelPointerSample & sample : samples) {
    out.push_back(std::move(sample));
  }
}

void
QarCApiPanelInputSource::set_lifecycle_timeout(double seconds)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  mapper_.set_lifecycle_timeout(seconds);
}

double
QarCApiPanelInputSource::lifecycle_timeout() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return mapper_.lifecycle_timeout();
}

std::size_t
QarCApiPanelInputSource::subscription_count() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return panels_.size();
}

bool
QarCApiPanelInputSource::has_panel(const std::string & panel_key) const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return panels_.find(panel_key) != panels_.end();
}

PanelInputStats
QarCApiPanelInputSource::stats() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const seam::GestureStats & counted = mapper_.stats();
  PanelInputStats out;
  out.events_received = counted.events_received;
  out.events_invalid = counted.events_invalid;
  out.events_clamped = counted.events_clamped;
  out.lifecycle_timeouts = counted.lifecycle_timeouts;
  out.hover_timeouts = counted.hover_timeouts;
  out.events_unknown_kind = counted.events_unknown_kind;
  out.events_multi_tap = counted.events_multi_tap;
  out.events_drag = counted.events_drag;
  out.events_long_press = counted.events_long_press;
  return out;
}

}  // namespace qar_capi_panels
