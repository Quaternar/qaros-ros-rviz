// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/action_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "rcl_action/action_client.h"
#include "rclcpp/create_timer.hpp"
#include "rclcpp/timer.hpp"

#include "qar_ros_ui/detail/dynamic_action_client.hpp"
#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/graph_discovery.hpp"
#include "qar_ros_ui/introspection.hpp"

#include "conversion_internal.hpp"

namespace qar_ros_ui
{

using qar_ui_model::UiValue;
using Clock = std::chrono::steady_clock;

namespace
{

constexpr std::chrono::milliseconds kPollPeriod{50};

void report(RosUiDiagnosticsSink * sink, DiagnosticSeverity severity, const std::string & message)
{
  if (sink != nullptr) {
    sink->on_message(severity, "action", message);
  }
}

UiValue uuid_value(const GoalId & id)
{
  UiValue uuid = UiValue::array();
  for (std::uint8_t byte : id) {
    uuid.push_back(UiValue(static_cast<std::int64_t>(byte)));
  }
  UiValue goal_id = UiValue::object();
  goal_id.set("uuid", std::move(uuid));
  return goal_id;
}

/// Reads `<member>.uuid` (uint8[16]) straight off an instance, no conversion.
bool read_goal_id(const void * message, const MessageMembers & members, const char * member_name, GoalId & out)
{
  const MessageMember * goal_id = find_member(members, member_name);
  if (goal_id == nullptr || goal_id->members_ == nullptr || goal_id->members_->data == nullptr) {
    return false;
  }
  const auto * uuid_members = static_cast<const MessageMembers *>(goal_id->members_->data);
  const MessageMember * uuid = find_member(*uuid_members, "uuid");
  if (uuid == nullptr || !uuid->is_array_ || uuid->array_size_ != out.size() ||
    uuid->get_const_function == nullptr)
  {
    return false;
  }
  const void * goal_id_base = static_cast<const std::uint8_t *>(message) + goal_id->offset_;
  const void * uuid_base = static_cast<const std::uint8_t *>(goal_id_base) + uuid->offset_;
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = *static_cast<const std::uint8_t *>(uuid->get_const_function(uuid_base, i));
  }
  return true;
}

GoalStatus status_from_int(std::int64_t value)
{
  if (value < 0 || value > static_cast<std::int64_t>(GoalStatus::Aborted)) {
    return GoalStatus::Unknown;
  }
  return static_cast<GoalStatus>(value);
}

}  // namespace

const char * to_string(GoalStatus status)
{
  switch (status) {
    case GoalStatus::Unknown: return "unknown";
    case GoalStatus::Accepted: return "accepted";
    case GoalStatus::Executing: return "executing";
    case GoalStatus::Canceling: return "canceling";
    case GoalStatus::Succeeded: return "succeeded";
    case GoalStatus::Canceled: return "canceled";
    case GoalStatus::Aborted: return "aborted";
  }
  return "unknown";
}

bool is_terminal(GoalStatus status)
{
  return status == GoalStatus::Succeeded || status == GoalStatus::Canceled ||
         status == GoalStatus::Aborted;
}

std::string goal_id_to_string(const GoalId & id)
{
  static const char digits[] = "0123456789abcdef";
  std::string text;
  text.reserve(id.size() * 2);
  for (std::uint8_t byte : id) {
    text.push_back(digits[byte >> 4]);
    text.push_back(digits[byte & 0x0F]);
  }
  return text;
}

const char * to_string(GoalEventKind kind)
{
  switch (kind) {
    case GoalEventKind::Rejected: return "rejected";
    case GoalEventKind::Accepted: return "accepted";
    case GoalEventKind::Feedback: return "feedback";
    case GoalEventKind::Status: return "status";
    case GoalEventKind::Result: return "result";
    case GoalEventKind::CancelResponse: return "cancel_response";
    case GoalEventKind::Error: return "error";
  }
  return "unknown";
}

// --- ActionGoalHandle -------------------------------------------------------

