// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The enlarged AR copy of a Qt panel: which factor and render scale a copy gets, the 1.8 m limit,
// which streams are scaled and the words of the log lines. Standard library only.

#include "qar_rviz_streaming/ar_copy_scale.hpp"

#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::ArCopyScale;
using qar_rviz_streaming::ArCopyScaleSource;
using qar_rviz_streaming::ArCopySize;

/// A copy whose video is @p video_m tall at factor 1 and whose frame is @p frame_m: the height is
/// a straight line of the factor, the frame does not scale.
float height_at(double factor, float video_m, float frame_m)
{
  return static_cast<float>(static_cast<double>(video_m) * factor) + frame_m;
}

}  // namespace

TEST(ArCopyScale, EnlargedUsesTheDefaultFactor)
{
  const ArCopyScale scale = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, qar_rviz_streaming::kArCopyScaleDefault, 0.9f, false);
  EXPECT_DOUBLE_EQ(scale.factor, qar_rviz_streaming::kArCopyScaleDefault);
  EXPECT_DOUBLE_EQ(scale.render_scale, scale.factor);
  EXPECT_EQ(scale.source, ArCopyScaleSource::Setting);

  // The factor of the setting counts; a panel's own choice is named as such.
  const ArCopyScale own = qar_rviz_streaming::ar_copy_scale(ArCopySize::Enlarged, 1.5, 0.9f, true);
  EXPECT_DOUBLE_EQ(own.factor, 1.5);
  EXPECT_DOUBLE_EQ(own.render_scale, 1.5);
  EXPECT_EQ(own.source, ArCopyScaleSource::PanelChoice);

  // A factor that is not a number or outside the range is read as the default.
  for (const double bad : {0.0, 0.5, 3.5, std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()})
  {
    const ArCopyScale scale_of_bad =
      qar_rviz_streaming::ar_copy_scale(ArCopySize::Enlarged, bad, 0.9f, false);
    EXPECT_DOUBLE_EQ(scale_of_bad.factor, qar_rviz_streaming::kArCopyScaleDefault) << bad;
  }
}

TEST(ArCopyScale, NativeIsOne)
{
  const ArCopyScale scale = qar_rviz_streaming::ar_copy_scale(ArCopySize::Native, 2.0, 0.9f, false);
  EXPECT_DOUBLE_EQ(scale.factor, 1.0);
  EXPECT_DOUBLE_EQ(scale.render_scale, 1.0);
  EXPECT_EQ(scale.source, ArCopyScaleSource::Setting);
  const ArCopyScale own = qar_rviz_streaming::ar_copy_scale(ArCopySize::Native, 2.0, 5.0f, true);
  EXPECT_DOUBLE_EQ(own.factor, 1.0);
  EXPECT_EQ(own.source, ArCopyScaleSource::PanelChoice);
}

TEST(ArCopyScale, ATallPanelIsEnlargedLessToStayUnderTheCap)
{
  using qar_rviz_streaming::kArCopyMaxHeightM;
  // A 1400 px copy: 1.68 m of video and a frame of 0.07 m. Native it is 1.75 m, doubled 3.43 m.
  constexpr float kVideo = 1.68f;
  constexpr float kFrame = 0.07f;
  const ArCopyScale scale = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 2.0, kVideo + kFrame, height_at(2.0, kVideo, kFrame), false);
  EXPECT_EQ(scale.source, ArCopyScaleSource::LimitedByHeight);
  EXPECT_GT(scale.factor, 1.0);
  EXPECT_LT(scale.factor, 1.1);
  EXPECT_LE(height_at(scale.factor, kVideo, kFrame), kArCopyMaxHeightM + 1e-4f);
  EXPECT_DOUBLE_EQ(scale.render_scale, scale.factor);

  // Without a frame the height is the native height times the factor.
  const ArCopyScale framed_none = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 2.0, 1.2f, false);
  EXPECT_EQ(framed_none.source, ArCopyScaleSource::LimitedByHeight);
  EXPECT_NEAR(framed_none.factor, 1.5, 0.011);
  EXPECT_LE(1.2 * framed_none.factor, static_cast<double>(kArCopyMaxHeightM) + 1e-9);

  // A copy that fits keeps the factor and the source of the choice.
  const ArCopyScale fits = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 2.0, 0.8f, height_at(2.0, 0.75f, 0.05f), true);
  EXPECT_DOUBLE_EQ(fits.factor, 2.0);
  EXPECT_EQ(fits.source, ArCopyScaleSource::PanelChoice);

  // Exactly at the limit is not limited.
  const ArCopyScale at_cap = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 2.0, 0.9f, qar_rviz_streaming::kArCopyMaxHeightM, false);
  EXPECT_DOUBLE_EQ(at_cap.factor, 2.0);
  EXPECT_EQ(at_cap.source, ArCopyScaleSource::Setting);
}

