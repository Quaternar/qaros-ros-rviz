// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "qar_web_panels/http_message.hpp"
#include "qar_web_panels/panel_token.hpp"

using namespace qar_web_panels;

namespace
{

HttpParseResult
parse(const std::string & bytes, HttpRequest & out, HttpParseLimits limits = {})
{
  return parse_http_request(bytes, limits, out);
}

int
invalid_status(const std::string & bytes, HttpParseLimits limits = {})
{
  HttpRequest request;
  const HttpParseResult r = parse(bytes, request, limits);
  return r.status == HttpParseStatus::Invalid ? r.http_status : 0;
}

}  // namespace

TEST(HttpParse, RequestLineHeadersPathAndQuery)
{
  const std::string bytes =
    "GET /p/abc/?x=1&y=2 HTTP/1.1\r\nHost: h:1\r\nAccept: */*\r\nConnection:  keep-alive \r\n\r\n";
  HttpRequest r;
  const HttpParseResult result = parse(bytes, r);
  ASSERT_EQ(result.status, HttpParseStatus::Complete) << result.reason;
  EXPECT_EQ(r.method, "GET");
  EXPECT_EQ(r.target, "/p/abc/?x=1&y=2");
  EXPECT_EQ(r.path, "/p/abc/");
  EXPECT_EQ(r.query, "x=1&y=2");
  EXPECT_TRUE(r.is_http_1_1());
  ASSERT_EQ(r.headers.size(), 3u);
  EXPECT_EQ(r.header("host"), "h:1");
  EXPECT_EQ(r.header("Connection"), "keep-alive");
  EXPECT_EQ(r.header("nope"), "");
  EXPECT_TRUE(r.keep_alive());
  EXPECT_FALSE(r.is_websocket_upgrade());
  EXPECT_EQ(r.head_bytes, bytes.size());
  EXPECT_FALSE(r.content_length.has_value());
}

TEST(HttpParse, IncompleteUntilTheBlankLine)
{
  HttpRequest r;
  EXPECT_EQ(parse("GET / HTTP/1.1\r\nHost: h\r\n", r).status, HttpParseStatus::Incomplete);
  EXPECT_EQ(parse("GET / HTTP/1.1\r\nHost: h\r\n\r", r).status, HttpParseStatus::Incomplete);
  EXPECT_EQ(parse("", r).status, HttpParseStatus::Incomplete);
  EXPECT_EQ(parse("GET / HTTP/1.1\r\nHost: h\r\n\r\n", r).status, HttpParseStatus::Complete);
}

TEST(HttpParse, LeadingEmptyLinesAreIgnoredAndCounted)
{
  HttpRequest r;
  const std::string bytes = "\r\n\r\nGET / HTTP/1.1\r\nHost: h\r\n\r\nGET";
  ASSERT_EQ(parse(bytes, r).status, HttpParseStatus::Complete);
  EXPECT_EQ(r.head_bytes, bytes.size() - 3);
}