struct ActionGoalHandle::State
{
  mutable std::mutex mutex;
  GoalId id{};
  std::string action;
  std::string type_name;
  GoalStatus status = GoalStatus::Unknown;
  bool accepted = false;
  bool rejected = false;
  bool finished = false;
  bool have_result = false;
  UiValue feedback;
  std::uint64_t feedback_sequence = 0;
  std::uint64_t feedback_taken = 0;
  UiValue result;
  GoalStatus terminal = GoalStatus::Unknown;
  Result error;
  GoalEventCallback on_event;
  ConversionLimits limits;
};

ActionGoalHandle::ActionGoalHandle(std::shared_ptr<State> state)
: state_(std::move(state))
{
}

ActionGoalHandle::~ActionGoalHandle() = default;

const GoalId & ActionGoalHandle::goal_id() const {return state_->id;}
std::string ActionGoalHandle::goal_id_string() const {return goal_id_to_string(state_->id);}
const std::string & ActionGoalHandle::action_name() const {return state_->action;}
const std::string & ActionGoalHandle::type_name() const {return state_->type_name;}

GoalStatus ActionGoalHandle::status() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->status;
}

bool ActionGoalHandle::accepted() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->accepted;
}

bool ActionGoalHandle::rejected() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->rejected;
}

bool ActionGoalHandle::finished() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->finished;
}

bool ActionGoalHandle::take_feedback(UiValue & out, std::uint64_t * sequence)
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->feedback_sequence == 0 || state_->feedback_sequence == state_->feedback_taken) {
    return false;
  }
  state_->feedback_taken = state_->feedback_sequence;
  out = state_->feedback;
  if (sequence != nullptr) {
    *sequence = state_->feedback_sequence;
  }
  return true;
}

bool ActionGoalHandle::latest_feedback(UiValue & out, std::uint64_t * sequence) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->feedback_sequence == 0) {
    return false;
  }
  out = state_->feedback;
  if (sequence != nullptr) {
    *sequence = state_->feedback_sequence;
  }
  return true;
}

std::uint64_t ActionGoalHandle::feedback_count() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->feedback_sequence;
}

bool ActionGoalHandle::result(UiValue & out, GoalStatus * terminal_status) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (!state_->have_result) {
    return false;
  }
  out = state_->result;
  if (terminal_status != nullptr) {
    *terminal_status = state_->terminal;
  }
  return true;
}

Result ActionGoalHandle::error() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->error;
}

ActionGoalDiagnostics ActionGoalHandle::diagnostics() const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  ActionGoalDiagnostics out;
  out.action = state_->action;
  out.type_name = state_->type_name;
  out.goal_id = goal_id_to_string(state_->id);
  out.status = state_->status;
  out.accepted = state_->accepted;
  out.finished = state_->finished;
  out.feedback_count = state_->feedback_sequence;
  out.last_error = state_->error.message;
  return out;
}

// --- ActionBridgeImpl -------------------------------------------------------

struct ActionBridgeImpl : public std::enable_shared_from_this<ActionBridgeImpl>
{
  struct PendingGoal
  {
    std::shared_ptr<ActionGoalHandle> handle;
    std::shared_ptr<DynamicActionClient> client;
    DynamicMessage request;
    bool sent = false;
    Clock::time_point deadline;
  };

  NodeHandles handles;
  TypeSupportRegistry * registry = nullptr;
  RosUiDiagnosticsSink * sink = nullptr;

  mutable std::mutex mutex;
  std::map<std::string, std::shared_ptr<DynamicActionClient>> clients;
  std::vector<PendingGoal> pending;  ///< Until accepted or rejected.
  std::map<GoalId, std::shared_ptr<ActionGoalHandle>> active;  ///< Accepted, until the result.
  rclcpp::TimerBase::SharedPtr timer;
  bool timer_active = false;
  std::uint64_t goals_sent = 0;
  std::uint64_t goals_accepted = 0;
  std::uint64_t goals_rejected = 0;
  std::uint64_t results = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t failures = 0;
  std::string last_error;