TEST(ArCopyScale, TheFactorNeverDropsBelowOne)
{
  // Native it is above the limit already: the factor is 1, still named as limited.
  const ArCopyScale taller = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 2.0, 2.4f, 4.7f, false);
  EXPECT_DOUBLE_EQ(taller.factor, 1.0);
  EXPECT_DOUBLE_EQ(taller.render_scale, 1.0);
  EXPECT_EQ(taller.source, ArCopyScaleSource::LimitedByHeight);
  const ArCopyScale four_args = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 3.0, 9.0f, false);
  EXPECT_DOUBLE_EQ(four_args.factor, 1.0);

  // A height that is not known (0 or not a number) does not lower the factor.
  for (const float unknown : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
    const ArCopyScale scale = qar_rviz_streaming::ar_copy_scale(
      ArCopySize::Enlarged, 2.0, unknown, false);
    EXPECT_DOUBLE_EQ(scale.factor, 2.0);
    EXPECT_EQ(scale.source, ArCopyScaleSource::Setting);
  }
}

TEST(ArCopyScale, TheRatioIsSticky)
{
  using qar_rviz_streaming::sticky_render_scale;
  EXPECT_DOUBLE_EQ(sticky_render_scale(1.0, 2.0), 2.0);
  EXPECT_DOUBLE_EQ(sticky_render_scale(2.0, 1.0), 2.0);
  EXPECT_DOUBLE_EQ(sticky_render_scale(2.0, 2.0), 2.0);
  EXPECT_DOUBLE_EQ(sticky_render_scale(1.5, 2.5), 2.5);
  EXPECT_DOUBLE_EQ(sticky_render_scale(0.0, 0.0), 1.0);
  EXPECT_DOUBLE_EQ(sticky_render_scale(std::numeric_limits<double>::quiet_NaN(), 2.0), 2.0);
  EXPECT_DOUBLE_EQ(sticky_render_scale(2.0, std::numeric_limits<double>::quiet_NaN()), 2.0);
}

TEST(ArCopyScale, VideoPanelsAreNotScaled)
{
  using qar_rviz_streaming::ar_copy_scales_key;
  EXPECT_TRUE(ar_copy_scales_key("qar_streaming_panel"));
  EXPECT_TRUE(ar_copy_scales_key("dock:Views"));
  EXPECT_TRUE(ar_copy_scales_key("dock:Time"));
  EXPECT_TRUE(ar_copy_scales_key("hidden:Displays"));
  EXPECT_FALSE(ar_copy_scales_key("video:/camera/image"));
  EXPECT_FALSE(ar_copy_scales_key("dock:"));
  EXPECT_FALSE(ar_copy_scales_key(""));
  EXPECT_FALSE(ar_copy_scales_key("hidden:Other"));
  EXPECT_FALSE(ar_copy_scales_key("qar_control_panel"));
}

