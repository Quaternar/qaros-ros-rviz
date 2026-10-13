// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/type_description.hpp"

#include <cmath>
#include <utility>

namespace qar_ui_model
{

// ---------------------------------------------------------------------------
// Field kinds
// ---------------------------------------------------------------------------

const char *
to_string(FieldKind kind)
{
  switch (kind) {
    case FieldKind::Bool:
      return "bool";
    case FieldKind::Byte:
      return "byte";
    case FieldKind::Char:
      return "char";
    case FieldKind::WChar:
      return "wchar";
    case FieldKind::Int8:
      return "int8";
    case FieldKind::UInt8:
      return "uint8";
    case FieldKind::Int16:
      return "int16";
    case FieldKind::UInt16:
      return "uint16";
    case FieldKind::Int32:
      return "int32";
    case FieldKind::UInt32:
      return "uint32";
    case FieldKind::Int64:
      return "int64";
    case FieldKind::UInt64:
      return "uint64";
    case FieldKind::Float32:
      return "float32";
    case FieldKind::Float64:
      return "float64";
    case FieldKind::LongDouble:
      return "long double";
    case FieldKind::String:
      return "string";
    case FieldKind::WString:
      return "wstring";
    case FieldKind::Message:
      return "message";
  }
  return "unknown";
}

bool
field_kind_from_ros_name(std::string_view name, FieldKind & out_kind)
{
  struct Entry
  {
    const char * name;
    FieldKind kind;
  };
  static const Entry kEntries[] = {
    {"bool", FieldKind::Bool}, {"boolean", FieldKind::Bool},
    {"byte", FieldKind::Byte}, {"octet", FieldKind::Byte},
    {"char", FieldKind::Char}, {"wchar", FieldKind::WChar},
    {"int8", FieldKind::Int8}, {"uint8", FieldKind::UInt8},
    {"int16", FieldKind::Int16}, {"uint16", FieldKind::UInt16},
    {"int32", FieldKind::Int32}, {"uint32", FieldKind::UInt32},
    {"int64", FieldKind::Int64}, {"uint64", FieldKind::UInt64},
    {"float32", FieldKind::Float32}, {"float", FieldKind::Float32},
    {"float64", FieldKind::Float64}, {"double", FieldKind::Float64},
    {"long double", FieldKind::LongDouble},
    {"string", FieldKind::String}, {"wstring", FieldKind::WString},
  };
  for (const Entry & entry : kEntries) {
    if (name == entry.name) {
      out_kind = entry.kind;
      return true;
    }
  }
  return false;
}

bool
is_boolean(FieldKind kind)
{
  return kind == FieldKind::Bool;
}

bool
is_integer(FieldKind kind)
{
  switch (kind) {
    case FieldKind::Byte:
    case FieldKind::Char:
    case FieldKind::WChar:
    case FieldKind::Int8:
    case FieldKind::UInt8:
    case FieldKind::Int16:
    case FieldKind::UInt16:
    case FieldKind::Int32:
    case FieldKind::UInt32:
    case FieldKind::Int64:
    case FieldKind::UInt64:
      return true;
    default:
      return false;
  }
}

bool
is_signed_integer(FieldKind kind)
{
  switch (kind) {
    case FieldKind::Int8:
    case FieldKind::Int16:
    case FieldKind::Int32:
    case FieldKind::Int64:
      return true;
    default:
      return false;
  }
}

bool
is_floating(FieldKind kind)
{
  return kind == FieldKind::Float32 || kind == FieldKind::Float64 || kind == FieldKind::LongDouble;
}

bool
is_numeric(FieldKind kind)
{
  return is_integer(kind) || is_floating(kind);
}

bool
is_string(FieldKind kind)
{
  return kind == FieldKind::String || kind == FieldKind::WString;
}

bool
is_primitive(FieldKind kind)
{
  return kind != FieldKind::Message;
}

std::optional<NumericBounds>
numeric_bounds(FieldKind kind)
{
  switch (kind) {
    case FieldKind::Byte:
    case FieldKind::Char:
    case FieldKind::UInt8:
      return NumericBounds{0.0, 255.0};
    case FieldKind::WChar:
    case FieldKind::UInt16:
      return NumericBounds{0.0, 65535.0};
    case FieldKind::Int8:
      return NumericBounds{-128.0, 127.0};
    case FieldKind::Int16:
      return NumericBounds{-32768.0, 32767.0};
    case FieldKind::Int32:
      return NumericBounds{-2147483648.0, 2147483647.0};
    case FieldKind::UInt32:
      return NumericBounds{0.0, 4294967295.0};
    case FieldKind::Int64:
      return NumericBounds{-9223372036854775808.0, 9223372036854775808.0};
    case FieldKind::UInt64:
      return NumericBounds{0.0, 18446744073709551616.0};
    default:
      return std::nullopt;
  }
}

const char *
to_string(ArrayKind kind)
{
  switch (kind) {
    case ArrayKind::None:
      return "none";
    case ArrayKind::Fixed:
      return "fixed";
    case ArrayKind::Bounded:
      return "bounded";
    case ArrayKind::Unbounded:
      return "unbounded";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// FieldDescription
// ---------------------------------------------------------------------------

FieldDescription
FieldDescription::primitive(std::string name, FieldKind kind)
{
  FieldDescription field;
  field.name = std::move(name);
  field.kind = kind == FieldKind::Message ? FieldKind::Int32 : kind;
  field.type_name = to_string(field.kind);
  return field;
}

FieldDescription
FieldDescription::nested(std::string name, MessageDescriptionPtr type)
{
  FieldDescription field;
  field.name = std::move(name);
  field.kind = FieldKind::Message;
  if (type) {
    field.type_name = type->type_name;
  }
  field.message = std::move(type);
  return field;
}

FieldDescription &
FieldDescription::fixed_array(std::size_t size)
{
  array_kind = ArrayKind::Fixed;
  array_size = size;
  return *this;
}

FieldDescription &
FieldDescription::bounded_array(std::size_t max_size)
{
  array_kind = ArrayKind::Bounded;
  array_size = max_size;
  return *this;
}

FieldDescription &
FieldDescription::unbounded_array()
{
  array_kind = ArrayKind::Unbounded;
  array_size = 0;
  return *this;
}

FieldDescription &
FieldDescription::string_bound(std::size_t upper_bound)
{
  string_upper_bound = upper_bound;
  return *this;
}

FieldDescription &
FieldDescription::with_default(UiValue value)
{
  default_value = std::move(value);
  return *this;
}

FieldDescription &
FieldDescription::with_label(std::string text)
{
  label = std::move(text);
  return *this;
}

FieldDescription &
FieldDescription::with_unit(std::string text)
{
  unit = std::move(text);
  return *this;
}

FieldDescription &
FieldDescription::with_description(std::string text)
{
  description = std::move(text);
  return *this;
}

FieldDescription &
FieldDescription::with_range(double min_value, double max_value)
{
  min = min_value;
  max = max_value;
  return *this;
}

FieldDescription &
FieldDescription::with_step(double step_value)
{
  step = step_value;
  return *this;
}

std::string
FieldDescription::display_label() const
{
  return label.empty() ? humanize_identifier(name) : label;
}

// ---------------------------------------------------------------------------
// MessageDescription + builder
// ---------------------------------------------------------------------------

const FieldDescription *
MessageDescription::find_field(std::string_view name) const
{
  for (const FieldDescription & field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

const ConstantDescription *
MessageDescription::find_constant(std::string_view name) const
{
  for (const ConstantDescription & constant : constants) {
    if (constant.name == name) {
      return &constant;
    }
  }
  return nullptr;
}

MessageDescriptionBuilder::MessageDescriptionBuilder(std::string type_name)
{
  description_.type_name = std::move(type_name);
}

MessageDescriptionBuilder &
MessageDescriptionBuilder::description(std::string text)
{
  description_.description = std::move(text);
  return *this;
}

MessageDescriptionBuilder &
MessageDescriptionBuilder::field(FieldDescription field)
{
  description_.fields.push_back(std::move(field));
  return *this;
}

MessageDescriptionBuilder &
MessageDescriptionBuilder::constant(
  std::string name, FieldKind kind, UiValue value, std::string description)
{
  description_.constants.push_back(
    ConstantDescription{std::move(name), kind, std::move(value), std::move(description)});
  return *this;
}

MessageDescriptionPtr
MessageDescriptionBuilder::build()
{
  return std::make_shared<const MessageDescription>(description_);
}

MessageDescription
MessageDescriptionBuilder::build_value() const
{
  return description_;
}

namespace
{

bool
is_identifier(std::string_view text)
{
  if (text.empty()) {
    return false;
  }
  const auto is_alpha = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  const auto is_digit = [](char c) {
    return c >= '0' && c <= '9';
  };
  if (!is_alpha(text[0])) {
    return false;
  }
  for (char c : text) {
    if (!is_alpha(c) && !is_digit(c)) {
      return false;
    }
  }
  return true;
}

void
validate_description_impl(
  const MessageDescription & message,
  const std::string & prefix,
  std::size_t depth,
  std::vector<DescriptionProblem> & problems)
{
  // A description cannot be recursive in ROS, but a hand-built one could be
  // made so by mistake; refuse to loop forever.
  if (depth > 64) {
    problems.push_back({prefix, "nesting deeper than 64 levels (recursive description?)"});
    return;
  }
  for (std::size_t i = 0; i < message.fields.size(); ++i) {
    const FieldDescription & field = message.fields[i];
    const std::string path = prefix.empty() ? field.name : prefix + "." + field.name;
    if (!is_identifier(field.name)) {
      problems.push_back({path, "field name '" + field.name + "' is not an identifier"});
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (message.fields[j].name == field.name) {
        problems.push_back({path, "duplicate field name '" + field.name + "'"});
        break;
      }
    }
    if (field.array_kind == ArrayKind::Fixed && field.array_size == 0) {
      problems.push_back({path, "fixed array of size 0"});
    }
    if (field.array_kind == ArrayKind::Bounded && field.array_size == 0) {
      problems.push_back({path, "bounded array with bound 0"});
    }
    if (field.string_upper_bound != 0 && !is_string(field.kind)) {
      problems.push_back({path, "string_upper_bound set on a non-string field"});
    }
    if (field.min && field.max && *field.min > *field.max) {
      problems.push_back({path, "min is greater than max"});
    }
    if (field.step && !(*field.step > 0.0)) {
      problems.push_back({path, "step must be positive"});
    }
    if (field.kind == FieldKind::Message) {
      if (!field.message) {
        problems.push_back({path, "message field has no element type"});
      } else {
        validate_description_impl(*field.message, path, depth + 1, problems);
      }
    } else if (field.message) {
      problems.push_back({path, "primitive field carries a message type"});
    }
  }
  for (std::size_t i = 0; i < message.constants.size(); ++i) {
    const ConstantDescription & constant = message.constants[i];
    const std::string path = prefix.empty() ? constant.name : prefix + "." + constant.name;
    if (!is_identifier(constant.name)) {
      problems.push_back({path, "constant name '" + constant.name + "' is not an identifier"});
    }
    if (constant.kind == FieldKind::Message) {
      problems.push_back({path, "a constant cannot be of message kind"});
    }
  }
}

}  // namespace

std::vector<DescriptionProblem>
validate_description(const MessageDescription & root)
{
  std::vector<DescriptionProblem> problems;
  validate_description_impl(root, "", 0, problems);
  return problems;
}

// ---------------------------------------------------------------------------
// Type names
// ---------------------------------------------------------------------------

bool
split_type_name(std::string_view type_name, TypeNameParts & out_parts)
{
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= type_name.size()) {
    const std::size_t slash = type_name.find('/', start);
    const std::string_view part =
      type_name.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
    parts.push_back(part);
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1;
  }
  for (std::string_view part : parts) {
    if (part.empty()) {
      return false;
    }
  }
  if (parts.size() == 3) {
    if (parts[1] != "msg" && parts[1] != "srv" && parts[1] != "action") {
      return false;
    }
    out_parts.package = std::string(parts[0]);
    out_parts.interface = std::string(parts[1]);
    out_parts.name = std::string(parts[2]);
    return true;
  }
  if (parts.size() == 2) {
    out_parts.package = std::string(parts[0]);
    out_parts.interface = "msg";
    out_parts.name = std::string(parts[1]);
    return true;
  }
  return false;
}

std::string
normalize_type_name(std::string_view type_name)
{
  TypeNameParts parts;
  if (!split_type_name(type_name, parts)) {
    return std::string(type_name);
  }
  return parts.package + "/" + parts.interface + "/" + parts.name;
}

std::string
humanize_identifier(std::string_view identifier)
{
  std::string out;
  out.reserve(identifier.size() + 4);
  char previous = '\0';
  for (std::size_t i = 0; i < identifier.size(); ++i) {
    const char c = identifier[i];
    if (c == '_' || c == '-' || c == ' ') {
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
      previous = c;
      continue;
    }
    const bool upper = c >= 'A' && c <= 'Z';
    const bool previous_lower =
      (previous >= 'a' && previous <= 'z') || (previous >= '0' && previous <= '9');
    if (out.empty()) {
      out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    } else if (upper && previous_lower) {
      out.push_back(' ');  // camelCase boundary
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
    previous = c;
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

bool
FieldPath::parse(std::string_view text, FieldPath & out_path, std::string & out_error)
{
  FieldPath path;
  if (text.empty()) {
    out_path = std::move(path);
    out_error.clear();
    return true;
  }
  std::size_t pos = 0;
  while (true) {
    const std::size_t name_start = pos;
    while (pos < text.size() && text[pos] != '.' && text[pos] != '[') {
      ++pos;
    }
    const std::string_view name = text.substr(name_start, pos - name_start);
    if (!is_identifier(name)) {
      out_error = "path '" + std::string(text) + "': segment '" + std::string(name) +
        "' at offset " + std::to_string(name_start) + " is not an identifier";
      return false;
    }
    PathSegment segment;
    segment.name = std::string(name);
    if (pos < text.size() && text[pos] == '[') {
      ++pos;
      const std::size_t digits_start = pos;
      std::size_t index = 0;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        if (index > 100000000) {
          out_error = "path '" + std::string(text) + "': index too large";
          return false;
        }
        index = index * 10 + static_cast<std::size_t>(text[pos] - '0');
        ++pos;
      }
      if (pos == digits_start || pos >= text.size() || text[pos] != ']') {
        out_error = "path '" + std::string(text) + "': expected '[digits]' after '" +
          segment.name + "'";
        return false;
      }
      ++pos;
      segment.index = index;
    }
    path.segments.push_back(std::move(segment));
    if (pos >= text.size()) {
      break;
    }
    if (text[pos] != '.') {
      out_error = "path '" + std::string(text) + "': unexpected '" + std::string(1, text[pos]) +
        "' at offset " + std::to_string(pos);
      return false;
    }
    ++pos;
    if (pos >= text.size()) {
      out_error = "path '" + std::string(text) + "': trailing '.'";
      return false;
    }
  }
  out_path = std::move(path);
  out_error.clear();
  return true;
}

std::string
FieldPath::to_string() const
{
  std::string out;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) {
      out.push_back('.');
    }
    out += segments[i].name;
    if (segments[i].index) {
      out.push_back('[');
      out += std::to_string(*segments[i].index);
      out.push_back(']');
    }
  }
  return out;
}

FieldPath
FieldPath::appended(PathSegment segment) const
{
  FieldPath copy = *this;
  copy.segments.push_back(std::move(segment));
  return copy;
}

bool
resolve_field_path(
  const MessageDescription & root,
  const FieldPath & path,
  ResolvedField & out_resolved,
  std::string & out_error)
{
  if (path.empty()) {
    out_error = "empty path names the root, not a field";
    return false;
  }
  const MessageDescription * current = &root;
  ResolvedField resolved;
  FieldPath so_far;
  for (std::size_t i = 0; i < path.segments.size(); ++i) {
    const PathSegment & segment = path.segments[i];
    so_far.segments.push_back(segment);
    const std::string where = "'" + so_far.to_string() + "'";
    if (current == nullptr) {
      out_error = where + ": cannot descend into a primitive";
      return false;
    }
    const FieldDescription * field = current->find_field(segment.name);
    if (field == nullptr) {
      out_error = where + ": no field '" + segment.name + "' in " + current->type_name;
      return false;
    }
    resolved.field = field;
    resolved.owner = current;
    resolved.indexed = segment.index.has_value();
    if (segment.index) {
      if (!field->is_array()) {
        out_error = where + ": '" + segment.name + "' is not an array";
        return false;
      }
      if ((field->array_kind == ArrayKind::Fixed || field->array_kind == ArrayKind::Bounded) &&
        *segment.index >= field->array_size)
      {
        out_error = where + ": index " + std::to_string(*segment.index) +
          " is out of range for " +
          (field->array_kind == ArrayKind::Fixed ? "size " : "bound ") +
          std::to_string(field->array_size);
        return false;
      }
    }
    const bool last = i + 1 == path.segments.size();
    if (!last) {
      if (field->is_array() && !segment.index) {
        out_error = where + ": '" + segment.name + "' is an array; index it with [i]";
        return false;
      }
      if (field->kind != FieldKind::Message) {
        out_error = where + ": '" + segment.name + "' is a " + to_string(field->kind) +
          ", not a message";
        return false;
      }
      current = field->message.get();
    }
  }
  out_resolved = resolved;
  out_error.clear();
  return true;
}

bool
resolve_field_path(
  const MessageDescription & root,
  std::string_view path,
  ResolvedField & out_resolved,
  std::string & out_error)
{
  FieldPath parsed;
  if (!FieldPath::parse(path, parsed, out_error)) {
    return false;
  }
  return resolve_field_path(root, parsed, out_resolved, out_error);
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

namespace
{

bool
walk_fields_impl(
  const MessageDescription & message,
  const FieldPath & prefix,
  std::size_t depth,
  const FieldVisitor & visitor)
{
  for (const FieldDescription & field : message.fields) {
    const FieldPath path = prefix.appended(PathSegment{field.name, std::nullopt});
    const WalkControl control = visitor(path, field, depth);
    if (control == WalkControl::Stop) {
      return false;
    }
    if (control == WalkControl::SkipChildren) {
      continue;
    }
    if (field.kind == FieldKind::Message && !field.is_array() && field.message && depth < 64) {
      if (!walk_fields_impl(*field.message, path, depth + 1, visitor)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool
walk_fields(const MessageDescription & root, const FieldVisitor & visitor)
{
  return walk_fields_impl(root, FieldPath{}, 0, visitor);
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

const UiValue *
get_path(const UiValue & root, const FieldPath & path)
{
  const UiValue * current = &root;
  for (const PathSegment & segment : path.segments) {
    current = current->find(segment.name);
    if (current == nullptr) {
      return nullptr;
    }
    if (segment.index) {
      current = current->at(*segment.index);
      if (current == nullptr) {
        return nullptr;
      }
    }
  }
  return current;
}

UiValue *
get_path(UiValue & root, const FieldPath & path)
{
  return const_cast<UiValue *>(get_path(static_cast<const UiValue &>(root), path));
}

bool
set_path(UiValue & root, const FieldPath & path, UiValue value, std::string & out_error)
{
  if (path.empty()) {
    root = std::move(value);
    out_error.clear();
    return true;
  }
  // Check first, so a failure leaves the document untouched.
  {
    const UiValue * current = &root;
    for (std::size_t i = 0; i < path.segments.size() && current != nullptr; ++i) {
      const PathSegment & segment = path.segments[i];
      if (!current->is_object() && !current->is_null()) {
        out_error = "'" + path.to_string() + "': '" +
          (i == 0 ? std::string("<root>") : path.segments[i - 1].name) + "' is a " +
          to_string(current->kind()) + ", not an object";
        return false;
      }
      const UiValue * next = current->find(segment.name);
      if (next == nullptr) {
        break;  // Everything from here on is created fresh.
      }
      if (segment.index) {
        if (!next->is_array() && !next->is_null()) {
          out_error = "'" + path.to_string() + "': '" + segment.name + "' is a " +
            to_string(next->kind()) + ", not an array";
          return false;
        }
        current = next->at(*segment.index);  // nullptr when growing: fine
      } else {
        current = next;
      }
    }
  }

  UiValue * current = &root;
  for (std::size_t i = 0; i < path.segments.size(); ++i) {
    const PathSegment & segment = path.segments[i];
    const bool last = i + 1 == path.segments.size();
    if (current->is_null()) {
      *current = UiValue::object();
    }
    UiValue * next = current->find(segment.name);
    if (next == nullptr) {
      current->set(segment.name, UiValue());
      next = current->find(segment.name);
    }
    if (segment.index) {
      if (next->is_null()) {
        *next = UiValue::array();
      }
      UiArray & elements = next->elements();
      if (elements.size() <= *segment.index) {
        elements.resize(*segment.index + 1);
      }
      next = &elements[*segment.index];
    }
    if (last) {
      *next = std::move(value);
      break;
    }
    current = next;
  }
  out_error.clear();
  return true;
}

UiValue
make_default_value(const FieldDescription & field)
{
  const auto element_default = [&field]() -> UiValue {
    switch (field.kind) {
      case FieldKind::Bool:
        return UiValue(false);
      case FieldKind::Float32:
      case FieldKind::Float64:
      case FieldKind::LongDouble:
        return UiValue(0.0);
      case FieldKind::String:
      case FieldKind::WString:
        return UiValue("");
      case FieldKind::Message:
        return field.message ? make_default_value(*field.message) : UiValue::object();
      default:
        return UiValue(std::int64_t{0});
    }
  };

  if (!field.is_array()) {
    return field.default_value.is_null() ? element_default() : field.default_value;
  }
  if (!field.default_value.is_null()) {
    return field.default_value;
  }
  UiValue array = UiValue::array();
  if (field.array_kind == ArrayKind::Fixed) {
    for (std::size_t i = 0; i < field.array_size; ++i) {
      array.push_back(element_default());
    }
  }
  return array;
}

UiValue
make_default_value(const MessageDescription & description)
{
  UiValue object = UiValue::object();
  for (const FieldDescription & field : description.fields) {
    object.set(field.name, make_default_value(field));
  }
  return object;
}

namespace
{

void
validate_element(
  const FieldDescription & field,
  const UiValue & value,
  const std::string & path,
  const ValidateValueOptions & options,
  std::size_t depth,
  std::vector<ValueProblem> & problems);

void
validate_message_value(
  const MessageDescription & description,
  const UiValue & value,
  const std::string & prefix,
  const ValidateValueOptions & options,
  std::size_t depth,
  std::vector<ValueProblem> & problems)
{
  if (!value.is_object()) {
    problems.push_back({prefix, std::string("expected an object for ") + description.type_name +
      ", got " + to_string(value.kind())});
    return;
  }
  if (depth > 64) {
    problems.push_back({prefix, "nesting deeper than 64 levels"});
    return;
  }
  for (const FieldDescription & field : description.fields) {
    const std::string path = prefix.empty() ? field.name : prefix + "." + field.name;
    const UiValue * member = value.find(field.name);
    if (member == nullptr) {
      if (options.require_all_fields) {
        problems.push_back({path, "missing field"});
      }
      continue;
    }
    if (!field.is_array()) {
      validate_element(field, *member, path, options, depth, problems);
      continue;
    }
    if (!member->is_array()) {
      problems.push_back({path, std::string("expected an array, got ") + to_string(member->kind())});
      continue;
    }
    const std::size_t count = member->size();
    if (field.array_kind == ArrayKind::Fixed && count != field.array_size) {
      problems.push_back({path, "fixed array of size " + std::to_string(field.array_size) +
        " has " + std::to_string(count) + " elements"});
    } else if (field.array_kind == ArrayKind::Bounded && count > field.array_size) {
      problems.push_back({path, "bounded array (<=" + std::to_string(field.array_size) +
        ") has " + std::to_string(count) + " elements"});
    }
    for (std::size_t i = 0; i < count; ++i) {
      validate_element(
        field, *member->at(i), path + "[" + std::to_string(i) + "]", options, depth, problems);
    }
  }
  if (options.reject_unknown_fields) {
    for (const UiObjectEntry & entry : value.entries()) {
      if (description.find_field(entry.key) == nullptr) {
        const std::string path = prefix.empty() ? entry.key : prefix + "." + entry.key;
        problems.push_back({path, "unknown field '" + entry.key + "' in " + description.type_name});
      }
    }
  }
}

void
validate_element(
  const FieldDescription & field,
  const UiValue & value,
  const std::string & path,
  const ValidateValueOptions & options,
  std::size_t depth,
  std::vector<ValueProblem> & problems)
{
  const std::string expected = to_string(field.kind);
  const std::string got = to_string(value.kind());
  if (field.kind == FieldKind::Message) {
    if (field.message) {
      validate_message_value(*field.message, value, path, options, depth + 1, problems);
    }
    return;
  }
  if (is_boolean(field.kind)) {
    if (!value.is_bool()) {
      problems.push_back({path, "expected bool, got " + got});
    }
    return;
  }
  if (is_string(field.kind)) {
    if (!value.is_string()) {
      problems.push_back({path, "expected " + expected + ", got " + got});
      return;
    }
    if (field.string_upper_bound != 0 && value.as_string().size() > field.string_upper_bound) {
      problems.push_back({path, "string longer than its bound of " +
        std::to_string(field.string_upper_bound) + " bytes"});
    }
    return;
  }
  if (!value.is_number()) {
    problems.push_back({path, "expected " + expected + ", got " + got});
    return;
  }
  if (is_floating(field.kind)) {
    return;
  }
  // Integer kinds: a Double is acceptable only when integral (JavaScript has
  // no integer type, so 5 and 5.0 are the same message), and always in range.
  const double as_double = value.as_double();
  if (value.is_double() && (!std::isfinite(as_double) || as_double != std::floor(as_double))) {
    problems.push_back({path, "expected " + expected + ", got a non-integral double"});
    return;
  }
  const std::optional<NumericBounds> bounds = numeric_bounds(field.kind);
  if (bounds) {
    bool in_range = true;
    if (value.is_int()) {
      const std::int64_t v = value.as_int();
      if (field.kind == FieldKind::UInt64) {
        in_range = v >= 0;
      } else if (field.kind != FieldKind::Int64) {
        in_range = static_cast<double>(v) >= bounds->min && static_cast<double>(v) <= bounds->max;
      }
    } else {
      in_range = as_double >= bounds->min && as_double < bounds->max + 1.0;
    }
    if (!in_range) {
      problems.push_back({path, "value out of range for " + expected});
    }
  }
}

}  // namespace

std::vector<ValueProblem>
validate_value(
  const MessageDescription & description,
  const UiValue & value,
  const ValidateValueOptions & options)
{
  std::vector<ValueProblem> problems;
  validate_message_value(description, value, "", options, 0, problems);
  return problems;
}

}  // namespace qar_ui_model
