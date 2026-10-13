// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Manifest parsing + message-type -> Display class lookup. Fixture XML mirrors
// the real rviz_default_plugins/plugins_description.xml shape, so these run with
// no plugin environment.

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>

#include "qar_rviz_streaming/display_class_catalog.hpp"

using qar_rviz_streaming::DisplayClassCatalog;
using qar_rviz_streaming::format_video_display_health;
using qar_rviz_streaming::is_image_message_type;
using qar_rviz_streaming::is_video_display_class;
using qar_rviz_streaming::normalize_message_type;
using qar_rviz_streaming::parse_display_manifest;
using qar_rviz_streaming::should_skip_display_for_exposed_topic;
using qar_rviz_streaming::video_display_class_caveat;

namespace
{

// Shape copied from the installed rviz_default_plugins manifest: a <library>
// root, classes keyed by a `name` lookup id, one or more <message_type> children.
const char * kManifest = R"XML(
<library path="rviz_default_plugins">
  <class
    name="rviz_default_plugins/PointCloud2"
    type="rviz_default_plugins::displays::PointCloud2Display"
    base_class_type="rviz_common::Display">
    <description>Displays a point cloud.</description>
    <message_type>sensor_msgs/msg/PointCloud2</message_type>
  </class>
  <class
    name="rviz_default_plugins/Image"
    type="rviz_default_plugins::displays::ImageDisplay"
    base_class_type="rviz_common::Display">
    <description>Displays an image.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
    <message_type>sensor_msgs/msg/CompressedImage</message_type>
  </class>
  <class
    name="rviz_default_plugins/Axes"
    type="rviz_default_plugins::displays::AxesDisplay"
    base_class_type="rviz_common::Display">
    <description>Not a topic display at all.</description>
  </class>
</library>
)XML";

// Shape copied from the installed rviz_default_plugins manifest: both Image and
// Camera declare sensor_msgs/msg/Image, and "Camera" sorts alphabetically before
// "Image" — the trap best_class_for_message_type must not fall into.
const char * kImageAndCameraManifest = R"XML(
<library path="rviz_default_plugins">
  <class
    name="rviz_default_plugins/Camera"
    type="rviz_default_plugins::displays::CameraDisplay"
    base_class_type="rviz_common::Display">
    <description>Displays a camera.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
  </class>
  <class
    name="rviz_default_plugins/Image"
    type="rviz_default_plugins::displays::ImageDisplay"
    base_class_type="rviz_common::Display">
    <description>Displays an image.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
    <message_type>sensor_msgs/msg/CompressedImage</message_type>
  </class>
</library>
)XML";

// The real rviz_default_plugins manifest's sensor_msgs/Image family, class for
// class (plugins_description.xml of rviz_default_plugins): THREE displays accept
// sensor_msgs/msg/Image, and only Camera lists CompressedImage. "DepthCloud"
// sorts between "Camera" and "Image" - with Camera demoted it would be the
// alphabetical first, which would give a colour video exposed as an AR tool a
// DepthCloud display.
const char * kRealImageFamilyManifest = R"XML(
<library path="rviz_default_plugins">
  <class
    name="rviz_default_plugins/Camera"
    type="rviz_default_plugins::displays::CameraDisplay"
    base_class_type="rviz_common::Display">
    <description>Displays an image from a camera.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
    <message_type>sensor_msgs/msg/CompressedImage</message_type>
  </class>
  <class
    name="rviz_default_plugins/DepthCloud"
    type="rviz_default_plugins::displays::DepthCloudDisplay"
    base_class_type="rviz_common::Display">
    <description>Displays point clouds based on depth maps.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
  </class>
  <class
    name="rviz_default_plugins/Image"
    type="rviz_default_plugins::displays::ImageDisplay"
    base_class_type="rviz_common::Display">
    <description>Displays an image.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
  </class>
</library>
)XML";

