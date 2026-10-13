// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_web_panels/http_message.hpp"

#include <chrono>
#include <cstdio>

namespace qar_web_panels
{
namespace
{

constexpr std::size_t npos = std::string_view::npos;

bool
is_tchar(unsigned char c)
{
  if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
    return true;
  }
  switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
    case '-': case '.': case '^': case '_': case '`': case '|': case '~':
      return true;
    default:
      return false;
  }
}

bool
is_token(std::string_view s)
{
  if (s.empty()) {
    return false;
  }
  for (const char c : s) {
    if (!is_tchar(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

std::string_view
trim_ows(std::string_view s)
{
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }
  return s;
}

int
hex_value(char c)
{
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  return -1;
}

char
ascii_lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

HttpParseResult
invalid(int status, std::string reason)
{
  HttpParseResult r;
  r.status = HttpParseStatus::Invalid;
  r.http_status = status;
  r.reason = std::move(reason);
  return r;
}

}  // namespace

bool
ascii_iequals(std::string_view a, std::string_view b)
{
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower(a[i]) != ascii_lower(b[i])) {
      return false;
    }
  }
  return true;
}

bool
is_acceptable_host_header(std::string_view host)
{
  if (host.empty() || host.size() > 255) {
    return false;
  }
  for (const char c : host) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
      c == '.' || c == '-' || c == '_' || c == ':' || c == '[' || c == ']';
    if (!ok) {
      return false;
    }
  }
  return true;
}

std::string
origin_authority(std::string_view origin)
{
  const std::size_t sep = origin.find("://");
  if (sep == npos || sep == 0) {
    return {};
  }
  std::string_view rest = origin.substr(sep + 3);
  const std::size_t slash = rest.find('/');
  if (slash != npos) {
    rest = rest.substr(0, slash);
  }
  if (!is_acceptable_host_header(rest)) {
    return {};
  }
  return std::string(rest);
}

const std::string *
HttpRequest::find_header(std::string_view name) const
{
  for (const auto & h : headers) {
    if (ascii_iequals(h.name, name)) {
      return &h.value;
    }
  }
  return nullptr;
}

std::string
HttpRequest::header(std::string_view name) const
{
  const std::string * value = find_header(name);
  return value != nullptr ? *value : std::string();
}

bool
HttpRequest::header_has_token(std::string_view name, std::string_view token) const
{
  for (const auto & h : headers) {
    if (!ascii_iequals(h.name, name)) {
      continue;
    }
    std::string_view rest = h.value;
    while (true) {
      const std::size_t comma = rest.find(',');
      if (ascii_iequals(trim_ows(rest.substr(0, comma)), token)) {
        return true;
      }
      if (comma == npos) {
        break;
      }
      rest.remove_prefix(comma + 1);
    }
  }
  return false;
}

bool
HttpRequest::keep_alive() const
{
  if (header_has_token("Connection", "close")) {
    return false;
  }
  if (is_http_1_1()) {
    return true;
  }
  return header_has_token("Connection", "keep-alive");
}

bool
HttpRequest::is_websocket_upgrade() const
{
  return header_has_token("Upgrade", "websocket") && header_has_token("Connection", "upgrade");
}

bool
decode_request_target(std::string_view target, std::string & out_path, std::string & out_query)
{
  out_path.clear();
  out_query.clear();

  for (const char ch : target) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c <= 0x20 || c >= 0x7F || c == '#') {
      return false;
    }
  }

  // absolute-form (RFC 7230 section 5.3.2): a server must accept it, so drop the
  // scheme and authority and continue with the path.
  std::string_view t = target;
  for (const std::string_view scheme : {"http://", "https://", "ws://", "wss://"}) {
    if (t.size() >= scheme.size() && ascii_iequals(t.substr(0, scheme.size()), scheme)) {
      t.remove_prefix(scheme.size());
      const std::size_t slash = t.find('/');
      t = (slash == npos) ? std::string_view("/") : t.substr(slash);
      break;
    }
  }
  if (t.empty() || t.front() != '/') {
    return false;
  }

  const std::size_t q = t.find('?');
  const std::string_view raw_path = t.substr(0, q);
  if (q != npos) {
    out_query.assign(t.substr(q + 1));
  }

  std::string decoded;
  decoded.reserve(raw_path.size());
  for (std::size_t i = 0; i < raw_path.size(); ++i) {
    const char c = raw_path[i];
    if (c == '\\') {
      return false;
    }
    if (c != '%') {
      decoded.push_back(c);
      continue;
    }
    if (i + 2 >= raw_path.size()) {
      return false;
    }
    const int hi = hex_value(raw_path[i + 1]);
    const int lo = hex_value(raw_path[i + 2]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    const unsigned char d = static_cast<unsigned char>((hi << 4) | lo);
    // A decoded control character, space or backslash has no legitimate use in
    // a path we serve. Decoded '/' and '.' are allowed because the dot-segment
    // check below runs on the DECODED text — which is where %2e%2e%2f matters.
    if (d <= 0x20 || d == 0x7F || d == '\\') {
      return false;
    }
    decoded.push_back(static_cast<char>(d));
    i += 2;
  }

  std::size_t start = 1;
  while (start <= decoded.size()) {
    std::size_t end = decoded.find('/', start);
    if (end == npos) {
      end = decoded.size();
    }
    const std::string_view segment(decoded.data() + start, end - start);
    if (segment == "." || segment == "..") {
      return false;
    }
    start = end + 1;
  }

  out_path = std::move(decoded);
  return true;
}

