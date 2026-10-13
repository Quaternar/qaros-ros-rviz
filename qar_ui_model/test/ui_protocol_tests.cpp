// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The envelope the panel server implements server-side and the page runtime
// implements client-side. The exact JSON bytes are pinned because two independent
// implementations are written against them.

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include "qar_ui_model/json.hpp"
#include "qar_ui_model/ui_protocol.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

namespace
{

ProtocolMessage
decode_ok(const std::string & text)
{
  ProtocolMessage message;
  std::string error;
  EXPECT_TRUE(decode_message(text, message, error)) << text << " -> " << error;
  return message;
}

std::string
decode_fail(const std::string & text)
{
  ProtocolMessage message;
  std::string error;
  EXPECT_FALSE(decode_message(text, message, error)) << "accepted: " << text;
  EXPECT_FALSE(error.empty());
  return error;
}

}  // namespace

TEST(Protocol, Constants)
{
  EXPECT_EQ(kProtocolVersion, 1);
  EXPECT_STREQ(kRuntimeName, "qar-ui-runtime/1");
  EXPECT_STREQ(to_string(MessageType::Control), "control");
  MessageType type = MessageType::Unknown;
  EXPECT_TRUE(message_type_from_string("pong", type));
  EXPECT_EQ(type, MessageType::Pong);
  EXPECT_FALSE(message_type_from_string("unknown", type));
}

TEST(Protocol, ExactWireBytesOfEveryFrame)
{
  EXPECT_EQ(encode_message(make_hello(1, "robot", kRuntimeName)),
    "{\"v\":1,\"type\":\"hello\",\"seq\":1,\"panel\":\"robot\",\"runtime\":\"qar-ui-runtime/1\"}");
  EXPECT_EQ(encode_message(make_hello(1, "robot", "", UiValue::object().set("version", 1),
    UiValue::object().set("x", 1))),
    "{\"v\":1,\"type\":\"hello\",\"seq\":1,\"panel\":\"robot\",\"model\":{\"version\":1},\"data\":{\"x\":1}}");
  EXPECT_EQ(encode_message(make_data(42, {{"battery.percentage", 0.5}, {"", UiValue::object()}})),
    "{\"v\":1,\"type\":\"data\",\"seq\":42,\"patch\":[{\"path\":\"battery.percentage\",\"value\":0.5},"
    "{\"path\":\"\",\"value\":{}}]}");
  EXPECT_EQ(encode_message(make_data_replace(3, UiValue::object().set("a", 1))),
    "{\"v\":1,\"type\":\"data\",\"seq\":3,\"patch\":[{\"path\":\"\",\"value\":{\"a\":1}}]}");
  EXPECT_EQ(encode_message(make_control(7, "speed", "publish:/cmd_vel", "cmd_vel.linear.x", 0.25)),
    "{\"v\":1,\"type\":\"control\",\"seq\":7,\"control\":\"speed\",\"action\":\"publish:/cmd_vel\","
    "\"path\":\"cmd_vel.linear.x\",\"value\":0.25}");
  EXPECT_EQ(encode_message(make_status(2, StatusLevel::Warning, "no publisher on /odom")),
    "{\"v\":1,\"type\":\"status\",\"seq\":2,\"level\":\"warning\",\"text\":\"no publisher on /odom\"}");
  EXPECT_EQ(encode_message(make_ack(8, 7, true)),
    "{\"v\":1,\"type\":\"ack\",\"seq\":8,\"event_seq\":7,\"ok\":true}");
  EXPECT_EQ(encode_message(make_ack(9, 7, false, "service unavailable")),
    "{\"v\":1,\"type\":\"ack\",\"seq\":9,\"event_seq\":7,\"ok\":false,\"text\":\"service unavailable\"}");
  EXPECT_EQ(encode_message(make_ping(10)), "{\"v\":1,\"type\":\"ping\",\"seq\":10}");
  EXPECT_EQ(encode_message(make_pong(11, 10)), "{\"v\":1,\"type\":\"pong\",\"seq\":11,\"ping_seq\":10}");
}