// One manifest declaring just the named class, so a test can register classes in
// any order (and any subset) across separate add_manifest calls.
QString single_class_manifest(const QString & class_name, const QString & message_type)
{
  return QString(
    "<library path=\"p\"><class name=\"%1\" base_class_type=\"rviz_common::Display\">"
    "<message_type>%2</message_type></class></library>").arg(class_name, message_type);
}

}  // namespace

TEST(NormalizeMessageType, CollapsesTheRos2MsgInfix)
{
  EXPECT_EQ(normalize_message_type("sensor_msgs/msg/PointCloud2"), "sensor_msgs/PointCloud2");
}

TEST(NormalizeMessageType, LeavesTheShortFormAlone)
{
  EXPECT_EQ(normalize_message_type("sensor_msgs/PointCloud2"), "sensor_msgs/PointCloud2");
}

TEST(NormalizeMessageType, TrimsAndHandlesEmpty)
{
  EXPECT_EQ(normalize_message_type("  sensor_msgs/msg/Image  "), "sensor_msgs/Image");
  EXPECT_EQ(normalize_message_type(""), "");
}

TEST(NormalizeMessageType, DoesNotMangleAnInfixThatIsNotMsg)
{
  EXPECT_EQ(normalize_message_type("pkg/srv/Thing"), "pkg/srv/Thing");
}

TEST(ParseManifest, ReadsEveryClassIncludingOnesWithNoMessageType)
{
  const auto parsed = parse_display_manifest(kManifest);
  EXPECT_EQ(parsed.size(), 3);
  ASSERT_TRUE(parsed.contains("rviz_default_plugins/Axes"));
  // Present but empty: "not a topic display" must be distinguishable from
  // "manifest was never read".
  EXPECT_TRUE(parsed.value("rviz_default_plugins/Axes").isEmpty());
}

TEST(ParseManifest, CollectsAllMessageTypesOfAClass)
{
  const auto parsed = parse_display_manifest(kManifest);
  const auto image = parsed.value("rviz_default_plugins/Image");
  EXPECT_EQ(image.size(), 2);
  EXPECT_TRUE(image.contains("sensor_msgs/Image"));
  EXPECT_TRUE(image.contains("sensor_msgs/CompressedImage"));
}

TEST(ParseManifest, FallsBackToTheTypeAttributeWhenThereIsNoLookupName)
{
  const char * xml = R"XML(
    <library path="p">
      <class type="some_ns::SomeDisplay" base_class_type="rviz_common::Display">
        <message_type>std_msgs/msg/String</message_type>
      </class>
    </library>)XML";

  const auto parsed = parse_display_manifest(xml);
  ASSERT_TRUE(parsed.contains("some_ns::SomeDisplay"));
  EXPECT_TRUE(parsed.value("some_ns::SomeDisplay").contains("std_msgs/String"));
}

TEST(ParseManifest, HandlesAClassLibrariesWrapperAroundSeveralLibraries)
{
  const char * xml = R"XML(
    <class_libraries>
      <library path="a">
        <class name="a/One" base_class_type="rviz_common::Display">
          <message_type>std_msgs/msg/String</message_type>
        </class>
      </library>
      <library path="b">
        <class name="b/Two" base_class_type="rviz_common::Display">
          <message_type>std_msgs/msg/Int32</message_type>
        </class>
      </library>
    </class_libraries>)XML";

  const auto parsed = parse_display_manifest(xml);
  EXPECT_EQ(parsed.size(), 2);
  EXPECT_TRUE(parsed.contains("a/One"));
  EXPECT_TRUE(parsed.contains("b/Two"));
}

TEST(ParseManifest, MalformedXmlContributesNothingRatherThanPartialGarbage)
{
  const char * broken = "<library><class name=\"x/Y\"><message_type>std_msgs/msg/String";
  EXPECT_TRUE(parse_display_manifest(broken).isEmpty());
}

TEST(ParseManifest, EmptyInputIsEmptyOutput)
{
  EXPECT_TRUE(parse_display_manifest("").isEmpty());
}