TEST(ArCopyScale, WordsRoundTrip)
{
  using qar_rviz_streaming::ar_copy_size_from_word;
  for (const ArCopySize size : {ArCopySize::Enlarged, ArCopySize::Native}) {
    ArCopySize read = size == ArCopySize::Enlarged ? ArCopySize::Native : ArCopySize::Enlarged;
    ASSERT_TRUE(ar_copy_size_from_word(qar_rviz_streaming::to_string(size), read));
    EXPECT_EQ(read, size);
  }
  EXPECT_STREQ(qar_rviz_streaming::to_string(ArCopySize::Enlarged), "enlarged");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ArCopySize::Native), "native");
  ArCopySize read = ArCopySize::Native;
  EXPECT_TRUE(ar_copy_size_from_word("  Enlarged\t", read));
  EXPECT_EQ(read, ArCopySize::Enlarged);
  EXPECT_TRUE(ar_copy_size_from_word("NATIVE", read));
  EXPECT_EQ(read, ArCopySize::Native);
  read = ArCopySize::Enlarged;
  EXPECT_FALSE(ar_copy_size_from_word("huge", read));
  EXPECT_FALSE(ar_copy_size_from_word("", read));
  EXPECT_EQ(read, ArCopySize::Enlarged);
}

TEST(ArCopyScale, TheLogLineFormat)
{
  using qar_rviz_streaming::ar_copy_scale_line;
  using qar_rviz_streaming::ar_copy_switch_line;
  ArCopyScale scale;
  scale.factor = 2.0;
  scale.render_scale = 2.0;
  scale.source = ArCopyScaleSource::Setting;
  EXPECT_EQ(
    ar_copy_scale_line("dock:Views", scale),
    "AR PANEL: 'dock:Views' size in AR: enlarged x2.00 (setting)");
  scale.source = ArCopyScaleSource::PanelChoice;
  scale.factor = 1.5;
  EXPECT_EQ(
    ar_copy_scale_line("qar_streaming_panel", scale),
    "AR PANEL: 'qar_streaming_panel' size in AR: enlarged x1.50 (panel choice)");
  scale.source = ArCopyScaleSource::LimitedByHeight;
  scale.factor = 1.07;
  EXPECT_EQ(
    ar_copy_scale_line("hidden:Displays", scale),
    "AR PANEL: 'hidden:Displays' size in AR: enlarged x1.07 (limited to 1.8 m tall)");
  const ArCopyScale native =
    qar_rviz_streaming::ar_copy_scale(ArCopySize::Native, 2.0, 1.0f, false);
  EXPECT_EQ(
    ar_copy_scale_line("dock:Time", native), "AR PANEL: 'dock:Time' size in AR: native (setting)");
  const ArCopyScale native_own =
    qar_rviz_streaming::ar_copy_scale(ArCopySize::Native, 2.0, 1.0f, true);
  EXPECT_EQ(
    ar_copy_scale_line("dock:Time", native_own),
    "AR PANEL: 'dock:Time' size in AR: native (panel choice)");

  scale.factor = 2.0;
  scale.source = ArCopyScaleSource::Setting;
  EXPECT_EQ(
    ar_copy_switch_line("dock:Views", native, false),
    "AR PANEL: 'dock:Views' switched to native in AR: the copy is resized in place");
  EXPECT_EQ(
    ar_copy_switch_line("dock:Views", scale, true),
    "AR PANEL: 'dock:Views' switched to enlarged x2.00 in AR: the copy is resized in place - a "
    "sharper copy follows through the resize ring");
  EXPECT_EQ(
    ar_copy_switch_line("dock:Views", scale, false),
    "AR PANEL: 'dock:Views' switched to enlarged x2.00 in AR: the copy is resized in place");
}