  ~ActionBridgeImpl()
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (timer) {
      timer->cancel();
    }
    pending.clear();
    active.clear();
    clients.clear();
  }

  static std::string client_key(const std::string & action, const std::string & type_name)
  {
    return action + "|" + type_name;
  }

  static ActionGoalHandle::State & state_of(const ActionGoalHandle & handle)
  {
    return *handle.state_;
  }

  Result client_for_locked(
    const std::string & action, const std::string & type_name,
    std::shared_ptr<DynamicActionClient> & out)
  {
    const std::string key = client_key(action, type_name);
    const auto cached = clients.find(key);
    if (cached != clients.end()) {
      out = cached->second;
      return Result::success();
    }
    ActionTypeSupport ts;
    Result result = registry->action(type_name, ts);
    if (!result.ok()) {
      return result;
    }
    if (!ts.valid()) {
      return Result::error(Status::TypeSupportUnavailable, "action typesupport for " + type_name + " is incomplete");
    }
    std::shared_ptr<DynamicActionClient> client;
    try {
      // Mirrors rclcpp_action::create_client: the deleter removes the waitable
      // from the node, so dropping the client is enough to unregister it.
      std::weak_ptr<rclcpp::node_interfaces::NodeWaitablesInterface> weak_waitables = handles.waitables;
      auto deleter = [weak_waitables](DynamicActionClient * ptr) {
          if (ptr == nullptr) {
            return;
          }
          if (auto waitables = weak_waitables.lock()) {
            std::shared_ptr<DynamicActionClient> fake(ptr, [](DynamicActionClient *) {});
            waitables->remove_waitable(fake, nullptr);
          }
          delete ptr;
        };
      client = std::shared_ptr<DynamicActionClient>(
        new DynamicActionClient(handles, ts, action, rcl_action_client_get_default_options()),
        deleter);
      handles.waitables->add_waitable(client, nullptr);
    } catch (const std::exception & e) {
      return Result::error(Status::RclError, e.what());
    }
    std::weak_ptr<ActionBridgeImpl> weak = weak_from_this();
    DynamicActionClient * raw = client.get();
    client->set_handlers(
      [weak, raw](const void * feedback_message) {
        if (auto self = weak.lock()) {
          self->on_feedback(raw, feedback_message);
        }
      },
      [weak](const action_msgs::msg::GoalStatusArray & status) {
        if (auto self = weak.lock()) {
          self->on_status(status);
        }
      });
    clients[key] = client;
    out = client;
    return Result::success();
  }

  static bool ready(DynamicActionClient & client)
  {
    try {
      return client.action_server_is_ready();
    } catch (const std::exception &) {
      return false;
    }
  }

  /// A goal we sent, accepted or not yet answered. Feedback and status can
  /// overtake the goal response: Humble's ClientBase::take_data() drains the
  /// feedback subscription before the goal client, and a server that starts
  /// executing at once publishes feedback within microseconds of accepting.
  /// The goal id is ours, so anything addressed to it is kept, not dropped.
  std::shared_ptr<ActionGoalHandle> find_goal_locked(const GoalId & id) const
  {
    const auto active_it = active.find(id);
    if (active_it != active.end()) {
      return active_it->second;
    }
    for (const PendingGoal & goal : pending) {
      if (goal.sent && goal.handle->goal_id() == id) {
        return goal.handle;
      }
    }
    return nullptr;
  }

  static void emit_event(const std::shared_ptr<ActionGoalHandle> & handle, const GoalEvent & event)
  {
    GoalEventCallback callback;
    {
      std::lock_guard<std::mutex> lock(state_of(*handle).mutex);
      callback = state_of(*handle).on_event;
    }
    if (callback) {
      callback(handle, event);
    }
  }

  void ensure_timer_locked()
  {
    if (timer) {
      if (!timer_active) {
        timer->reset();
        timer_active = true;
      }
      return;
    }
    std::weak_ptr<ActionBridgeImpl> weak = weak_from_this();
    try {
      timer = rclcpp::create_wall_timer(
        kPollPeriod,
        [weak]() {
          if (auto self = weak.lock()) {
            self->poll();
          }
        },
        nullptr, handles.base.get(), handles.timers.get());
      timer_active = true;
    } catch (const std::exception & e) {
      report(
        sink, DiagnosticSeverity::Warning,
        std::string("no wall timer for goal timeouts (") + e.what() + "); call poll() periodically");
    }
  }

  void idle_timer_locked()
  {
    if (pending.empty() && timer && timer_active) {
      timer->cancel();
      timer_active = false;
    }
  }

  /// Sends the goal request of @p goal; the response lands in on_goal_response.
  Result send_locked(PendingGoal & goal)
  {
    std::weak_ptr<ActionBridgeImpl> weak = weak_from_this();
    std::shared_ptr<ActionGoalHandle> handle = goal.handle;
    std::shared_ptr<DynamicActionClient> client = goal.client;
    Result result = goal.client->send_goal(
      std::move(goal.request),
      [weak, handle, client](std::shared_ptr<void> response) {
        if (auto self = weak.lock()) {
          self->on_goal_response(handle, client, std::move(response));
        }
      });
    if (result.ok()) {
      goal.sent = true;
    }
    return result;
  }

  static void finish_with_error(ActionGoalHandle::State & state, Result error)
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.error = std::move(error);
    state.finished = true;
  }

  void on_goal_response(
    const std::shared_ptr<ActionGoalHandle> & handle,
    const std::shared_ptr<DynamicActionClient> & client,
    std::shared_ptr<void> response)
  {
    ActionGoalHandle::State & state = state_of(*handle);
    bool accepted = false;
    {
      const MessageMembers * members = client->type_support().send_goal_response.members();
      if (members != nullptr && response) {
        const UiValue value = message_to_ui_value(response.get(), *members);
        const UiValue * flag = value.find("accepted");
        accepted = flag != nullptr && flag->as_bool();
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      const auto it = std::find_if(
        pending.begin(), pending.end(),
        [&handle](const PendingGoal & goal) {return goal.handle == handle;});
      if (it == pending.end()) {
        return;  // already timed out and reported
      }
      pending.erase(it);
      if (accepted) {
        active[state.id] = handle;
        ++goals_accepted;
      } else {
        ++goals_rejected;
      }
      idle_timer_locked();
    }
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.accepted = accepted;
      state.rejected = !accepted;
      if (accepted) {
        state.status = GoalStatus::Accepted;
      } else {
        state.finished = true;
      }
    }
    GoalEvent event;
    event.kind = accepted ? GoalEventKind::Accepted : GoalEventKind::Rejected;
    event.status = accepted ? GoalStatus::Accepted : GoalStatus::Unknown;
    emit_event(handle, event);
    if (!accepted) {
      return;
    }

    // The result request is answered when the goal reaches a terminal state.
    DynamicMessage request(client->type_support().get_result_request);
    UiValue request_value = UiValue::object();
    request_value.set("goal_id", uuid_value(state.id));
    Result result = request.valid() ?
      request.from_ui_value(request_value) :
      Result::error(Status::TypeSupportUnavailable, "cannot build the result request");
    if (result.ok()) {
      std::weak_ptr<ActionBridgeImpl> weak = weak_from_this();
      result = client->send_result(
        std::move(request),
        [weak, handle, client](std::shared_ptr<void> response) {
          if (auto self = weak.lock()) {
            self->on_result_response(handle, client, std::move(response));
          }
        });
    }
    if (!result.ok()) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        active.erase(state.id);
        ++failures;
        last_error = result.message;
      }
      finish_with_error(state, result);
      GoalEvent error_event;
      error_event.kind = GoalEventKind::Error;
      error_event.error = result;
      emit_event(handle, error_event);
    }
  }

  void on_result_response(
    const std::shared_ptr<ActionGoalHandle> & handle,
    const std::shared_ptr<DynamicActionClient> & client,
    std::shared_ptr<void> response)
  {
    ActionGoalHandle::State & state = state_of(*handle);
    GoalStatus terminal = GoalStatus::Unknown;
    UiValue result_value;
    {
      const MessageMembers * members = client->type_support().get_result_response.members();
      ConversionLimits limits;
      {
        std::lock_guard<std::mutex> lock(state.mutex);
        limits = state.limits;
      }
      if (members != nullptr && response) {
        UiValue value = message_to_ui_value(response.get(), *members, limits);
        if (const UiValue * status = value.find("status")) {
          terminal = status_from_int(status->as_int());
        }
        if (UiValue * result = value.find("result")) {
          result_value = std::move(*result);
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      active.erase(state.id);
      ++results;
      idle_timer_locked();
    }
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.status = terminal;
      state.terminal = terminal;
      state.result = result_value;
      state.have_result = true;
      state.finished = true;
    }
    GoalEvent event;
    event.kind = GoalEventKind::Result;
    event.status = terminal;
    event.value = std::move(result_value);
    emit_event(handle, event);
  }

  void on_feedback(DynamicActionClient * client, const void * feedback_message)
  {
    const MessageMembers * members = client->type_support().feedback_message.members();
    if (members == nullptr || feedback_message == nullptr) {
      return;
    }
    GoalId id{};
    if (!read_goal_id(feedback_message, *members, "goal_id", id)) {
      return;
    }
    std::shared_ptr<ActionGoalHandle> handle;
    {
      std::lock_guard<std::mutex> lock(mutex);
      handle = find_goal_locked(id);
    }
    if (!handle) {
      return;  // somebody else's goal
    }
    ActionGoalHandle::State & state = state_of(*handle);
    const MessageMember * feedback = find_member(*members, "feedback");
    if (feedback == nullptr) {
      return;
    }
    ConversionLimits limits;
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      limits = state.limits;
    }
    UiValue value = detail::member_to_ui_value(feedback_message, *feedback, limits, nullptr, 0);
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.feedback = value;
      ++state.feedback_sequence;
    }
    GoalEvent event;
    event.kind = GoalEventKind::Feedback;
    event.value = std::move(value);
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      event.status = state.status;
    }
    emit_event(handle, event);
  }

  void on_status(const action_msgs::msg::GoalStatusArray & status_array)
  {
    for (const action_msgs::msg::GoalStatus & status : status_array.status_list) {
      GoalId id{};
      std::copy(
        status.goal_info.goal_id.uuid.begin(), status.goal_info.goal_id.uuid.end(), id.begin());
      std::shared_ptr<ActionGoalHandle> handle;
      {
        std::lock_guard<std::mutex> lock(mutex);
        handle = find_goal_locked(id);
      }
      if (!handle) {
        continue;
      }
      ActionGoalHandle::State & state = state_of(*handle);
      const GoalStatus next = status_from_int(status.status);
      bool changed = false;
      {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.finished && state.status != next) {
          state.status = next;
          changed = true;
        }
      }
      if (changed) {
        GoalEvent event;
        event.kind = GoalEventKind::Status;
        event.status = next;
        emit_event(handle, event);
      }
    }
  }

  std::size_t poll()
  {
    struct Failed
    {
      std::shared_ptr<ActionGoalHandle> handle;
      Result error;
    };
    std::vector<Failed> failed;
    {
      std::lock_guard<std::mutex> lock(mutex);
      const Clock::time_point now = Clock::now();
      for (auto it = pending.begin(); it != pending.end();) {
        PendingGoal & goal = *it;
        if (!goal.sent && ready(*goal.client)) {
          Result sent = send_locked(goal);
          if (!sent.ok()) {
            ++failures;
            last_error = sent.message;
            failed.push_back({goal.handle, sent});
            it = pending.erase(it);
            continue;
          }
        }
        if (now >= goal.deadline) {
          ++timeouts;
          const std::string reason = goal.sent ?
            "no goal response from '" + goal.handle->action_name() + "'" :
            "no server for '" + goal.handle->action_name() + "'";
          failed.push_back({goal.handle, Result::error(Status::Timeout, reason)});
          it = pending.erase(it);
          continue;
        }
        ++it;
      }
      idle_timer_locked();
    }
    for (Failed & entry : failed) {
      finish_with_error(state_of(*entry.handle), entry.error);
      GoalEvent event;
      event.kind = GoalEventKind::Error;
      event.error = entry.error;
      emit_event(entry.handle, event);
    }
    return failed.size();
  }
};