TEST(Catalog, LooksUpADisplayClassForATopicType)
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(kManifest);

  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/msg/PointCloud2"),
    "rviz_default_plugins/PointCloud2");
}

TEST(Catalog, AcceptsEitherSpellingOfTheLookupType)
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(kManifest);

  // The manifest declared the ROS 2 form; a caller asking with the short form
  // must still get a hit, and vice versa.
  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/PointCloud2"),
    "rviz_default_plugins/PointCloud2");
}

TEST(Catalog, ReportsEveryClassAcceptingATypeSorted)
{
  const char * second = R"XML(
    <library path="other">
      <class name="aaa_plugins/Cloud" base_class_type="rviz_common::Display">
        <message_type>sensor_msgs/msg/PointCloud2</message_type>
      </class>
    </library>)XML";

  DisplayClassCatalog catalog;
  catalog.add_manifest(kManifest);
  catalog.add_manifest(second);

  const auto classes = catalog.classes_for_message_type("sensor_msgs/msg/PointCloud2");
  ASSERT_EQ(classes.size(), 2);
  EXPECT_EQ(classes[0], "aaa_plugins/Cloud") << "must be sorted for a stable dialog";
  EXPECT_EQ(classes[1], "rviz_default_plugins/PointCloud2");
}

TEST(Catalog, PrefersImageOverCameraForSensorMsgsImage)
{
  // Camera must not be picked just because 'C' < 'I': Camera without
  // camera_info/TF is always empty, while Image only needs the topic.
  DisplayClassCatalog catalog;
  catalog.add_manifest(kImageAndCameraManifest);

  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/msg/Image"), "rviz_default_plugins/Image");
}

TEST(Catalog, ImagePreferenceIsIndependentOfManifestRegistrationOrder)
{
  DisplayClassCatalog forward;
  forward.add_manifest(kImageAndCameraManifest);
  EXPECT_EQ(forward.best_class_for_message_type("sensor_msgs/Image"), "rviz_default_plugins/Image");

  // classes_for_message_type is a QSet under the hood before sorting — registering
  // manifests in the other order (or, as here, a manifest with Image declared
  // first) must not change the winner.
  const char * image_first = R"XML(
    <library path="p">
      <class name="rviz_default_plugins/Image" base_class_type="rviz_common::Display">
        <message_type>sensor_msgs/msg/Image</message_type>
      </class>
      <class name="rviz_default_plugins/Camera" base_class_type="rviz_common::Display">
        <message_type>sensor_msgs/msg/Image</message_type>
      </class>
    </library>)XML";
  DisplayClassCatalog reordered;
  reordered.add_manifest(image_first);
  EXPECT_EQ(
    reordered.best_class_for_message_type("sensor_msgs/Image"), "rviz_default_plugins/Image");
}

TEST(Catalog, CameraStillWinsWhenItIsTheOnlyOptionForTheType)
{
  const char * camera_only = R"XML(
    <library path="p">
      <class name="rviz_default_plugins/Camera" base_class_type="rviz_common::Display">
        <message_type>sensor_msgs/msg/Image</message_type>
      </class>
    </library>)XML";
  DisplayClassCatalog catalog;
  catalog.add_manifest(camera_only);

  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/Image"), "rviz_default_plugins/Camera");
}

TEST(Catalog, PreferenceRankDoesNotDisturbUnrelatedAlphabeticalTies)
{
  // classes_for_message_type's contract (used directly by the picker dialog) does
  // not depend on the preference rank: with no Image/Camera involved, plain
  // alphabetical order holds, and best_class_for_message_type picks the
  // alphabetical first.
  const char * second = R"XML(
    <library path="other">
      <class name="aaa_plugins/Cloud" base_class_type="rviz_common::Display">
        <message_type>sensor_msgs/msg/PointCloud2</message_type>
      </class>
    </library>)XML";
  DisplayClassCatalog catalog;
  catalog.add_manifest(kManifest);
  catalog.add_manifest(second);

  EXPECT_EQ(catalog.best_class_for_message_type("sensor_msgs/PointCloud2"), "aaa_plugins/Cloud");
}