TEST(ArCopyScale, TheScaleOfADialogIsTheWholePanelScaleAndItsLineSaysSo)
{
  using qar_rviz_streaming::ar_dialog_scale_line;
  // A dialog is enlarged as a whole at the factor of the setting, the metres and the capture
  // together, whatever copy opened it.
  const ArCopyScale enlarged = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, qar_rviz_streaming::kArCopyScaleDefault, 0.4f, 0.8f, false);
  EXPECT_DOUBLE_EQ(enlarged.factor, 2.0);
  EXPECT_DOUBLE_EQ(enlarged.render_scale, 2.0);
  EXPECT_EQ(enlarged.method, qar_rviz_streaming::ArCopyMethod::WholeCopy);
  EXPECT_EQ(
    ar_dialog_scale_line("Add", "hidden:Displays", enlarged),
    "AR PANEL: dialog 'Add' opened from 'hidden:Displays' size in AR: enlarged x2.00 (setting)");

  // Native is one, and a dialog that would stand over 1.8 m is enlarged less and says so.
  const ArCopyScale native =
    qar_rviz_streaming::ar_copy_scale(ArCopySize::Native, 2.0, 0.4f, 0.8f, false);
  EXPECT_DOUBLE_EQ(native.factor, 1.0);
  EXPECT_DOUBLE_EQ(native.render_scale, 1.0);
  EXPECT_EQ(
    ar_dialog_scale_line("Select Color", "dock:Views", native),
    "AR PANEL: dialog 'Select Color' opened from 'dock:Views' size in AR: native (setting)");
  const ArCopyScale tall = qar_rviz_streaming::ar_copy_scale(
    ArCopySize::Enlarged, 2.0, 1.2f, 2.4f, false);
  EXPECT_EQ(tall.source, ArCopyScaleSource::LimitedByHeight);
  EXPECT_LT(tall.factor, 2.0);
  EXPECT_NE(
    ar_dialog_scale_line("Big", "dock:Views", tall).find("(limited to 1.8 m tall)"),
    std::string::npos);
}

TEST(ArCopyScale, ADialogDrawnWithTheStyleOfACopyIsStreamedAtTheSizeOfThatCopy)
{
  using qar_rviz_streaming::ArCopyMethod;
  using qar_rviz_streaming::ArDialogScaleInputs;
  using qar_rviz_streaming::ar_dialog_scale;
  using qar_rviz_streaming::ar_dialog_scale_line;

  // The style reaches the window (RViz parents its colour picker into the panel): the controls
  // are x2 already, so the panel takes the overall factor of the copies, never the whole-panel 2.
  ArDialogScaleInputs styled;
  styled.styled = true;
  styled.control_factor = 2.0;
  styled.native_height_m = 0.5f;
  styled.height_at_wanted_m = 0.75f;
  const ArCopyScale copy = ar_dialog_scale(styled);
  EXPECT_EQ(copy.method, ArCopyMethod::EnlargedControls);
  EXPECT_DOUBLE_EQ(copy.factor, qar_rviz_streaming::kArCopyOverallScaleDefault);
  EXPECT_DOUBLE_EQ(copy.render_scale, qar_rviz_streaming::kArCopyOverallScaleDefault);
  EXPECT_DOUBLE_EQ(copy.control_factor, 2.0);
  EXPECT_EQ(copy.source, ArCopyScaleSource::Setting);

  // Whatever the setting and the owner's size say: the window decides.
  ArDialogScaleInputs native_owner = styled;
  native_owner.size = ArCopySize::Native;
  EXPECT_DOUBLE_EQ(ar_dialog_scale(native_owner).factor, copy.factor);
  ArDialogScaleInputs chosen = styled;
  chosen.panel_choice = true;
  EXPECT_EQ(ar_dialog_scale(chosen).source, ArCopyScaleSource::PanelChoice);

  // The same dialog from an owner that is not drawn with the style: the whole-panel factor.
  ArDialogScaleInputs plain;
  plain.native_height_m = 0.5f;
  plain.height_at_wanted_m = 1.0f;
  const ArCopyScale whole = ar_dialog_scale(plain);
  EXPECT_EQ(whole.method, ArCopyMethod::WholeCopy);
  EXPECT_DOUBLE_EQ(whole.factor, 2.0);
  EXPECT_DOUBLE_EQ(whole.render_scale, 2.0);
  plain.size = ArCopySize::Native;
  EXPECT_DOUBLE_EQ(ar_dialog_scale(plain).factor, 1.0);
  EXPECT_DOUBLE_EQ(ar_dialog_scale(plain).render_scale, 1.0);

  // A dialog that would stand over 1.8 m is enlarged less under either rule.
  ArDialogScaleInputs tall = styled;
  tall.native_height_m = 1.0f;
  tall.height_at_wanted_m = 2.0f;
  const ArCopyScale limited = ar_dialog_scale(tall);
  EXPECT_EQ(limited.source, ArCopyScaleSource::LimitedByHeight);
  EXPECT_LT(limited.factor, copy.factor);
  EXPECT_GE(limited.factor, 1.0);

  // The lines say which rule applied.
  EXPECT_EQ(
    ar_dialog_scale_line("Select Color", "dock:Displays", copy, 1.25),
    "AR PANEL: dialog 'Select Color' opened from 'dock:Displays' size in AR: copy x1.50, text "
    "x1.25 (styled by 'dock:Displays')");
  EXPECT_EQ(
    ar_dialog_scale_line("Select Color", "dock:Displays", copy, 0.0),
    "AR PANEL: dialog 'Select Color' opened from 'dock:Displays' size in AR: copy x1.50 (styled "
    "by 'dock:Displays')");
  const std::string tall_line = ar_dialog_scale_line("Big", "dock:Views", limited, 1.25);
  EXPECT_NE(tall_line.find(", limited to 1.8 m tall)"), std::string::npos) << tall_line;
  EXPECT_NE(tall_line.find("(styled by 'dock:Views', "), std::string::npos) << tall_line;
  EXPECT_EQ(
    ar_dialog_scale_line("Add", "dock:Time", whole, 1.25),
    "AR PANEL: dialog 'Add' opened from 'dock:Time' size in AR: enlarged x2.00 (setting)")
    << "the text factor belongs to the first rule only";
}