TEST(Protocol, TimestampIsOptionalAndEmittedAfterSeq)
{
  ProtocolMessage message = make_ping(5);
  message.timestamp = 1725000000.5;
  EXPECT_EQ(encode_message(message), "{\"v\":1,\"type\":\"ping\",\"seq\":5,\"ts\":1725000000.5}");
  const ProtocolMessage back = decode_ok(encode_message(message));
  ASSERT_TRUE(back.timestamp);
  EXPECT_DOUBLE_EQ(*back.timestamp, 1725000000.5);
  EXPECT_FALSE(decode_ok("{\"v\":1,\"type\":\"ping\",\"seq\":5}").timestamp);
}

TEST(Protocol, EveryFrameRoundTrips)
{
  const std::vector<ProtocolMessage> frames = {
    make_hello(1, "robot", kRuntimeName),
    make_hello(1, "robot", "", UiValue::object().set("version", 1), UiValue::object().set("x", 1)),
    make_data(42, {{"battery.percentage", 0.5}, {"ranges[2]", UiValue::array().push_back(1)}}),
    make_control(7, "speed", "publish", "cmd_vel.linear.x", 0.25),
    make_control(8, "stop", "call:/estop", "", UiValue()),
    make_control(9, "name", "", "name", "text\nwith \"quotes\""),
    make_control(10, "flag", "", "flag", true),
    make_control(11, "vec", "", "v", UiValue::object().set("x", 1.0).set("y", 2.0).set("z", 3.0)),
    make_status(2, StatusLevel::Error, "boom"),
    make_ack(8, 7, true),
    make_ack(9, 7, false, "nope"),
    make_ping(10),
    make_pong(11, 10),
  };
  for (const ProtocolMessage & frame : frames) {
    const std::string text = encode_message(frame);
    const ProtocolMessage back = decode_ok(text);
    EXPECT_EQ(back.type, frame.type) << text;
    EXPECT_EQ(back.seq, frame.seq) << text;
    EXPECT_EQ(to_value(back), to_value(frame)) << text;
    EXPECT_EQ(encode_message(back), text);
  }
}

TEST(Protocol, DecodedBodiesAreTyped)
{
  const ProtocolMessage control = decode_ok(
    "{\"v\":1,\"type\":\"control\",\"seq\":3,\"control\":\"speed\",\"action\":\"pub\",\"path\":\"x\",\"value\":2}");
  ASSERT_NE(control.as<ControlBody>(), nullptr);
  EXPECT_EQ(control.as<ControlBody>()->control, "speed");
  EXPECT_EQ(control.as<ControlBody>()->action, "pub");
  EXPECT_EQ(control.as<ControlBody>()->path, "x");
  EXPECT_EQ(control.as<ControlBody>()->value.as_int(), 2);
  EXPECT_EQ(control.as<DataBody>(), nullptr);

  // Optional members may be absent.
  const ProtocolMessage minimal = decode_ok("{\"v\":1,\"type\":\"control\",\"seq\":3,\"control\":\"go\"}");
  ASSERT_NE(minimal.as<ControlBody>(), nullptr);
  EXPECT_TRUE(minimal.as<ControlBody>()->action.empty());
  EXPECT_TRUE(minimal.as<ControlBody>()->value.is_null());

  const ProtocolMessage data = decode_ok(
    "{\"v\":1,\"type\":\"data\",\"seq\":4,\"patch\":[{\"path\":\"a.b[1]\",\"value\":null}]}");
  ASSERT_NE(data.as<DataBody>(), nullptr);
  ASSERT_EQ(data.as<DataBody>()->patch.size(), 1u);
  EXPECT_EQ(data.as<DataBody>()->patch[0].path, "a.b[1]");

  const ProtocolMessage status = decode_ok("{\"v\":1,\"type\":\"status\",\"seq\":5,\"text\":\"hi\"}");
  ASSERT_NE(status.as<StatusBody>(), nullptr);
  EXPECT_EQ(status.as<StatusBody>()->level, StatusLevel::Neutral);  // default when absent

  const ProtocolMessage hello = decode_ok("{\"v\":1,\"type\":\"hello\",\"seq\":1,\"panel\":\"p\"}");
  ASSERT_NE(hello.as<HelloBody>(), nullptr);
  EXPECT_TRUE(hello.as<HelloBody>()->model.is_null());

  EXPECT_NE(decode_ok("{\"v\":1,\"type\":\"ping\",\"seq\":1}").as<PingBody>(), nullptr);
  EXPECT_EQ(decode_ok("{\"v\":1,\"type\":\"pong\",\"seq\":2,\"ping_seq\":1}").as<PongBody>()->ping_seq, 1u);
}

