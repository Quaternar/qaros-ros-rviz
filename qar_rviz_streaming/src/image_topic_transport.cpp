// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/image_topic_transport.hpp"

#include <algorithm>

namespace qar_rviz_streaming
{
namespace
{
constexpr const char * kRawType = "sensor_msgs/msg/Image";
constexpr const char * kCompressedType = "sensor_msgs/msg/CompressedImage";

bool ends_with(const std::string & text, const std::string & suffix)
{
  return text.size() >= suffix.size() &&
    text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

ImageTransport choose_image_transport(
  const std::string & topic, const std::vector<std::string> & advertised_types)
{
  const bool advertises_compressed = std::find(
    advertised_types.begin(), advertised_types.end(), kCompressedType) != advertised_types.end();
  if (advertises_compressed) {
    return ImageTransport::Compressed;
  }
  const bool advertises_raw = std::find(
    advertised_types.begin(), advertised_types.end(), kRawType) != advertised_types.end();
  if (advertises_raw) {
    return ImageTransport::Raw;
  }
  return ends_with(topic, "/compressed") ? ImageTransport::Compressed : ImageTransport::Raw;
}

const char * to_string(ImageTransport transport)
{
  return transport == ImageTransport::Compressed ? kCompressedType : kRawType;
}

}  // namespace qar_rviz_streaming
