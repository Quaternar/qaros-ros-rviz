// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// choose_image_transport: which sensor_msgs message a topic is subscribed as.
// Pure stdlib logic, no rclcpp, no graph.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "qar_rviz_streaming/image_topic_transport.hpp"

using qar_rviz_streaming::choose_image_transport;
using qar_rviz_streaming::ImageTransport;

TEST(ImageTopicTransportTest, AnAdvertisedCompressedImageWinsOutright)
{
  const std::vector<std::string> types = {"sensor_msgs/msg/CompressedImage"};
  EXPECT_EQ(choose_image_transport("/cam/image_raw", types), ImageTransport::Compressed);
}

TEST(ImageTopicTransportTest, AnAdvertisedRawImageWins)
{
  const std::vector<std::string> types = {"sensor_msgs/msg/Image"};
  EXPECT_EQ(choose_image_transport("/cam/image_raw/compressed", types), ImageTransport::Raw);
}

TEST(ImageTopicTransportTest, CompressedBeatsRawWhenBothAreSomehowAdvertised)
{
  const std::vector<std::string> types = {
    "sensor_msgs/msg/Image", "sensor_msgs/msg/CompressedImage"};
  EXPECT_EQ(choose_image_transport("/cam/image_raw", types), ImageTransport::Compressed);
}

TEST(ImageTopicTransportTest, NothingAdvertisedFallsBackToTheCompressedSuffixConvention)
{
  EXPECT_EQ(choose_image_transport("/cam/image_raw/compressed", {}), ImageTransport::Compressed);
  EXPECT_EQ(choose_image_transport("/cam/image_raw", {}), ImageTransport::Raw);
}

TEST(ImageTopicTransportTest, AnUnrelatedAdvertisedTypeIsIgnored)
{
  // Neither Image nor CompressedImage advertised (e.g. the publisher has not
  // started and the graph reports something stale/unrelated): falls through to
  // the suffix convention rather than trusting the unrelated type.
  const std::vector<std::string> types = {"std_msgs/msg/String"};
  EXPECT_EQ(choose_image_transport("/cam/image_raw", types), ImageTransport::Raw);
  EXPECT_EQ(choose_image_transport("/cam/compressed", types), ImageTransport::Compressed);
}

TEST(ImageTopicTransportTest, ToStringNamesTheWireType)
{
  EXPECT_STREQ(qar_rviz_streaming::to_string(ImageTransport::Raw), "sensor_msgs/msg/Image");
  EXPECT_STREQ(
    qar_rviz_streaming::to_string(ImageTransport::Compressed),
    "sensor_msgs/msg/CompressedImage");
}
