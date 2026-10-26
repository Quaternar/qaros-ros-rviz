// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

const char * to_string(Status status)
{
  switch (status) {
    case Status::Ok: return "Ok";
    case Status::InvalidArgument: return "InvalidArgument";
    case Status::InvalidState: return "InvalidState";
    case Status::NotFound: return "NotFound";
    case Status::TypeSupportUnavailable: return "TypeSupportUnavailable";
    case Status::ConversionFailed: return "ConversionFailed";
    case Status::SerializationFailed: return "SerializationFailed";
    case Status::RclError: return "RclError";
    case Status::Timeout: return "Timeout";
    case Status::Rejected: return "Rejected";
    case Status::Unsupported: return "Unsupported";
  }
  return "Unknown";
}

const char * to_string(DiagnosticSeverity severity)
{
  switch (severity) {
    case DiagnosticSeverity::Info: return "Info";
    case DiagnosticSeverity::Warning: return "Warning";
    case DiagnosticSeverity::Error: return "Error";
  }
  return "Unknown";
}

}  // namespace qar_ros_ui