TEST(ArCopyScale, AControlsCopyAtTheDefaultsKeepsTheOverallMetres)
{
  using qar_rviz_streaming::ArCopyMethod;
  const ArCopyScale scale = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Enlarged, qar_rviz_streaming::kArCopyScaleDefault,
    qar_rviz_streaming::kArCopyOverallScaleDefault, 0.9f, 0.0f, false);
  EXPECT_EQ(scale.method, ArCopyMethod::EnlargedControls);
  EXPECT_DOUBLE_EQ(scale.control_factor, 2.0);
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::kArCopyOverallScaleDefault, 1.5);
  EXPECT_DOUBLE_EQ(scale.factor, 1.5);
  EXPECT_DOUBLE_EQ(scale.render_scale, 1.5);
  EXPECT_EQ(scale.source, ArCopyScaleSource::Setting);

  // At the overall factor 1 the metres and the render scale are the native ones.
  const ArCopyScale plain = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Enlarged, 2.0, 1.0, 0.9f, 0.0f, true);
  EXPECT_DOUBLE_EQ(plain.factor, 1.0);
  EXPECT_DOUBLE_EQ(plain.render_scale, 1.0);
  EXPECT_DOUBLE_EQ(plain.control_factor, 2.0);
  EXPECT_EQ(plain.source, ArCopyScaleSource::PanelChoice);
}