TEST(Catalog, AnUnknownTypeYieldsNothing)
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(kManifest);

  EXPECT_TRUE(catalog.classes_for_message_type("nav_msgs/msg/Odometry").isEmpty());
  EXPECT_TRUE(catalog.best_class_for_message_type("nav_msgs/msg/Odometry").isEmpty());
}

TEST(Catalog, StartsEmptyAndReportsWhatItIndexed)
{
  DisplayClassCatalog catalog;
  EXPECT_TRUE(catalog.empty());

  catalog.add_manifest(kManifest);
  EXPECT_FALSE(catalog.empty());
  // PointCloud2, Image, CompressedImage — Axes declared no type.
  EXPECT_EQ(catalog.type_count(), 3);
}

// --- is_video_display_class / video_display_class_caveat (variant (b): the
// Qt-dock substitute for RViz's uncapturable native Image/Camera render
// window) -----------------------------------------------------------------

TEST(IsVideoDisplayClass, TrueForImage)
{
  EXPECT_TRUE(is_video_display_class("rviz_default_plugins/Image"));
}

TEST(IsVideoDisplayClass, TrueForCamera)
{
  EXPECT_TRUE(is_video_display_class("rviz_default_plugins/Camera"));
}

TEST(IsVideoDisplayClass, FalseForOtherDisplayClasses)
{
  EXPECT_FALSE(is_video_display_class("rviz_default_plugins/Grid"));
  EXPECT_FALSE(is_video_display_class("rviz_default_plugins/PointCloud2"));
  EXPECT_FALSE(is_video_display_class(""));
}

TEST(IsVideoDisplayClass, IsNotFooledByAPartialOrCaseVariantMatch)
{
  // Exact-match only — no accidental hits on a substring or a different case.
  EXPECT_FALSE(is_video_display_class("rviz_default_plugins/ImageTransport"));
  EXPECT_FALSE(is_video_display_class("Image"));
  EXPECT_FALSE(is_video_display_class("rviz_default_plugins/image"));
  EXPECT_FALSE(is_video_display_class("rviz_default_plugins/CAMERA"));
}

TEST(VideoDisplayClassCaveat, EmptyForImage)
{
  EXPECT_TRUE(video_display_class_caveat("rviz_default_plugins/Image").empty());
}

TEST(VideoDisplayClassCaveat, MentionsTheOverlayForCamera)
{
  const std::string caveat = video_display_class_caveat("rviz_default_plugins/Camera");
  EXPECT_FALSE(caveat.empty());
  EXPECT_NE(caveat.find("overlay"), std::string::npos);
}

TEST(VideoDisplayClassCaveat, EmptyForAnythingNotVideo)
{
  EXPECT_TRUE(video_display_class_caveat("rviz_default_plugins/Grid").empty());
  EXPECT_TRUE(video_display_class_caveat("").empty());
}

// --- should_skip_display_for_exposed_topic ("Expose as a QAR AR tool" must not
// also create a redundant/empty Image or Camera dock) -------------------------

TEST(ShouldSkipDisplayForExposedTopic, SkipsImageWhenExposed)
{
  EXPECT_TRUE(should_skip_display_for_exposed_topic(true, "rviz_default_plugins/Image"));
}

TEST(ShouldSkipDisplayForExposedTopic, SkipsCameraWhenExposed)
{
  EXPECT_TRUE(should_skip_display_for_exposed_topic(true, "rviz_default_plugins/Camera"));
}

TEST(ShouldSkipDisplayForExposedTopic, KeepsImageWhenNotExposed)
{
  // Only exposure suppresses a display; what gets created otherwise is the
  // catalog's pick.
  EXPECT_FALSE(should_skip_display_for_exposed_topic(false, "rviz_default_plugins/Image"));
  EXPECT_FALSE(should_skip_display_for_exposed_topic(false, "rviz_default_plugins/Camera"));
}

