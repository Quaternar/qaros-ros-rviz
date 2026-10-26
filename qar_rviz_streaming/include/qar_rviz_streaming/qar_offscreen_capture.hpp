// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__QAR_OFFSCREEN_CAPTURE_HPP_
#define QAR_RVIZ_STREAMING__QAR_OFFSCREEN_CAPTURE_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace qar_rviz_streaming
{

/// @brief Content statistics of a captured offscreen frame. They decide
/// (scriptably, no human needed) whether the capture actually contains scene
/// content and non-uniform depth.
struct CaptureStats
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t color_non_background_pixels = 0;  ///< pixels differing from the clear colour
  std::uint64_t depth_nonzero_pixels = 0;         ///< pixels with metric depth > 0 (geometry)
  float depth_min = 0.0f;                          ///< min metric depth over geometry (metres)
  float depth_max = 0.0f;                          ///< max metric depth over geometry (metres)
};

/// @brief Write an RGBA8 buffer (row-major, byte order R,G,B,A) as a PNG.
bool writeColorPng(
  const std::string & path, std::uint32_t width, std::uint32_t height,
  const std::vector<std::uint8_t> & rgba);

/// @brief Write an 8-bit grayscale visualization PNG of metric depth: geometry is
/// normalized across its non-zero range with NEAR = bright, FAR = dark; empty
/// (zero) background stays black. Purely for human/debug inspection.
bool writeDepthVisualizationPng(
  const std::string & path, std::uint32_t width, std::uint32_t height,
  const std::vector<float> & depth_metres);

/// @brief Write the raw metric depth as little-endian float32, one per pixel,
/// row-major (top row first, matching the PNG). For downstream/analytic use.
bool writeDepthRawF32(
  const std::string & path, std::uint32_t width, std::uint32_t height,
  const std::vector<float> & depth_metres);

/// @brief Compute content statistics for a captured frame.
CaptureStats computeCaptureStats(
  std::uint32_t width, std::uint32_t height,
  const std::vector<std::uint8_t> & rgba, const std::vector<float> & depth_metres);

/// @brief Quantitative stereo-parallax measurement over a left/right eye pair.
///
/// Splits the shared metric-depth range into a NEAR and a FAR band and measures,
/// for each band, the horizontal shift of the band's pixel centroid between the
/// left and right eye. Correct stereo has parallax proportional to 1/depth, so
/// the near band's disparity must exceed the far band's (@ref nearer_shifts_more)
/// — the acceptance signal for stereo rendering, computed directly from the
/// rendered eyes. Also reports the mean absolute color difference to confirm the
/// two eyes rendered genuinely different viewpoints (not a duplicated image).
struct StereoParallaxStats
{
  float near_band_lo = 0.0f;
  float near_band_hi = 0.0f;
  float far_band_lo = 0.0f;
  float far_band_hi = 0.0f;
  std::uint64_t near_band_pixels = 0;  ///< min(left,right) populated pixels in band
  std::uint64_t far_band_pixels = 0;
  double near_disparity_px = 0.0;      ///< |meanX_left - meanX_right| over near band
  double far_disparity_px = 0.0;       ///< |meanX_left - meanX_right| over far band
  bool nearer_shifts_more = false;     ///< near_disparity_px > far_disparity_px
  double image_mean_abs_diff = 0.0;    ///< mean |L-R| over RGB (0..255); >0 => differ
};

/// @brief Compute stereo parallax stats from a left/right color+depth pair
/// (all row-major, same @p width x @p height). Depth in metres, 0 = background.
StereoParallaxStats computeStereoParallax(
  std::uint32_t width, std::uint32_t height,
  const std::vector<std::uint8_t> & left_rgba,
  const std::vector<std::uint8_t> & right_rgba,
  const std::vector<float> & left_depth,
  const std::vector<float> & right_depth);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__QAR_OFFSCREEN_CAPTURE_HPP_