TEST(ArCopyScale, ANativeControlsCopyHasNoStyle)
{
  using qar_rviz_streaming::ArCopyMethod;
  const ArCopyScale scale = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Native, 2.0, 1.5, 0.9f, 1.3f, false);
  EXPECT_EQ(scale.method, ArCopyMethod::EnlargedControls);
  EXPECT_DOUBLE_EQ(scale.control_factor, 1.0);
  EXPECT_DOUBLE_EQ(scale.factor, 1.0);
  EXPECT_DOUBLE_EQ(scale.render_scale, 1.0);
  EXPECT_EQ(scale.source, ArCopyScaleSource::Setting);
  EXPECT_EQ(qar_rviz_streaming::ar_copy_scale_words(scale), "native");
  const ArCopyScale own = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Native, 2.0, 1.5, 0.9f, 1.3f, true);
  EXPECT_EQ(own.source, ArCopyScaleSource::PanelChoice);
}

TEST(ArCopyScale, TheOverallFactorIsLoweredUnderTheCap)
{
  // A video of 1.0 m at factor 1 with a frame of 0.1 m: 1.1 m native, 1.6 m at 1.5, 2.1 m at 2.
  const float native = height_at(1.0, 1.0f, 0.1f);
  const ArCopyScale fits = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Enlarged, 2.0, 1.5, native, height_at(1.5, 1.0f, 0.1f), false);
  EXPECT_DOUBLE_EQ(fits.factor, 1.5);
  EXPECT_EQ(fits.source, ArCopyScaleSource::Setting);

  const ArCopyScale limited = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Enlarged, 2.0, 2.0, native, height_at(2.0, 1.0f, 0.1f), false);
  EXPECT_EQ(limited.source, ArCopyScaleSource::LimitedByHeight);
  EXPECT_LT(limited.factor, 2.0);
  EXPECT_GT(limited.factor, 1.0);
  EXPECT_LE(height_at(limited.factor, 1.0f, 0.1f), qar_rviz_streaming::kArCopyMaxHeightM + 1e-4f);
  EXPECT_DOUBLE_EQ(limited.render_scale, limited.factor);
  // The limit touches the metres only: the controls keep their factor.
  EXPECT_DOUBLE_EQ(limited.control_factor, 2.0);

  // A copy already taller than the cap at factor 1 is not enlarged at all.
  const ArCopyScale tall = qar_rviz_streaming::ar_copy_controls_scale(
    ArCopySize::Enlarged, 2.0, 1.5, 1.9f, 2.7f, false);
  EXPECT_DOUBLE_EQ(tall.factor, 1.0);
  EXPECT_DOUBLE_EQ(tall.control_factor, 2.0);
}

TEST(ArCopyScale, TheControlFactorIsClamped)
{
  using qar_rviz_streaming::ar_copy_controls_scale;
  const auto controls_copy = [](double control, double overall) {
      return ar_copy_controls_scale(ArCopySize::Enlarged, control, overall, 0.5f, 0.0f, false);
    };
  EXPECT_DOUBLE_EQ(controls_copy(0.2, 1.0).control_factor, qar_rviz_streaming::kArCopyScaleMin);
  EXPECT_DOUBLE_EQ(controls_copy(9.0, 1.0).control_factor, qar_rviz_streaming::kArCopyScaleMax);
  EXPECT_DOUBLE_EQ(controls_copy(1.75, 1.0).control_factor, 1.75);
  EXPECT_DOUBLE_EQ(
    controls_copy(std::numeric_limits<double>::quiet_NaN(), 1.0).control_factor,
    qar_rviz_streaming::kArCopyScaleDefault);
  EXPECT_DOUBLE_EQ(
    controls_copy(std::numeric_limits<double>::infinity(), 1.0).control_factor,
    qar_rviz_streaming::kArCopyScaleDefault);
  // The overall factor is clamped to its own range; a value that is not a number is its default.
  EXPECT_DOUBLE_EQ(controls_copy(2.0, 0.3).factor, qar_rviz_streaming::kArCopyOverallScaleMin);
  EXPECT_DOUBLE_EQ(controls_copy(2.0, 7.0).factor, qar_rviz_streaming::kArCopyOverallScaleMax);
  EXPECT_DOUBLE_EQ(
    controls_copy(2.0, std::numeric_limits<double>::quiet_NaN()).factor,
    qar_rviz_streaming::kArCopyOverallScaleDefault);
}