TEST(ShouldSkipDisplayForExposedTopic, KeepsNonVideoDisplaysEvenWhenExposed)
{
  // e.g. a PointCloud2 topic exposed as an AR tool: PointCloud2's dock has no
  // associated native render widget in conflict with the AR panel, so it still
  // gets created alongside the exposure.
  EXPECT_FALSE(should_skip_display_for_exposed_topic(true, "rviz_default_plugins/PointCloud2"));
}

TEST(ShouldSkipDisplayForExposedTopic, FalseForAnEmptyDisplayClass)
{
  // A topic whose type no display handles at all (display_class empty): nothing
  // to skip, exposed or not.
  EXPECT_FALSE(should_skip_display_for_exposed_topic(true, ""));
  EXPECT_FALSE(should_skip_display_for_exposed_topic(false, ""));
}

// --- DepthCloud is never the pick for a colour video topic -------------------

TEST(Catalog, NeverPicksDepthCloudForASensorMsgsImageColourVideo)
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(kRealImageFamilyManifest);

  const QString best = catalog.best_class_for_message_type("sensor_msgs/msg/Image");
  EXPECT_EQ(best, "rviz_default_plugins/Image");
  EXPECT_NE(best, "rviz_default_plugins/DepthCloud");
  // Same answer for the short spelling the catalog also accepts.
  EXPECT_EQ(catalog.best_class_for_message_type("sensor_msgs/Image"), "rviz_default_plugins/Image");
}

TEST(Catalog, ImageBeatsCameraAndDepthCloudWhateverTheRegistrationOrder)
{
  const QString names[] = {
    "rviz_default_plugins/Camera", "rviz_default_plugins/DepthCloud", "rviz_default_plugins/Image"};
  // Every permutation of the three, each class in its own manifest, in that order.
  int order[] = {0, 1, 2};
  int permutations = 0;
  do {
    DisplayClassCatalog catalog;
    for (const int index : order) {
      catalog.add_manifest(single_class_manifest(names[index], "sensor_msgs/msg/Image"));
    }
    EXPECT_EQ(
      catalog.best_class_for_message_type("sensor_msgs/msg/Image"), "rviz_default_plugins/Image")
      << "permutation " << permutations;
    ++permutations;
  } while (std::next_permutation(std::begin(order), std::end(order)));
  EXPECT_EQ(permutations, 6);
}

TEST(Catalog, ThePickerStillListsDepthCloudAlphabetically)
{
  // Only the pre-selected "best" changes; classes_for_message_type (the dialog's
  // stable, alphabetical listing) must keep offering every accepting class.
  DisplayClassCatalog catalog;
  catalog.add_manifest(kRealImageFamilyManifest);

  const QStringList classes = catalog.classes_for_message_type("sensor_msgs/msg/Image");
  ASSERT_EQ(classes.size(), 3);
  EXPECT_EQ(classes[0], "rviz_default_plugins/Camera");
  EXPECT_EQ(classes[1], "rviz_default_plugins/DepthCloud");
  EXPECT_EQ(classes[2], "rviz_default_plugins/Image");
}

TEST(Catalog, CameraBeatsDepthCloudWhenImageIsNotInstalled)
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(
    single_class_manifest("rviz_default_plugins/DepthCloud", "sensor_msgs/msg/Image"));
  catalog.add_manifest(
    single_class_manifest("rviz_default_plugins/Camera", "sensor_msgs/msg/Image"));

  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/msg/Image"), "rviz_default_plugins/Camera");
}

TEST(Catalog, DepthCloudIsStillTheLastResortWhenItIsTheOnlyOption)
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(
    single_class_manifest("rviz_default_plugins/DepthCloud", "sensor_msgs/msg/Image"));

  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/msg/Image"),
    "rviz_default_plugins/DepthCloud");
}