HttpParseResult
parse_http_request(std::string_view bytes, const HttpParseLimits & limits, HttpRequest & out)
{
  out = HttpRequest{};

  // RFC 7230 section 3.5: ignore empty lines before the request-line.
  std::string_view view = bytes;
  std::size_t skipped = 0;
  while (view.size() >= 2 && view[0] == '\r' && view[1] == '\n') {
    view.remove_prefix(2);
    skipped += 2;
  }

  const std::size_t end = view.find("\r\n\r\n");
  if (end == npos) {
    // A bare-LF terminator would never match above: recognise it now instead
    // of letting the connection run into the size limit or the timeout.
    if (view.find("\n\n") != npos) {
      return invalid(400, "bare LF line endings");
    }
    if (bytes.size() >= limits.max_head_bytes) {
      return invalid(
        431, "request head exceeds " + std::to_string(limits.max_head_bytes) + " bytes");
    }
    HttpParseResult r;
    r.status = HttpParseStatus::Incomplete;
    return r;
  }
  if (skipped + end + 4 > limits.max_head_bytes) {
    return invalid(431, "request head exceeds " + std::to_string(limits.max_head_bytes) + " bytes");
  }

  const std::string_view head = view.substr(0, end);
  std::vector<std::string_view> lines;
  for (std::size_t pos = 0;;) {
    const std::size_t nl = head.find("\r\n", pos);
    lines.push_back(head.substr(pos, nl == npos ? npos : nl - pos));
    if (nl == npos) {
      break;
    }
    pos = nl + 2;
  }
  for (const std::string_view line : lines) {
    if (line.find('\r') != npos || line.find('\n') != npos) {
      return invalid(400, "bare CR or LF inside the head");
    }
  }

  // --- request line ---------------------------------------------------------
  const std::string_view request_line = lines[0];
  const std::size_t sp1 = request_line.find(' ');
  const std::size_t sp2 = (sp1 == npos) ? npos : request_line.find(' ', sp1 + 1);
  if (sp1 == npos || sp2 == npos || request_line.find(' ', sp2 + 1) != npos) {
    return invalid(400, "malformed request line");
  }
  const std::string_view method = request_line.substr(0, sp1);
  const std::string_view target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
  const std::string_view version = request_line.substr(sp2 + 1);

  if (!is_token(method) || method.size() > 32) {
    return invalid(400, "malformed method");
  }
  if (target.empty()) {
    return invalid(400, "empty request-target");
  }
  if (target.size() > limits.max_target_bytes) {
    return invalid(
      414, "request-target exceeds " + std::to_string(limits.max_target_bytes) + " bytes");
  }
  if (version.size() != 8 || version.substr(0, 5) != "HTTP/" || version[6] != '.' ||
    version[5] < '0' || version[5] > '9' || version[7] < '0' || version[7] > '9')
  {
    return invalid(400, "malformed HTTP version");
  }
  const int major = version[5] - '0';
  const int minor = version[7] - '0';
  if (major != 1 || (minor != 0 && minor != 1)) {
    return invalid(505, "unsupported HTTP version " + std::string(version));
  }

  // --- header fields ----------------------------------------------------------
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const std::string_view line = lines[i];
    if (line.empty()) {
      return invalid(400, "empty header line");
    }
    if (line[0] == ' ' || line[0] == '\t') {
      return invalid(400, "obsolete line folding");
    }
    const std::size_t colon = line.find(':');
    if (colon == npos || colon == 0) {
      return invalid(400, "header field without a name");
    }
    const std::string_view name = line.substr(0, colon);
    if (!is_token(name)) {
      return invalid(400, "whitespace or bad character in a header name");
    }
    const std::string_view value = trim_ows(line.substr(colon + 1));
    for (const char ch : value) {
      const unsigned char c = static_cast<unsigned char>(ch);
      if ((c < 0x20 && c != '\t') || c == 0x7F) {
        return invalid(400, "control character in a header value");
      }
    }
    if (out.headers.size() >= limits.max_headers) {
      return invalid(431, "more than " + std::to_string(limits.max_headers) + " header fields");
    }
    out.headers.push_back({std::string(name), std::string(value)});
  }

  std::size_t host_count = 0;
  for (const auto & h : out.headers) {
    if (ascii_iequals(h.name, "Host")) {
      ++host_count;
    }
    if (ascii_iequals(h.name, "Content-Length")) {
      if (h.value.empty() || h.value.size() > 18) {
        return invalid(400, "malformed Content-Length");
      }
      std::uint64_t n = 0;
      for (const char c : h.value) {
        if (c < '0' || c > '9') {
          return invalid(400, "malformed Content-Length");
        }
        n = n * 10 + static_cast<std::uint64_t>(c - '0');
      }
      if (out.content_length.has_value() && *out.content_length != n) {
        return invalid(400, "conflicting Content-Length fields");
      }
      out.content_length = n;
    }
    if (ascii_iequals(h.name, "Transfer-Encoding")) {
      out.has_transfer_encoding = true;
    }
  }
  if (major == 1 && minor == 1 && host_count != 1) {
    return invalid(400, host_count == 0 ? "missing Host header" : "multiple Host headers");
  }

  if (!decode_request_target(target, out.path, out.query)) {
    return invalid(400, "unacceptable request-target");
  }

  out.method.assign(method);
  out.target.assign(target);
  out.version_major = major;
  out.version_minor = minor;
  out.head_bytes = skipped + end + 4;

  HttpParseResult r;
  r.status = HttpParseStatus::Complete;
  return r;
}