TEST(ArCopyScale, TheWordsOfAControlsCopy)
{
  using qar_rviz_streaming::ar_copy_controls_scale;
  using qar_rviz_streaming::ar_copy_scale_line;
  using qar_rviz_streaming::ar_copy_scale_words;
  const ArCopyScale at_one =
    ar_copy_controls_scale(ArCopySize::Enlarged, 2.0, 1.0, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_scale_words(at_one), "controls x2.00");
  EXPECT_EQ(
    ar_copy_scale_line("dock:Views", at_one),
    "AR PANEL: 'dock:Views' size in AR: controls x2.00 (setting)");
  const ArCopyScale larger =
    ar_copy_controls_scale(ArCopySize::Enlarged, 2.0, 1.25, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_scale_words(larger), "controls x2.00, copy x1.25");
  // Controls at factor 1 and larger metres: only the copy is larger.
  const ArCopyScale metres_only =
    ar_copy_controls_scale(ArCopySize::Enlarged, 1.0, 1.5, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_scale_words(metres_only), "copy x1.50");
  const ArCopyScale nothing =
    ar_copy_controls_scale(ArCopySize::Enlarged, 1.0, 1.0, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_scale_words(nothing), "native");
  // The words of a whole copy are unchanged.
  EXPECT_EQ(
    ar_copy_scale_words(qar_rviz_streaming::ar_copy_scale(ArCopySize::Enlarged, 2.0, 0.5f, false)),
    "enlarged x2.00");
}

TEST(ArCopyScale, TheSwitchLineOfAControlsCopy)
{
  using qar_rviz_streaming::ar_copy_controls_scale;
  using qar_rviz_streaming::ar_copy_switch_line;
  const ArCopyScale enlarged =
    ar_copy_controls_scale(ArCopySize::Enlarged, 2.0, 1.0, 0.5f, 0.0f, false);
  const std::string tail =
    " in AR: the AR copy is drawn again with its controls at that size and fitted again; its "
    "stream follows through the resize ring";
  EXPECT_EQ(
    ar_copy_switch_line("dock:Views", enlarged, false),
    "AR PANEL: 'dock:Views' switched to controls x2.00" + tail);
  // The flag of a whole copy changes nothing for a controls copy.
  EXPECT_EQ(
    ar_copy_switch_line("dock:Views", enlarged, true),
    ar_copy_switch_line("dock:Views", enlarged, false));
  const ArCopyScale native =
    ar_copy_controls_scale(ArCopySize::Native, 2.0, 1.5, 0.5f, 0.0f, false);
  EXPECT_EQ(
    ar_copy_switch_line("hidden:Displays", native, false),
    "AR PANEL: 'hidden:Displays' switched to native" + tail);
  // A whole copy keeps its line.
  const ArCopyScale whole =
    qar_rviz_streaming::ar_copy_scale(ArCopySize::Enlarged, 2.0, 0.5f, false);
  EXPECT_EQ(
    ar_copy_switch_line("qar_streaming_panel", whole, true),
    "AR PANEL: 'qar_streaming_panel' switched to enlarged x2.00 in AR: the copy is resized in "
    "place - a sharper copy follows through the resize ring");
}