// --- ActionBridge -----------------------------------------------------------

ActionBridge::ActionBridge(
  NodeHandles handles, TypeSupportRegistry * registry, RosUiDiagnosticsSink * sink)
: impl_(std::make_shared<ActionBridgeImpl>())
{
  impl_->handles = std::move(handles);
  impl_->registry = registry ? registry : &TypeSupportRegistry::global();
  impl_->sink = sink;
}

ActionBridge::~ActionBridge() = default;

Result ActionBridge::resolve_action_type(const std::string & action, std::string & out_type) const
{
  if (!impl_->handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  std::string resolved = action;
  try {
    resolved = impl_->handles.base->resolve_topic_or_service_name(action, false);
  } catch (const std::exception & e) {
    return Result::error(Status::InvalidArgument, "invalid action name '" + action + "': " + e.what());
  }
  GraphSnapshot snapshot;
  Result result = take_graph_snapshot(impl_->handles, snapshot);
  if (!result.ok()) {
    return result;
  }
  const GraphEntity * entity = snapshot.find_action(resolved);
  if (entity == nullptr || entity->types.empty()) {
    return Result::error(Status::NotFound, "action '" + resolved + "' is not on the graph");
  }
  out_type = entity->primary_type();
  return Result::success();
}

Result ActionBridge::describe(const std::string & type_name, qar_ui_model::ActionDescription & out) const
{
  return introspect_action(type_name, out, impl_->registry);
}

bool ActionBridge::server_available(const std::string & action, const std::string & type_name)
{
  std::string type = type_name;
  if (type.empty() && !resolve_action_type(action, type).ok()) {
    return false;
  }
  std::shared_ptr<DynamicActionClient> client;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->client_for_locked(action, type, client).ok()) {
      return false;
    }
  }
  return ActionBridgeImpl::ready(*client);
}

