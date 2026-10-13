// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__TYPE_DESCRIPTION_HPP_
#define QAR_UI_MODEL__TYPE_DESCRIPTION_HPP_

/// @file type_description.hpp
/// @brief ROS-agnostic description of a message / service / action payload.
///
/// This is the schema seam between ROS and the UI model. qar_ros_ui fills it
/// from `rosidl_typesupport_introspection_cpp` (it is shaped 1:1 after that
/// API's `MessageMember`, so the mapping is mechanical), templates turn it into
/// widgets, and this library uses it to validate bindings and values. Nothing
/// in here knows what ROS is: a `MessageDescription` could equally describe a
/// hand-written composite ("battery" + "odom" under one root) that a panel
/// aggregates from several topics.
///
/// Everything ROS 2 IDL can express is representable:
///   * every primitive kind rosidl has, including the .idl-only ones (`wchar`,
///     `long double`) so an introspected member always has a home;
///   * nested messages, keeping the original ROS type name;
///   * fixed (`T[N]`), bounded (`T[<=N]`) and unbounded (`T[]`) arrays, and
///     bounded strings (`string<=N`);
///   * default values, and constants where the source knows them (Humble's
///     introspection does not expose constants at run time; a .msg parser or
///     a template does — hence "where knowable");
///   * per-field UI metadata: label, unit, description, documented range/step.
///
/// Nested descriptions are held by `shared_ptr<const>` so one introspected
/// `std_msgs/msg/Header` is shared by every message that embeds it and a
/// registry can hand out the same instance. Descriptions are immutable once
/// built; use @ref MessageDescriptionBuilder or the chainable setters on
/// @ref FieldDescription to construct them.
///
/// Paths are dotted, with `[i]` for array elements: `pose.position.x`,
/// `ranges[3]`, `points[2].x`. The same grammar is used for widget bindings,
/// for the wire protocol's data patches, and by the page's JavaScript runtime.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qar_ui_model/ui_value.hpp"

namespace qar_ui_model
{

// ---------------------------------------------------------------------------
// Field kinds
// ---------------------------------------------------------------------------

/// @brief Element kind of a field. One entry per rosidl type id.
enum class FieldKind : std::uint8_t
{
  Bool = 0,
  Byte,        ///< IDL `octet` / .msg `byte`: carried as an Int 0..255.
  Char,        ///< .msg `char` (a uint8 in ROS 2): carried as an Int 0..255.
  WChar,       ///< .idl `wchar`: carried as an Int 0..65535.
  Int8,
  UInt8,
  Int16,
  UInt16,
  Int32,
  UInt32,
  Int64,
  UInt64,      ///< Values above INT64_MAX arrive as Double (see UiValue).
  Float32,
  Float64,
  LongDouble,  ///< Carried as Double; precision beyond double is not preserved.
  String,
  WString,     ///< Carried as a UTF-8 String; qar_ros_ui transcodes.
  Message      ///< Nested message: see FieldDescription::message.
};

/// @brief ROS IDL spelling: "bool", "float64", "string", ..., "message".
const char * to_string(FieldKind kind);

/// @brief Parse a ROS IDL primitive name. Accepts both the .msg spellings
/// (`bool`, `byte`, `float32`, `float64`) and the .idl ones (`boolean`,
/// `octet`, `float`, `double`, `wchar`, `long double`). Never yields Message.
bool field_kind_from_ros_name(std::string_view name, FieldKind & out_kind);

bool is_boolean(FieldKind kind);
bool is_integer(FieldKind kind);  ///< Byte/Char/WChar and the (u)intN kinds.
bool is_signed_integer(FieldKind kind);
bool is_floating(FieldKind kind);
bool is_numeric(FieldKind kind);  ///< is_integer || is_floating
bool is_string(FieldKind kind);   ///< String or WString
bool is_primitive(FieldKind kind);  ///< Everything but Message

/// @brief Representable range of an integer kind, as doubles for UI use.
/// Exact for every kind up to 32 bits; Int64/UInt64 bounds round to 2^63/2^64.
struct NumericBounds
{
  double min = 0.0;
  double max = 0.0;
};
std::optional<NumericBounds> numeric_bounds(FieldKind kind);

// ---------------------------------------------------------------------------
// Descriptions
// ---------------------------------------------------------------------------

enum class ArrayKind : std::uint8_t
{
  None = 0,   ///< A single value.
  Fixed,      ///< `T[N]`: exactly array_size elements.
  Bounded,    ///< `T[<=N]`: at most array_size elements.
  Unbounded   ///< `T[]`: any number of elements.
};
const char * to_string(ArrayKind kind);

struct MessageDescription;
using MessageDescriptionPtr = std::shared_ptr<const MessageDescription>;

/// @brief One member of a message.
struct FieldDescription
{
  std::string name;
  FieldKind kind = FieldKind::Message;