// --- responses ----------------------------------------------------------------

HttpResponse
HttpResponse::text(int status, std::string body)
{
  HttpResponse r;
  r.status = status;
  r.content_type = "text/plain; charset=utf-8";
  r.body = std::move(body);
  return r;
}

HttpResponse
HttpResponse::html(std::string body)
{
  HttpResponse r;
  r.content_type = "text/html; charset=utf-8";
  r.body = std::move(body);
  return r;
}

HttpResponse
HttpResponse::not_found()
{
  return text(404, "not found\n");
}

const char *
http_reason_phrase(int status)
{
  switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 417: return "Expectation Failed";
    case 426: return "Upgrade Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    default: return "Unknown";
  }
}

std::string
http_date(std::int64_t unix_seconds)
{
  // Civil-from-days (Howard Hinnant's algorithm): no gmtime, no locale.
  std::int64_t days = unix_seconds / 86400;
  std::int64_t rem = unix_seconds % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  const std::int64_t z = days + 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t doe = z - era * 146097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const int day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  std::int64_t weekday = (days + 4) % 7;  // 1970-01-01 was a Thursday.
  if (weekday < 0) {
    weekday += 7;
  }

  static const char * const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char * const kMonths[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  char buf[64];
  std::snprintf(
    buf, sizeof(buf), "%s, %02d %s %04lld %02d:%02d:%02d GMT", kDays[weekday], day,
    kMonths[month - 1], static_cast<long long>(year), static_cast<int>(rem / 3600),
    static_cast<int>((rem % 3600) / 60), static_cast<int>(rem % 60));
  return buf;
}

std::string
http_date_now()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return http_date(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

std::string
serialize_http_response(
  const HttpResponse & response, bool head_only, bool keep_alive, const std::string & date)
{
  const bool bodiless_status = response.status == 204 || response.status == 304 ||
    (response.status >= 100 && response.status < 200);

  std::string out;
  out.reserve(320 + (head_only || bodiless_status ? 0 : response.body.size()));
  out += "HTTP/1.1 ";
  out += std::to_string(response.status);
  out += ' ';
  out += http_reason_phrase(response.status);
  out += "\r\nDate: ";
  out += date;
  out += "\r\nServer: qar_web_panels\r\n";
  if (!bodiless_status) {
    if (!response.content_type.empty()) {
      out += "Content-Type: ";
      out += response.content_type;
      out += "\r\n";
    }
    out += "Content-Length: ";
    out += std::to_string(response.body.size());
    out += "\r\n";
  }
  // The page URL carries the panel token: never cache it, never leak it in a
  // Referer, never let a browser sniff a text body into something executable.
  out += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
    "Referrer-Policy: no-referrer\r\n";
  for (const auto & h : response.extra_headers) {
    // A CR/LF inside a header would split the response; such a header is a
    // programming error upstream and is dropped rather than emitted.
    if (h.name.empty() || h.name.find_first_of("\r\n:") != std::string::npos ||
      h.value.find_first_of("\r\n") != std::string::npos)
    {
      continue;
    }
    out += h.name;
    out += ": ";
    out += h.value;
    out += "\r\n";
  }
  out += (keep_alive && !response.close) ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
  out += "\r\n";
  if (!head_only && !bodiless_status) {
    out += response.body;
  }
  return out;
}

}  // namespace qar_web_panels