TEST(Protocol, UnknownTypeIsForwardCompatible)
{
  const ProtocolMessage message = decode_ok("{\"v\":1,\"type\":\"telemetry\",\"seq\":9,\"x\":1}");
  EXPECT_EQ(message.type, MessageType::Unknown);
  EXPECT_EQ(message.type_name, "telemetry");
  EXPECT_EQ(message.seq, 9u);
  EXPECT_EQ(message.as<DataBody>(), nullptr);
  EXPECT_EQ(encode_message(message), "{\"v\":1,\"type\":\"telemetry\",\"seq\":9}");
}

TEST(Protocol, RejectsBadEnvelopes)
{
  EXPECT_NE(decode_fail("{\"type\":\"ping\",\"seq\":1}").find("version"), std::string::npos);
  EXPECT_NE(decode_fail("{\"v\":2,\"type\":\"ping\",\"seq\":1}").find("unsupported protocol version"),
    std::string::npos);
  EXPECT_NE(decode_fail("{\"v\":\"1\",\"type\":\"ping\",\"seq\":1}").find("version"), std::string::npos);
  decode_fail("{\"v\":1,\"seq\":1}");
  decode_fail("{\"v\":1,\"type\":5,\"seq\":1}");
  decode_fail("{\"v\":1,\"type\":\"ping\"}");
  decode_fail("{\"v\":1,\"type\":\"ping\",\"seq\":-1}");
  decode_fail("{\"v\":1,\"type\":\"ping\",\"seq\":\"1\"}");
  decode_fail("{\"v\":1,\"type\":\"ping\",\"seq\":1,\"ts\":\"now\"}");
  decode_fail("[1,2]");
  decode_fail("not json");
  EXPECT_NE(decode_fail("{\"v\":1,").find("malformed JSON"), std::string::npos);
}

TEST(Protocol, RejectsBadBodies)
{
  decode_fail("{\"v\":1,\"type\":\"hello\",\"seq\":1}");                       // no panel
  decode_fail("{\"v\":1,\"type\":\"hello\",\"seq\":1,\"panel\":\"p\",\"model\":5}");
  decode_fail("{\"v\":1,\"type\":\"data\",\"seq\":1}");                        // no patch
  decode_fail("{\"v\":1,\"type\":\"data\",\"seq\":1,\"patch\":{}}");
  decode_fail("{\"v\":1,\"type\":\"data\",\"seq\":1,\"patch\":[5]}");
  decode_fail("{\"v\":1,\"type\":\"data\",\"seq\":1,\"patch\":[{\"path\":\"a\"}]}");  // no value
  decode_fail("{\"v\":1,\"type\":\"data\",\"seq\":1,\"patch\":[{\"path\":\"a..b\",\"value\":1}]}");
  decode_fail("{\"v\":1,\"type\":\"control\",\"seq\":1}");                     // no control id
  decode_fail("{\"v\":1,\"type\":\"control\",\"seq\":1,\"control\":\"bad id\"}");
  decode_fail("{\"v\":1,\"type\":\"control\",\"seq\":1,\"control\":\"x\",\"action\":7}");
  decode_fail("{\"v\":1,\"type\":\"status\",\"seq\":1,\"level\":\"purple\"}");
  decode_fail("{\"v\":1,\"type\":\"ack\",\"seq\":1,\"ok\":true}");             // no event_seq
  decode_fail("{\"v\":1,\"type\":\"ack\",\"seq\":1,\"event_seq\":1}");         // no ok
  decode_fail("{\"v\":1,\"type\":\"ack\",\"seq\":1,\"event_seq\":1,\"ok\":\"yes\"}");
  decode_fail("{\"v\":1,\"type\":\"pong\",\"seq\":1}");                        // no ping_seq
}