  /// ROS name of the ELEMENT type: "float64" for a primitive (filled in by
  /// @ref primitive), "geometry_msgs/msg/Point" for a nested message. For an
  /// array field this is still the element type; the array-ness is below.
  std::string type_name;

  ArrayKind array_kind = ArrayKind::None;
  std::size_t array_size = 0;  ///< Fixed: exact length. Bounded: maximum. Else 0.

  /// `string<=N` / `wstring<=N`; 0 = unbounded.
  std::size_t string_upper_bound = 0;

  /// Element type when kind == Message. Never null for a Message field of a
  /// well-formed description (validate_description checks).
  MessageDescriptionPtr message;

  /// IDL default (`int32 x 5`, `string s "hi"`, `int8[3] a [1,2,3]`). Null means
  /// "none": make_default_value then zero-fills. For an array field this is an
  /// Array of element defaults.
  UiValue default_value;

  // --- UI metadata (optional; templates and .msg comments fill these) --------
  std::string label;        ///< Human label. Empty: derived from name by display_label().
  std::string unit;         ///< "m", "m/s", "rad", "%", ...
  std::string description;  ///< Help text, e.g. the .msg comment.
  std::optional<double> min;   ///< Documented range, if any.
  std::optional<double> max;
  std::optional<double> step;  ///< Sensible increment for an input control.

  static FieldDescription primitive(std::string name, FieldKind kind);
  /// A nested-message field of element type @p type (kind = Message).
  static FieldDescription nested(std::string name, MessageDescriptionPtr type);

  FieldDescription & fixed_array(std::size_t size);
  FieldDescription & bounded_array(std::size_t max_size);
  FieldDescription & unbounded_array();
  FieldDescription & string_bound(std::size_t upper_bound);
  FieldDescription & with_default(UiValue value);
  FieldDescription & with_label(std::string text);
  FieldDescription & with_unit(std::string text);
  FieldDescription & with_description(std::string text);
  FieldDescription & with_range(double min_value, double max_value);
  FieldDescription & with_step(double step_value);

  bool is_array() const { return array_kind != ArrayKind::None; }
  bool is_message() const { return kind == FieldKind::Message; }

  /// label, or the humanized name ("linear_velocity" -> "Linear velocity").
  std::string display_label() const;
};

/// @brief A named constant of a message type (`uint8 STATUS_OK=0`).
struct ConstantDescription
{
  std::string name;
  FieldKind kind = FieldKind::Int32;
  UiValue value;
  std::string description;
};

struct MessageDescription
{
  /// Canonical "package/msg/Name" (also "package/srv/Name_Request" and so on
  /// for the halves of a service or action). Free-form for a synthetic root.
  std::string type_name;
  std::string description;
  std::vector<FieldDescription> fields;
  std::vector<ConstantDescription> constants;

  const FieldDescription * find_field(std::string_view name) const;
  const ConstantDescription * find_constant(std::string_view name) const;
};

/// @brief A service: two messages.
struct ServiceDescription
{
  std::string type_name;  ///< "package/srv/Name"
  MessageDescriptionPtr request;
  MessageDescriptionPtr response;
};

/// @brief An action: three messages.
struct ActionDescription
{
  std::string type_name;  ///< "package/action/Name"
  MessageDescriptionPtr goal;
  MessageDescriptionPtr result;
  MessageDescriptionPtr feedback;
};

/// @brief Readable construction of an immutable description.
class MessageDescriptionBuilder
{
public:
  explicit MessageDescriptionBuilder(std::string type_name);

  MessageDescriptionBuilder & description(std::string text);
  MessageDescriptionBuilder & field(FieldDescription field);
  MessageDescriptionBuilder & constant(
    std::string name, FieldKind kind, UiValue value, std::string description = {});

  MessageDescriptionPtr build();
  MessageDescription build_value() const;

private:
  MessageDescription description_;
};

/// @brief Structural problems in a description itself (a Message field with
/// no element type, duplicate field names, a fixed array of size 0, ...).
struct DescriptionProblem
{
  std::string path;  ///< Dotted field path, "" for the root.
  std::string message;
};
std::vector<DescriptionProblem> validate_description(const MessageDescription & root);

// ---------------------------------------------------------------------------
// Type names
// ---------------------------------------------------------------------------

struct TypeNameParts
{
  std::string package;    ///< "geometry_msgs"
  std::string interface;  ///< "msg", "srv" or "action"
  std::string name;       ///< "Pose"
};

/// @brief Split "pkg/msg/Name"; the two-part "pkg/Name" is read as "msg".
bool split_type_name(std::string_view type_name, TypeNameParts & out_parts);

/// @brief "pkg/Name" -> "pkg/msg/Name"; canonical input unchanged; anything
/// unrecognizable is returned as-is.
std::string normalize_type_name(std::string_view type_name);

/// @brief "linear_velocity" -> "Linear velocity", "maxSpeed" -> "Max speed",
/// "x" -> "X". Used wherever a label is derived from an identifier.
std::string humanize_identifier(std::string_view identifier);

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

struct PathSegment
{
  std::string name;
  std::optional<std::size_t> index;  ///< Set for `name[i]`.