TEST(HttpParse, MalformedFramingIs400)
{
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\nHost: h\n\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\rX: y\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET /\r\nHost: h\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1 extra\r\nHost: h\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("G@T / HTTP/1.1\r\nHost: h\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/x.1\r\nHost: h\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost : h\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\n  folded\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\n: v\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\nnocolon\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status(std::string("GET / HTTP/1.1\r\nHost: h\r\nX: a\x01" "b\r\n\r\n")), 400);
}

TEST(HttpParse, HostRulesFollowTheVersion)
{
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n"), 400);
  HttpRequest r;
  EXPECT_EQ(parse("GET / HTTP/1.0\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_FALSE(r.keep_alive());
  EXPECT_EQ(parse("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_TRUE(r.keep_alive());
  EXPECT_EQ(parse("GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_FALSE(r.keep_alive());
}

TEST(HttpParse, UnsupportedVersionIs505)
{
  EXPECT_EQ(invalid_status("GET / HTTP/2.0\r\nHost: h\r\n\r\n"), 505);
  EXPECT_EQ(invalid_status("GET / HTTP/1.2\r\nHost: h\r\n\r\n"), 505);
}

TEST(HttpParse, SizeLimitsGive431And414)
{
  const std::string no_end(17 * 1024, 'X');
  EXPECT_EQ(invalid_status(no_end), 431);
  HttpParseLimits small;
  small.max_head_bytes = 64;
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\nX: " + std::string(60, 'y') + "\r\n\r\n", small), 431);
  small = HttpParseLimits{};
  small.max_headers = 2;
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\nA: 1\r\nB: 2\r\n\r\n", small), 431);
  EXPECT_EQ(invalid_status("GET /" + std::string(3000, 'a') + " HTTP/1.1\r\nHost: h\r\n\r\n"), 414);
  HttpRequest r;
  EXPECT_EQ(parse("GET / HTTP/1.1\r\nHost: h\r\nA: 1\r\n\r\n", r, small).status, HttpParseStatus::Complete);
}

TEST(HttpParse, ContentLengthAndTransferEncodingAreReported)
{
  HttpRequest r;
  ASSERT_EQ(parse("POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 12\r\n\r\n", r).status, HttpParseStatus::Complete);
  ASSERT_TRUE(r.content_length.has_value());
  EXPECT_EQ(*r.content_length, 12u);
  EXPECT_FALSE(r.has_transfer_encoding);
  ASSERT_EQ(parse("POST / HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_TRUE(r.has_transfer_encoding);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\nContent-Length: -1\r\n\r\n"), 400);
  EXPECT_EQ(invalid_status("GET / HTTP/1.1\r\nHost: h\r\nContent-Length: 12a\r\n\r\n"), 400);
}

TEST(HttpParse, TraversalShapedTargetsAre400)
{
  for (const char * target : {
      "/p/../x", "/p/%2e%2e/x", "/p/..%2fx", "/p/%2E%2E%2Fx", "/..", "/p\\x", "/p/%5cx",
      "/p/%00", "/p/%zz", "/p/%2", "/a/./b", "/p/x#frag", "p/x", "*", "/p/%20x", "/p/%7f"})
  {
    EXPECT_EQ(invalid_status(std::string("GET ") + target + " HTTP/1.1\r\nHost: h\r\n\r\n"), 400)
      << target;
  }
}

TEST(HttpParse, DecodesEscapesAndAcceptsAbsoluteForm)
{
  HttpRequest r;
  ASSERT_EQ(parse("GET /p/a%2Fb%41?q=%zz HTTP/1.1\r\nHost: h\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_EQ(r.path, "/p/a/bA");
  EXPECT_EQ(r.query, "q=%zz");
  ASSERT_EQ(parse("GET http://h:1/p/x HTTP/1.1\r\nHost: h\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_EQ(r.path, "/p/x");
  ASSERT_EQ(parse("GET HTTP://h HTTP/1.1\r\nHost: h\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_EQ(r.path, "/");
}

TEST(HttpParse, HeaderTokensAndUpgradeDetection)
{
  HttpRequest r;
  ASSERT_EQ(
    parse("GET /ws/x HTTP/1.1\r\nHost: h\r\nConnection: keep-alive, Upgrade\r\nUpgrade: WebSocket\r\n\r\n", r).status,
    HttpParseStatus::Complete);
  EXPECT_TRUE(r.header_has_token("connection", "upgrade"));
  EXPECT_TRUE(r.header_has_token("Connection", "KEEP-ALIVE"));
  EXPECT_FALSE(r.header_has_token("Connection", "close"));
  EXPECT_TRUE(r.is_websocket_upgrade());
  ASSERT_EQ(parse("GET / HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\n\r\n", r).status, HttpParseStatus::Complete);
  EXPECT_FALSE(r.is_websocket_upgrade()) << "Upgrade without Connection: upgrade is not a handshake";
}

TEST(HttpMessage, HostAndOriginHelpers)
{
  EXPECT_TRUE(is_acceptable_host_header("192.168.1.5:8080"));
  EXPECT_TRUE(is_acceptable_host_header("[fe80::1]:80"));
  EXPECT_TRUE(is_acceptable_host_header("robot-pc.local"));
  EXPECT_FALSE(is_acceptable_host_header(""));
  EXPECT_FALSE(is_acceptable_host_header("a b"));
  EXPECT_FALSE(is_acceptable_host_header("a/b"));
  EXPECT_FALSE(is_acceptable_host_header("a\"b"));
  EXPECT_FALSE(is_acceptable_host_header(std::string(256, 'a')));
  EXPECT_EQ(origin_authority("http://192.168.1.5:8080"), "192.168.1.5:8080");
  EXPECT_EQ(origin_authority("https://a.b/path"), "a.b");
  EXPECT_EQ(origin_authority("null"), "");
  EXPECT_EQ(origin_authority("://x"), "");
  EXPECT_EQ(origin_authority("http://bad host"), "");
  EXPECT_TRUE(ascii_iequals("Content-Type", "content-type"));
  EXPECT_FALSE(ascii_iequals("a", "ab"));
}

TEST(HttpMessage, SerializesResponsesExactly)
{
  const std::string date = "Sun, 06 Nov 1994 08:49:37 GMT";
  const std::string full = serialize_http_response(HttpResponse::html("<p>hi</p>"), false, true, date);
  EXPECT_EQ(
    full,
    "HTTP/1.1 200 OK\r\n"
    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
    "Server: qar_web_panels\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 9\r\n"
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
    "<p>hi</p>");

  const std::string head = serialize_http_response(HttpResponse::html("<p>hi</p>"), true, false, date);
  EXPECT_NE(head.find("Content-Length: 9\r\n"), std::string::npos);
  EXPECT_NE(head.find("Connection: close\r\n"), std::string::npos);
  EXPECT_EQ(head.substr(head.size() - 4), "\r\n\r\n") << "HEAD carries no body";

  HttpResponse extra = HttpResponse::text(405, "no\n");
  extra.extra_headers.push_back({"Allow", "GET, HEAD"});
  extra.extra_headers.push_back({"Evil", "a\r\nInjected: yes"});
  extra.close = true;
  const std::string text = serialize_http_response(extra, false, true, date);
  EXPECT_NE(text.find("HTTP/1.1 405 Method Not Allowed\r\n"), std::string::npos);
  EXPECT_NE(text.find("Allow: GET, HEAD\r\n"), std::string::npos);
  EXPECT_EQ(text.find("Injected"), std::string::npos);
  EXPECT_NE(text.find("Connection: close\r\n"), std::string::npos) << "response.close wins over keep-alive";

  HttpResponse empty;
  empty.status = 204;
  const std::string no_content = serialize_http_response(empty, false, true, date);
  EXPECT_EQ(no_content.find("Content-Length"), std::string::npos);
  EXPECT_STREQ(http_reason_phrase(426), "Upgrade Required");
  EXPECT_STREQ(http_reason_phrase(999), "Unknown");
}

TEST(HttpMessage, DatesAreImfFixdate)
{
  EXPECT_EQ(http_date(784111777), "Sun, 06 Nov 1994 08:49:37 GMT");
  EXPECT_EQ(http_date(0), "Thu, 01 Jan 1970 00:00:00 GMT");
  EXPECT_EQ(http_date(951782400), "Tue, 29 Feb 2000 00:00:00 GMT");
  EXPECT_EQ(http_date(-1), "Wed, 31 Dec 1969 23:59:59 GMT");
  EXPECT_EQ(http_date_now().size(), 29u);
}

TEST(PanelToken, IsRandomWellFormedAndComparedInConstantTime)
{
  std::set<std::string> seen;
  for (int i = 0; i < 64; ++i) {
    const std::string token = generate_panel_token();
    EXPECT_TRUE(is_well_formed_token(token)) << token;
    seen.insert(token);
  }
  EXPECT_EQ(seen.size(), 64u);
  EXPECT_FALSE(is_well_formed_token("0123456789abcdef0123456789abcdeF"));
  EXPECT_FALSE(is_well_formed_token("0123456789abcdef0123456789abcde"));
  EXPECT_FALSE(is_well_formed_token(""));
  EXPECT_TRUE(tokens_equal("abc", "abc"));
  EXPECT_FALSE(tokens_equal("abc", "abd"));
  EXPECT_FALSE(tokens_equal("abc", "ab"));
}