TEST(ApplyPatch, AssignmentsInOrderWithRootReplace)
{
  UiValue document = UiValue::object().set("keep", 1);
  std::string error;
  DataBody body;
  body.patch = {
    {"battery.percentage", 0.5},
    {"battery.cells[1]", 3.7},
    {"battery.percentage", 0.6},  // later entry wins
  };
  ASSERT_TRUE(apply_patch(document, body, error)) << error;
  EXPECT_EQ(document.find("keep")->as_int(), 1);
  EXPECT_DOUBLE_EQ(document.find("battery")->find("percentage")->as_double(), 0.6);
  ASSERT_EQ(document.find("battery")->find("cells")->size(), 2u);
  EXPECT_TRUE(document.find("battery")->find("cells")->at(0)->is_null());

  DataBody replace;
  replace.patch = {{"", UiValue::object().set("fresh", true)}, {"added", 2}};
  ASSERT_TRUE(apply_patch(document, replace, error)) << error;
  EXPECT_EQ(document.size(), 2u);
  EXPECT_TRUE(document.find("fresh")->as_bool());
  EXPECT_EQ(document.find("added")->as_int(), 2);
}

TEST(ApplyPatch, IsAllOrNothing)
{
  UiValue document = UiValue::object().set("scalar", 1);
  const UiValue before = document;
  std::string error;
  DataBody body;
  body.patch = {{"fine", 1}, {"scalar.child", 2}, {"also_fine", 3}};
  EXPECT_FALSE(apply_patch(document, body, error));
  EXPECT_NE(error.find("patch entry 1"), std::string::npos);
  EXPECT_EQ(document, before);

  DataBody bad_path;
  bad_path.patch = {{"a..b", 1}};
  EXPECT_FALSE(apply_patch(document, bad_path, error));
  EXPECT_EQ(document, before);

  DataBody empty;
  EXPECT_TRUE(apply_patch(document, empty, error));
  EXPECT_EQ(document, before);
}

TEST(ApplyPatch, DecodedDataFrameAppliesToADocument)
{
  const ProtocolMessage frame = decode_ok(
    "{\"v\":1,\"type\":\"data\",\"seq\":1,\"patch\":[{\"path\":\"pose.position\",\"value\":{\"x\":1.5,\"y\":0.0,\"z\":0.0}}]}");
  UiValue document;
  std::string error;
  ASSERT_TRUE(apply_patch(document, *frame.as<DataBody>(), error)) << error;
  EXPECT_DOUBLE_EQ(document.find("pose")->find("position")->find("x")->as_double(), 1.5);
}

// ---------------------------------------------------------------------------
// viewport — the frame that exists because the SDK does not say how a website
// panel's browser viewport follows from the metres we declare.
// ---------------------------------------------------------------------------

TEST(Viewport, ExactWireBytes)
{
  ViewportBody body;
  body.css_width = 1024.0;
  body.css_height = 640.0;
  body.device_pixel_ratio = 1.5;
  body.content_width = 1024.0;
  body.content_height = 1580.0;
  body.root_width = 1024.0;
  body.root_height = 1580.0;
  body.reason = "connect";
  EXPECT_EQ(encode_message(make_viewport(3, body)),
    "{\"v\":1,\"type\":\"viewport\",\"seq\":3,\"css_w\":1024.0,\"css_h\":640.0,\"dpr\":1.5,"
    "\"content_w\":1024.0,\"content_h\":1580.0,\"root_w\":1024.0,\"root_h\":1580.0,"
    "\"reason\":\"connect\"}");
}

TEST(Viewport, OmitsWhatThePageCouldNotMeasure)
{
  // Absent is "not reported", not zero — a receiver has to be able to tell an
  // engine that has no devicePixelRatio from one that reports 0.
  ViewportBody body;
  body.css_width = 800.0;
  body.css_height = 800.0;
  EXPECT_EQ(encode_message(make_viewport(1, body)),
    "{\"v\":1,\"type\":\"viewport\",\"seq\":1,\"css_w\":800.0,\"css_h\":800.0}");
}