  bool operator==(const PathSegment & other) const
  {
    return name == other.name && index == other.index;
  }
  bool operator!=(const PathSegment & other) const { return !(*this == other); }
};

/// @brief A parsed dotted path. The empty path names the root itself.
struct FieldPath
{
  std::vector<PathSegment> segments;

  /// Grammar: `segment ('.' segment)*`, `segment = identifier ('[' digits ']')?`,
  /// `identifier = [A-Za-z_][A-Za-z0-9_]*`. Empty text is the (valid) root.
  static bool parse(std::string_view text, FieldPath & out_path, std::string & out_error);

  std::string to_string() const;
  bool empty() const { return segments.empty(); }

  FieldPath appended(PathSegment segment) const;

  bool operator==(const FieldPath & other) const { return segments == other.segments; }
  bool operator!=(const FieldPath & other) const { return !(*this == other); }
};

/// @brief What a path names inside a description.
struct ResolvedField
{
  const FieldDescription * field = nullptr;   ///< The last segment's field.
  const MessageDescription * owner = nullptr; ///< The message that declares it.
  /// True when the last segment carried `[i]`, so the path names ONE ELEMENT
  /// of an array field (and its kind is the element kind).
  bool indexed = false;

  /// True when the path names a whole array (array field, no index).
  bool is_array_value() const { return field != nullptr && field->is_array() && !indexed; }
};

/// @brief Resolve a path against a description.
///
/// Rules: an array field must be indexed before descending into it
/// (`points.x` is an error, `points[2].x` is fine); an index on a non-array is
/// an error; an index at or beyond a Fixed size or a Bounded maximum is an
/// error; a scalar cannot be descended into. The empty path is an error here
/// (it names the root, which is not a field).
bool resolve_field_path(
  const MessageDescription & root,
  const FieldPath & path,
  ResolvedField & out_resolved,
  std::string & out_error);

bool resolve_field_path(
  const MessageDescription & root,
  std::string_view path,
  ResolvedField & out_resolved,
  std::string & out_error);

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

enum class WalkControl
{
  Continue,      ///< Visit children (if any), then siblings.
  SkipChildren,  ///< Do not descend into this field; continue with siblings.
  Stop           ///< Abort the whole walk.
};

using FieldVisitor =
  std::function<WalkControl(const FieldPath & path, const FieldDescription & field, std::size_t depth)>;

/// @brief Depth-first, declaration-order walk over every field.
///
/// Descends into non-array Message fields. Array fields are visited but not
/// descended: an array of messages is one thing (a table, a list), and a
/// visitor that wants the element structure has `field.message` at hand.
/// @return false if the visitor stopped the walk.
bool walk_fields(const MessageDescription & root, const FieldVisitor & visitor);

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

/// @brief Read the value at @p path. nullptr when any segment is missing, an
/// index is out of range, or a segment's kind does not match (naming into an
/// array, indexing into an object). The empty path yields @p root.
const UiValue * get_path(const UiValue & root, const FieldPath & path);
UiValue * get_path(UiValue & root, const FieldPath & path);

/// @brief Write @p value at @p path, creating what is missing on the way:
/// a Null becomes an Object (or an Array when the segment is indexed) and an
/// Array grows with Nulls up to the index. Fails, leaving @p root untouched,
/// when an existing value is of the wrong kind for a segment. The empty path
/// replaces @p root.
bool set_path(UiValue & root, const FieldPath & path, UiValue value, std::string & out_error);

/// @brief Value with every field at its default: the IDL default when there is
/// one, else false / 0 / 0.0 / "" / a nested default; a Fixed array is filled
/// with element defaults, Bounded/Unbounded arrays start empty.
UiValue make_default_value(const MessageDescription & description);
UiValue make_default_value(const FieldDescription & field);

struct ValueProblem
{
  std::string path;
  std::string message;
};

struct ValidateValueOptions
{
  /// Report fields present in the description but absent from the value.
  bool require_all_fields = false;
  /// Report keys present in the value that the description does not declare.
  bool reject_unknown_fields = true;
};

/// @brief Check a value against a description: kinds, integer ranges, fixed /
/// bounded array lengths, string bounds, nested messages. An empty result
/// means qar_ros_ui can construct the message without a conversion surprise.
std::vector<ValueProblem> validate_value(
  const MessageDescription & description,
  const UiValue & value,
  const ValidateValueOptions & options = {});

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__TYPE_DESCRIPTION_HPP_