TEST(ArCopyScale, TheCaptions)
{
  using qar_rviz_streaming::ar_copy_caption;
  using qar_rviz_streaming::ar_copy_controls_scale;
  const ArCopyScale controls =
    ar_copy_controls_scale(ArCopySize::Enlarged, 2.0, 1.5, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_caption(controls, ArCopySize::Enlarged), "controls 2x");
  const ArCopyScale fractional =
    ar_copy_controls_scale(ArCopySize::Enlarged, 2.5, 1.5, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_caption(fractional, ArCopySize::Enlarged), "controls 2.5x");
  const ArCopyScale quarter =
    ar_copy_controls_scale(ArCopySize::Enlarged, 1.25, 1.5, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_caption(quarter, ArCopySize::Enlarged), "controls 1.25x");
  const ArCopyScale whole =
    qar_rviz_streaming::ar_copy_scale(ArCopySize::Enlarged, 2.0, 0.5f, false);
  EXPECT_EQ(ar_copy_caption(whole, ArCopySize::Enlarged), "2x");
  EXPECT_EQ(ar_copy_caption(controls, ArCopySize::Native), "native");
  EXPECT_EQ(ar_copy_caption(whole, ArCopySize::Native), "native");
  const ArCopyScale native =
    ar_copy_controls_scale(ArCopySize::Native, 2.0, 1.5, 0.5f, 0.0f, false);
  EXPECT_EQ(ar_copy_caption(native, ArCopySize::Native), "native");
}

TEST(ArCopyScale, TheItemTextAndTheToolTips)
{
  using qar_rviz_streaming::ar_copy_enlarged_item_text;
  EXPECT_EQ(ar_copy_enlarged_item_text(2.0), "Enlarged controls (2x)");
  EXPECT_EQ(ar_copy_enlarged_item_text(2.5), "Enlarged controls (2.5x)");
  EXPECT_EQ(ar_copy_enlarged_item_text(1.25), "Enlarged controls (1.25x)");
  EXPECT_EQ(ar_copy_enlarged_item_text(1.0), "Enlarged controls (1x)");
  EXPECT_NE(
    std::string(qar_rviz_streaming::kArCopyRowToolTip).find("text keeps its size"),
    std::string::npos);
  EXPECT_NE(
    std::string(qar_rviz_streaming::kArCopyButtonToolTip).find("enlarged controls"),
    std::string::npos);
}

TEST(ArCopyScale, TheTextScaleIsOneAndAQuarterAndTheEnvironmentIsClamped)
{
  using qar_rviz_streaming::ar_copy_text_scale;
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::kArCopyTextScale, 1.25);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale(nullptr), 1.25);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale(""), 1.25);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("  "), 1.25);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("abc"), 1.25) << "not a number";
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("1.5x"), 1.25) << "trailing text";
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("nan"), 1.25);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("inf"), 1.25);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("1.5"), 1.5);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale(" 1.75 "), 1.75);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("0.5"), 1.0) << "below the range";
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("-3"), 1.0);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("2"), 2.0);
  EXPECT_DOUBLE_EQ(ar_copy_text_scale("9"), 2.0) << "above the range";
}

TEST(ArCopyScale, TheHintingIsNoneUnlessTheEnvironmentSaysDefault)
{
  using qar_rviz_streaming::ArCopyHinting;
  using qar_rviz_streaming::ar_copy_font_hinting;
  EXPECT_EQ(ar_copy_font_hinting(nullptr), ArCopyHinting::None);
  EXPECT_EQ(ar_copy_font_hinting(""), ArCopyHinting::None);
  EXPECT_EQ(ar_copy_font_hinting("none"), ArCopyHinting::None);
  EXPECT_EQ(ar_copy_font_hinting("whatever"), ArCopyHinting::None);
  EXPECT_EQ(ar_copy_font_hinting("default"), ArCopyHinting::Default);
  EXPECT_EQ(ar_copy_font_hinting("  Default "), ArCopyHinting::Default);
}

TEST(ArCopyScale, TheTextLineNamesTheScaleAndTheHinting)
{
  using qar_rviz_streaming::ArCopyHinting;
  EXPECT_EQ(
    qar_rviz_streaming::ar_copy_text_line("dock:Views", 1.25, ArCopyHinting::None),
    "AR PANEL: 'dock:Views' copy text x1.25, hinting none");
  EXPECT_EQ(
    qar_rviz_streaming::ar_copy_text_line("qar_streaming_panel", 1.0, ArCopyHinting::Default),
    "AR PANEL: 'qar_streaming_panel' copy text x1.00, hinting default");
}