TEST(Viewport, RoundTrips)
{
  ViewportBody body;
  body.css_width = 1024.0;
  body.css_height = 640.0;
  body.device_pixel_ratio = 2.0;
  body.content_height = 1580.0;
  body.reason = "resize";
  const ProtocolMessage back = decode_ok(encode_message(make_viewport(7, body)));
  ASSERT_EQ(back.type, MessageType::Viewport);
  EXPECT_EQ(back.seq, 7u);
  const ViewportBody * decoded = back.as<ViewportBody>();
  ASSERT_NE(decoded, nullptr);
  EXPECT_DOUBLE_EQ(decoded->css_width, 1024.0);
  EXPECT_DOUBLE_EQ(decoded->css_height, 640.0);
  EXPECT_DOUBLE_EQ(decoded->device_pixel_ratio, 2.0);
  EXPECT_DOUBLE_EQ(decoded->content_height, 1580.0);
  EXPECT_DOUBLE_EQ(decoded->content_width, 0.0);  // absent -> not reported
  EXPECT_EQ(decoded->reason, "resize");
}

TEST(Viewport, RejectsAMeasurementThatIsNotOne)
{
  // A panel is sized from these numbers, so a broken measurement must not get
  // through: it would come back as a physically wrong panel, silently.
  EXPECT_NE(decode_fail("{\"v\":1,\"type\":\"viewport\",\"seq\":1,\"css_h\":600}")
    .find("css_w"), std::string::npos);
  EXPECT_NE(decode_fail("{\"v\":1,\"type\":\"viewport\",\"seq\":1,\"css_w\":0,\"css_h\":600}")
    .find("css_w"), std::string::npos);
  EXPECT_NE(decode_fail("{\"v\":1,\"type\":\"viewport\",\"seq\":1,\"css_w\":-4,\"css_h\":600}")
    .find("css_w"), std::string::npos);
  EXPECT_NE(decode_fail("{\"v\":1,\"type\":\"viewport\",\"seq\":1,\"css_w\":\"wide\",\"css_h\":600}")
    .find("css_w"), std::string::npos);
  EXPECT_FALSE(decode_fail(
      "{\"v\":1,\"type\":\"viewport\",\"seq\":1,\"css_w\":800,\"css_h\":600,\"dpr\":-1}").empty());
}

TEST(Viewport, AnOlderPeerIgnoresItInsteadOfBreaking)
{
  // Adding a frame type is not a version change (docs/WIRE_PROTOCOL.md,
  // "Versioning"). The guarantee has two halves and this pins both.
  //
  // Half one: the frame a NEW sender emits still satisfies every envelope rule
  // an OLD receiver checks, so that receiver reaches its "unknown type ->
  // ignore" branch rather than its "malformed -> close the socket" one.
  ViewportBody body;
  body.css_width = 1024.0;
  body.css_height = 640.0;
  const std::string wire = encode_message(make_viewport(5, body));
  UiValue envelope;
  JsonParseError parse_error;
  ASSERT_TRUE(json_parse(wire, envelope, parse_error)) << parse_error.to_string();
  ASSERT_TRUE(envelope.is_object());
  EXPECT_EQ(envelope.find("v")->as_int(), kProtocolVersion);   // same major version
  EXPECT_TRUE(envelope.find("type")->is_string());
  EXPECT_EQ(envelope.find("seq")->as_int(), 5);

  // Half two: an unknown type decodes as Unknown with its name kept, which is
  // exactly what an older build does with "viewport" and what this build does
  // with whatever is added next.
  const ProtocolMessage future = decode_ok(
    "{\"v\":1,\"type\":\"viewport2\",\"seq\":9,\"css_w\":1,\"anything\":[1,2]}");
  EXPECT_EQ(future.type, MessageType::Unknown);
  EXPECT_EQ(future.type_name, "viewport2");
  EXPECT_EQ(future.seq, 9u);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(future.body));
}

TEST(Viewport, TypeNameRoundTrips)
{
  EXPECT_STREQ(to_string(MessageType::Viewport), "viewport");
  MessageType type = MessageType::Unknown;
  ASSERT_TRUE(message_type_from_string("viewport", type));
  EXPECT_EQ(type, MessageType::Viewport);
}