TEST(Catalog, CompressedImageMapsToCameraAsInTheRealManifest)
{
  // Only Camera declares CompressedImage in the real manifest; documenting the
  // current mapping (an exposed CompressedImage topic creates no display at all -
  // see ShouldSkipDisplayForExposedTopic below - so this only matters for the
  // not-exposed path).
  DisplayClassCatalog catalog;
  catalog.add_manifest(kRealImageFamilyManifest);

  EXPECT_EQ(
    catalog.best_class_for_message_type("sensor_msgs/msg/CompressedImage"),
    "rviz_default_plugins/Camera");
}

TEST(IsImageMessageType, TrueForBothImageTypesInEitherSpelling)
{
  EXPECT_TRUE(is_image_message_type("sensor_msgs/msg/Image"));
  EXPECT_TRUE(is_image_message_type("sensor_msgs/Image"));
  EXPECT_TRUE(is_image_message_type("sensor_msgs/msg/CompressedImage"));
  EXPECT_TRUE(is_image_message_type("sensor_msgs/CompressedImage"));
  EXPECT_TRUE(is_image_message_type("  sensor_msgs/msg/Image  ")) << "trimmed like the catalog does";
}

TEST(IsImageMessageType, FalseForEverythingElse)
{
  EXPECT_FALSE(is_image_message_type(""));
  EXPECT_FALSE(is_image_message_type("sensor_msgs/msg/CameraInfo"));
  EXPECT_FALSE(is_image_message_type("sensor_msgs/msg/PointCloud2"));
  EXPECT_FALSE(is_image_message_type("sensor_msgs/msg/ImageMarker"));
  EXPECT_FALSE(is_image_message_type("visualization_msgs/msg/ImageMarker"));
  EXPECT_FALSE(is_image_message_type("other_pkg/msg/Image")) << "the package matters";
  EXPECT_FALSE(is_image_message_type("sensor_msgs/msg/image")) << "type names are case-sensitive";
  EXPECT_FALSE(is_image_message_type("rviz_default_plugins/Image")) << "a display class, not a type";
}

TEST(ShouldSkipDisplayForExposedTopic, ExposedImageTopicGetsNoDisplayWhateverTheCatalogPicked)
{
  // The gap the message-type overload closes: a DepthCloud candidate, which the
  // class-only overload does not skip.
  EXPECT_FALSE(should_skip_display_for_exposed_topic(true, "rviz_default_plugins/DepthCloud"))
    << "documents the gap in the two-argument decision";
  EXPECT_TRUE(
    should_skip_display_for_exposed_topic(
      true, "sensor_msgs/msg/Image", "rviz_default_plugins/DepthCloud"));
  EXPECT_TRUE(
    should_skip_display_for_exposed_topic(
      true, "sensor_msgs/msg/Image", "rviz_default_plugins/Image"));
  EXPECT_TRUE(
    should_skip_display_for_exposed_topic(
      true, "sensor_msgs/msg/Image", "rviz_default_plugins/Camera"));
  EXPECT_TRUE(
    should_skip_display_for_exposed_topic(
      true, "sensor_msgs/msg/CompressedImage", "rviz_default_plugins/Camera"));
  // Even when no display class was found for it.
  EXPECT_TRUE(should_skip_display_for_exposed_topic(true, "sensor_msgs/msg/Image", ""));
  EXPECT_TRUE(should_skip_display_for_exposed_topic(true, "sensor_msgs/CompressedImage", ""));
}

TEST(ShouldSkipDisplayForExposedTopic, NonExposedImageTopicStillGetsItsDisplay)
{
  EXPECT_FALSE(
    should_skip_display_for_exposed_topic(
      false, "sensor_msgs/msg/Image", "rviz_default_plugins/Image"));
  EXPECT_FALSE(
    should_skip_display_for_exposed_topic(
      false, "sensor_msgs/msg/Image", "rviz_default_plugins/DepthCloud"));
  EXPECT_FALSE(
    should_skip_display_for_exposed_topic(
      false, "sensor_msgs/msg/CompressedImage", "rviz_default_plugins/Camera"));
}

