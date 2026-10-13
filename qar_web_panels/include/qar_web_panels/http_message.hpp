// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_WEB_PANELS__HTTP_MESSAGE_HPP_
#define QAR_WEB_PANELS__HTTP_MESSAGE_HPP_

/// @file http_message.hpp
/// @brief HTTP/1.1 request parsing and response serialization, as pure C++.
///
/// Deliberately Qt-free and socket-free: the parser sees a byte buffer and
/// answers "complete / incomplete / invalid (and which status to send)", the
/// serializer turns an HttpResponse into bytes. Everything that can go wrong
/// with a request is decided here, in unit-testable functions; http_server.cpp
/// only moves bytes between a socket and these two calls.
///
/// The server serves generated content from memory only, so a request path is
/// never mapped onto a filesystem — but a traversal-shaped path (`..`, `\`,
/// control characters, bad percent-escapes) is still rejected with 400 rather
/// than looked up and 404'd, so a probe is recognisable in the diagnostics.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qar_web_panels
{

struct HttpHeader
{
  std::string name;   ///< As sent; compare with @ref ascii_iequals.
  std::string value;  ///< Surrounding whitespace stripped.
};

struct HttpRequest
{
  std::string method;  ///< Token as sent (`GET`, `HEAD`, ...); methods are case-sensitive.
  std::string target;  ///< The raw request-target.
  std::string path;    ///< Percent-decoded origin-form path, always starting with '/'.
  std::string query;   ///< Raw text after '?', without the '?'. Not decoded.
  int version_major = 1;
  int version_minor = 1;
  std::vector<HttpHeader> headers;
  std::optional<std::uint64_t> content_length;
  bool has_transfer_encoding = false;
  std::size_t head_bytes = 0;  ///< Bytes of the head including the blank line.

  const std::string * find_header(std::string_view name) const;
  std::string header(std::string_view name) const;  ///< Empty when absent.
  /// True when the comma-separated header lists @p token (case-insensitively).
  bool header_has_token(std::string_view name, std::string_view token) const;
  bool is_http_1_1() const { return version_major == 1 && version_minor == 1; }
  /// HTTP/1.1 persists unless `Connection: close`; HTTP/1.0 persists only
  /// with an explicit `Connection: keep-alive`.
  bool keep_alive() const;
  /// `Upgrade: websocket` + `Connection: upgrade`: the RFC 6455 opening handshake.
  bool is_websocket_upgrade() const;
};

enum class HttpParseStatus
{
  Complete,    ///< The request is filled; `head_bytes` says how much to consume.
  Incomplete,  ///< More bytes needed, and the buffer is still within limits.
  Invalid      ///< Answer with `http_status` and close the connection.
};

struct HttpParseLimits
{
  std::size_t max_head_bytes = 16 * 1024;  ///< Request line + headers.
  std::size_t max_target_bytes = 2048;
  std::size_t max_headers = 100;
};

struct HttpParseResult
{
  HttpParseStatus status = HttpParseStatus::Incomplete;
  int http_status = 0;  ///< Invalid only: 400, 414, 431 or 505.
  std::string reason;   ///< Invalid only: one-line diagnostic.
};

HttpParseResult parse_http_request(
  std::string_view bytes, const HttpParseLimits & limits, HttpRequest & out_request);

/// @brief Split and percent-decode a request-target into path + query.
/// @return false for anything traversal-shaped: a bad or non-printable escape,
///         a `.` or `..` segment, a backslash, a byte outside visible ASCII.
bool decode_request_target(
  std::string_view target, std::string & out_path, std::string & out_query);

/// @brief Case-insensitive ASCII equality (header names, tokens, schemes).
bool ascii_iequals(std::string_view a, std::string_view b);

/// @brief A `Host` value safe to reflect into a page: `host[:port]` or
/// `[v6]:port`, made of `[A-Za-z0-9.\-_:\[\]]`, 1..255 bytes.
bool is_acceptable_host_header(std::string_view host);

/// @brief The `host[:port]` part of an `Origin` value (`http://a.b:8080` ->
/// `a.b:8080`). Empty for `null` or anything that is not `scheme://authority`.
std::string origin_authority(std::string_view origin);

struct HttpResponse
{
  int status = 200;
  std::string content_type;  ///< Omitted when empty.
  std::string body;
  std::vector<HttpHeader> extra_headers;
  bool close = false;  ///< Force `Connection: close` whatever the request asked.

  static HttpResponse text(int status, std::string body);
  static HttpResponse html(std::string body);
  static HttpResponse not_found();
};

const char * http_reason_phrase(int status);

/// @brief IMF-fixdate (`Sun, 06 Nov 1994 08:49:37 GMT`) for @p unix_seconds.
/// Formatted by hand so the process locale cannot change the names.
std::string http_date(std::int64_t unix_seconds);
std::string http_date_now();

/// @brief Status line + headers + body. `head_only` drops the body but keeps
/// Content-Length: a HEAD response describes the entity it does not send.
std::string serialize_http_response(
  const HttpResponse & response, bool head_only, bool keep_alive, const std::string & date);

}  // namespace qar_web_panels

#endif  // QAR_WEB_PANELS__HTTP_MESSAGE_HPP_