Result ActionBridge::send_goal(
  const std::string & action,
  const std::string & type_name,
  const UiValue & goal,
  const ActionGoalOptions & options,
  std::shared_ptr<ActionGoalHandle> & out)
{
  ActionBridgeImpl & impl = *impl_;
  if (!impl.handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  if (action.empty()) {
    return Result::error(Status::InvalidArgument, "action name is empty");
  }
  std::string type = type_name;
  if (type.empty()) {
    Result resolved = resolve_action_type(action, type);
    if (!resolved.ok()) {
      return resolved;
    }
  }

  std::lock_guard<std::mutex> lock(impl.mutex);
  std::shared_ptr<DynamicActionClient> client;
  Result result = impl.client_for_locked(action, type, client);
  if (!result.ok()) {
    ++impl.failures;
    impl.last_error = result.message;
    return result;
  }

  auto state = std::make_shared<ActionGoalHandle::State>();
  state->id = client->new_goal_id();
  state->action = action;
  state->type_name = client->type_support().type_name;
  state->on_event = options.on_event;
  state->limits = options.limits;
  std::shared_ptr<ActionGoalHandle> handle(new ActionGoalHandle(state));

  ActionBridgeImpl::PendingGoal pending_goal;
  pending_goal.handle = handle;
  pending_goal.client = client;
  pending_goal.request = DynamicMessage(client->type_support().send_goal_request);
  if (!pending_goal.request.valid()) {
    return Result::error(Status::TypeSupportUnavailable, "cannot build the goal request for " + type);
  }
  UiValue request_value = UiValue::object();
  request_value.set("goal_id", uuid_value(state->id));
  request_value.set("goal", goal);
  result = pending_goal.request.from_ui_value(request_value, options.conversion);
  if (!result.ok()) {
    ++impl.failures;
    impl.last_error = result.message;
    return result;
  }
  pending_goal.deadline = Clock::now() + options.response_timeout;

  if (ActionBridgeImpl::ready(*client)) {
    result = impl.send_locked(pending_goal);
    if (!result.ok()) {
      ++impl.failures;
      impl.last_error = result.message;
      return result;
    }
  } else if (!options.wait_for_server) {
    ++impl.failures;
    impl.last_error = "no server for '" + action + "'";
    return Result::error(Status::NotFound, impl.last_error);
  }
  ++impl.goals_sent;
  impl.pending.push_back(std::move(pending_goal));
  impl.ensure_timer_locked();
  out = handle;
  return Result::success();
}

Result ActionBridge::cancel(const std::shared_ptr<ActionGoalHandle> & handle)
{
  if (!handle) {
    return Result::error(Status::InvalidArgument, "goal handle is null");
  }
  ActionBridgeImpl & impl = *impl_;
  std::shared_ptr<DynamicActionClient> client;
  {
    std::lock_guard<std::mutex> lock(impl.mutex);
    const auto it =
      impl.clients.find(ActionBridgeImpl::client_key(handle->action_name(), handle->type_name()));
    if (it == impl.clients.end()) {
      return Result::error(Status::NotFound, "no client for '" + handle->action_name() + "'");
    }
    client = it->second;
  }
  if (handle->finished()) {
    return Result::error(Status::InvalidState, "goal already finished");
  }
  auto request = std::make_shared<action_msgs::srv::CancelGoal::Request>();
  std::copy(
    handle->goal_id().begin(), handle->goal_id().end(), request->goal_info.goal_id.uuid.begin());
  return client->send_cancel(
    request,
    [handle](std::shared_ptr<void> response) {
      auto cancel_response = std::static_pointer_cast<action_msgs::srv::CancelGoal::Response>(response);
      GoalEvent event;
      event.kind = GoalEventKind::CancelResponse;
      UiValue value = UiValue::object();
      UiValue canceling = UiValue::array();
      if (cancel_response) {
        value.set("return_code", UiValue(static_cast<std::int64_t>(cancel_response->return_code)));
        for (const action_msgs::msg::GoalInfo & info : cancel_response->goals_canceling) {
          GoalId id{};
          std::copy(info.goal_id.uuid.begin(), info.goal_id.uuid.end(), id.begin());
          canceling.push_back(UiValue(goal_id_to_string(id)));
        }
        if (cancel_response->return_code != action_msgs::srv::CancelGoal::Response::ERROR_NONE) {
          event.error = Result::error(
            Status::Rejected,
            "cancel refused with return code " + std::to_string(cancel_response->return_code));
        }
      }
      value.set("goals_canceling", std::move(canceling));
      event.value = std::move(value);
      event.status = handle->status();
      ActionBridgeImpl::emit_event(handle, event);
    });
}

std::size_t ActionBridge::poll()
{
  return impl_->poll();
}

std::size_t ActionBridge::active_goal_count() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->pending.size() + impl_->active.size();
}

ActionBridgeDiagnostics ActionBridge::diagnostics() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ActionBridgeDiagnostics out;
  out.clients = impl_->clients.size();
  out.active_goals = impl_->pending.size() + impl_->active.size();
  out.goals_sent = impl_->goals_sent;
  out.goals_accepted = impl_->goals_accepted;
  out.goals_rejected = impl_->goals_rejected;
  out.results = impl_->results;
  out.timeouts = impl_->timeouts;
  out.failures = impl_->failures;
  out.last_error = impl_->last_error;
  return out;
}

}  // namespace qar_ros_ui