TEST(ShouldSkipDisplayForExposedTopic, ExposedNonImageTopicKeepsItsDisplay)
{
  EXPECT_FALSE(
    should_skip_display_for_exposed_topic(
      true, "sensor_msgs/msg/PointCloud2", "rviz_default_plugins/PointCloud2"));
  EXPECT_FALSE(should_skip_display_for_exposed_topic(true, "nav_msgs/msg/Odometry", ""));
  EXPECT_FALSE(
    should_skip_display_for_exposed_topic(
      false, "sensor_msgs/msg/PointCloud2", "rviz_default_plugins/PointCloud2"));
}

TEST(FormatVideoDisplayHealth, MatchedPublisherButNothingReceivedIsWaiting)
{
  // The silent-failure shape: RViz's own node sees the publisher, the display's
  // Topic row still reads "OK" (it only changes once a frame is taken).
  EXPECT_EQ(
    format_video_display_health("Image", "/qar_test_video/image_raw", 1, "OK", true),
    "Image{/qar_test_video/image_raw pubs=1 WAITING}");
}

TEST(FormatVideoDisplayHealth, ReceivedFramesAreRx)
{
  EXPECT_EQ(
    format_video_display_health("Image", "/cam/image_raw", 1, "1203 messages received at 29.9 hz.",
      true),
    "Image{/cam/image_raw pubs=1 RX}");
  // A frame count beats a stale publisher count: the display clearly works.
  EXPECT_EQ(
    format_video_display_health("Camera", "/cam/image_raw", 0, "5 messages received at 1.0 hz.",
      true),
    "Camera{/cam/image_raw pubs=0 RX}");
}

TEST(FormatVideoDisplayHealth, NoPublisherSeenByRvizIsNopub)
{
  EXPECT_EQ(
    format_video_display_health("Image", "/cam/image_raw", 0, "OK", true),
    "Image{/cam/image_raw pubs=0 NOPUB}");
}

TEST(FormatVideoDisplayHealth, SubscribeErrorIsErr)
{
  EXPECT_EQ(
    format_video_display_health("Image", "/cam/image_raw", 1, "Error subscribing: bad transport",
      true),
    "Image{/cam/image_raw pubs=1 ERR}");
}

TEST(FormatVideoDisplayHealth, AnUnreadableStatusRowIsUnknownNotAFalseAlarm)
{
  EXPECT_EQ(
    format_video_display_health("Camera", "/cam/image_raw", 1, "", true),
    "Camera{/cam/image_raw pubs=1 UNKNOWN}");
  EXPECT_EQ(
    format_video_display_health("Camera", "/cam/image_raw", 0, "", true),
    "Camera{/cam/image_raw pubs=0 UNKNOWN}");
}

TEST(FormatVideoDisplayHealth, DisabledAndTopiclessDisplaysSayWhy)
{
  EXPECT_EQ(
    format_video_display_health("Image", "/cam/image_raw", 1, "OK", false),
    "Image{/cam/image_raw OFF}");
  EXPECT_EQ(format_video_display_health("Image", "", 0, "", true), "Image{NOTOPIC}");
  // No topic wins over disabled: nothing to report beyond the missing topic.
  EXPECT_EQ(format_video_display_health("Image", "", 0, "", false), "Image{NOTOPIC}");
}

TEST(ShouldSkipDisplayForExposedTopic, ThreeArgumentFormKeepsTheVideoClassRule)
{
  // Superset of the two-argument rule: an Image/Camera display class is skipped
  // when exposed even if the type string is something unexpected.
  EXPECT_TRUE(
    should_skip_display_for_exposed_topic(
      true, "custom_msgs/msg/Frame", "rviz_default_plugins/Image"));
  EXPECT_FALSE(
    should_skip_display_for_exposed_topic(
      false, "custom_msgs/msg/Frame", "rviz_default_plugins/Image"));
}
