// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the graph browser dialog (add_topic_dialog.hpp), offscreen. The dialog is handed a
// listing and a display class catalog and hands back a choice; these tests select entities in
// its tree and press its buttons, and check the choice and the texts the person sees.

#include "qar_rviz_streaming/add_topic_dialog.hpp"

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace
{

using qar_rviz_streaming::AddTopicDialog;
using qar_rviz_streaming::AddTopicSelection;
using qar_rviz_streaming::DisplayClassCatalog;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosGraphEntry;

const char * kManifest = R"XML(
<library path="rviz_default_plugins">
  <class name="rviz_default_plugins/Pose"
    type="rviz_default_plugins::displays::PoseDisplay" base_class_type="rviz_common::Display">
    <description>Displays a pose.</description>
    <message_type>geometry_msgs/msg/PoseStamped</message_type>
  </class>
  <class name="rviz_default_plugins/PointCloud2"
    type="rviz_default_plugins::displays::PointCloud2Display"
    base_class_type="rviz_common::Display">
    <description>Displays a point cloud.</description>
    <message_type>sensor_msgs/msg/PointCloud2</message_type>
  </class>
  <class name="rviz_default_plugins/Image"
    type="rviz_default_plugins::displays::ImageDisplay" base_class_type="rviz_common::Display">
    <description>Displays an image.</description>
    <message_type>sensor_msgs/msg/Image</message_type>
  </class>
</library>
)XML";

RosGraphEntry entry(RosEntityKind kind, const char * name, const char * type)
{
  RosGraphEntry e;
  e.kind = kind;
  e.name = name;
  e.types = QStringList{QString(type)};
  return e;
}

std::vector<RosGraphEntry> graph()
{
  return {
    entry(RosEntityKind::Topic, "/switch", "std_msgs/msg/Bool"),
    entry(RosEntityKind::Topic, "/cmd_vel", "geometry_msgs/msg/Twist"),
    entry(RosEntityKind::Topic, "/temperature", "std_msgs/msg/Float64"),
    entry(RosEntityKind::Topic, "/goal", "geometry_msgs/msg/PoseStamped"),
    entry(RosEntityKind::Topic, "/points", "sensor_msgs/msg/PointCloud2"),
    entry(RosEntityKind::Topic, "/camera/image", "sensor_msgs/msg/Image"),
    entry(RosEntityKind::Topic, "/camera/image_raw2", "sensor_msgs/msg/CompressedImage"),
    entry(RosEntityKind::Service, "/trigger", "std_srvs/srv/Trigger"),
    entry(RosEntityKind::Action, "/fibonacci", "example_interfaces/action/Fibonacci"),
  };
}

QTreeWidgetItem * find_item(QTreeWidgetItem * item, const QString & name)
{
  if (item->data(0, Qt::UserRole + 1).toString() == name) {
    return item;
  }
  for (int i = 0; i < item->childCount(); ++i) {
    if (QTreeWidgetItem * hit = find_item(item->child(i), name)) {
      return hit;
    }
  }
  return nullptr;
}

DisplayClassCatalog make_catalog()
{
  DisplayClassCatalog catalog;
  catalog.add_manifest(kManifest);
  return catalog;
}

/// The dialog under test plus the handles the tests press.
struct Fixture
{
  DisplayClassCatalog catalog;
  AddTopicDialog dialog;
  QTreeWidget * tree = nullptr;
  QCheckBox * publish = nullptr;
  QPushButton * add = nullptr;
  QPushButton * display_only = nullptr;
  QLabel * detail = nullptr;

  explicit Fixture(std::vector<RosGraphEntry> entries = graph())
  : catalog(make_catalog()),
    dialog(std::move(entries), catalog)
  {
    tree = dialog.findChild<QTreeWidget *>();
    publish = dialog.findChild<QCheckBox *>();
    detail = dialog.findChild<QLabel *>("add_topic_detail");
    for (QPushButton * button : dialog.findChildren<QPushButton *>()) {
      if (button->text() == "Add") {
        add = button;
      } else if (button->text() == "Add display only") {
        display_only = button;
      }
    }
  }

  bool select(const QString & name)
  {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
      if (QTreeWidgetItem * item = find_item(tree->topLevelItem(i), name)) {
        tree->setCurrentItem(item);
        return true;
      }
    }
    return false;
  }

  const AddTopicSelection & selection() const { return dialog.selection(); }
  QString text() const { return detail->text(); }
};

}  // namespace

TEST(AddTopicDialog, AddExposesWithoutTickingAnything)
{
  Fixture f;
  ASSERT_NE(f.add, nullptr);
  ASSERT_NE(f.display_only, nullptr);
  ASSERT_TRUE(f.select("/temperature"));
  // Nothing is ticked and the box of a number is off: a read-only AR tool, but a tool.
  EXPECT_TRUE(f.add->isEnabled());
  f.add->click();
  EXPECT_EQ(f.dialog.result(), static_cast<int>(QDialog::Accepted));
  EXPECT_EQ(f.selection().name, QString("/temperature"));
  EXPECT_TRUE(f.selection().expose_as_ar_tool);
  EXPECT_FALSE(f.selection().allow_publish);

  // A switch publishes by default: one click on Add, still nothing ticked by hand.
  Fixture g;
  ASSERT_TRUE(g.select("/switch"));
  g.add->click();
  EXPECT_EQ(g.dialog.result(), static_cast<int>(QDialog::Accepted));
  EXPECT_TRUE(g.selection().expose_as_ar_tool);
  EXPECT_TRUE(g.selection().allow_publish);
  EXPECT_FALSE(g.selection().write_chosen);
}

TEST(AddTopicDialog, ThePublishBoxStartsInTheRulesState)
{
  Fixture f;
  ASSERT_NE(f.publish, nullptr);
  ASSERT_TRUE(f.select("/switch"));
  EXPECT_TRUE(f.publish->isEnabled());
  EXPECT_TRUE(f.publish->isChecked());
  ASSERT_TRUE(f.select("/cmd_vel"));
  EXPECT_TRUE(f.publish->isChecked());
  ASSERT_TRUE(f.select("/goal"));
  EXPECT_TRUE(f.publish->isEnabled());
  EXPECT_FALSE(f.publish->isChecked());
  ASSERT_TRUE(f.select("/temperature"));
  EXPECT_FALSE(f.publish->isChecked());
  // A service and an action have their own forms: the box is off and cannot be changed.
  ASSERT_TRUE(f.select("/trigger"));
  EXPECT_FALSE(f.publish->isEnabled());
  EXPECT_FALSE(f.publish->isChecked());
  ASSERT_TRUE(f.select("/fibonacci"));
  EXPECT_FALSE(f.publish->isEnabled());
  // Back on a switch: the default again.
  ASSERT_TRUE(f.select("/switch"));
  EXPECT_TRUE(f.publish->isChecked());
}

TEST(AddTopicDialog, AChangedBoxSurvivesARefresh)
{
  Fixture f;
  ASSERT_TRUE(f.select("/goal"));
  f.publish->setChecked(true);  // a pose is read-only by default: the person asked for more
  EXPECT_TRUE(f.selection().allow_publish);
  EXPECT_TRUE(f.selection().write_chosen);

  // The Refresh button hands a fresh listing in; the same entity is selected again.
  f.dialog.setEntries(graph());
  EXPECT_EQ(f.selection().name, QString("/goal"));
  EXPECT_TRUE(f.publish->isChecked());
  EXPECT_TRUE(f.selection().allow_publish);
  EXPECT_TRUE(f.selection().write_chosen);

  // The same for a switch the person turned off.
  ASSERT_TRUE(f.select("/switch"));
  f.publish->setChecked(false);
  f.dialog.setEntries(graph());
  EXPECT_FALSE(f.publish->isChecked());
  EXPECT_TRUE(f.selection().write_chosen);

  // Another selection returns the box to the default of its type.
  ASSERT_TRUE(f.select("/temperature"));
  EXPECT_FALSE(f.publish->isChecked());
  EXPECT_FALSE(f.selection().write_chosen);
  ASSERT_TRUE(f.select("/switch"));
  EXPECT_TRUE(f.publish->isChecked());
  EXPECT_FALSE(f.selection().write_chosen);
}

TEST(AddTopicDialog, AddDisplayOnlyGeneratesNoTool)
{
  Fixture f;
  ASSERT_TRUE(f.select("/points"));
  EXPECT_TRUE(f.display_only->isEnabled());
  f.display_only->click();
  EXPECT_EQ(f.dialog.result(), static_cast<int>(QDialog::Accepted));
  EXPECT_FALSE(f.selection().expose_as_ar_tool);
  EXPECT_FALSE(f.selection().allow_publish);
  EXPECT_EQ(f.selection().display_class, QString("rviz_default_plugins/PointCloud2"));
  EXPECT_FALSE(f.text().contains("AR tool"));
  EXPECT_TRUE(f.text().contains("Will create display: rviz_default_plugins/PointCloud2"));

  // A switch that would publish by default: display only means no tool and no publication.
  Fixture g;
  ASSERT_TRUE(g.select("/switch"));
  EXPECT_TRUE(g.publish->isChecked());
  g.display_only->click();
  EXPECT_FALSE(g.selection().expose_as_ar_tool);
  EXPECT_FALSE(g.selection().allow_publish);
  EXPECT_FALSE(g.selection().write_chosen);
}

TEST(AddTopicDialog, ServicesAndActionsHaveNoDisplayOnlyButton)
{
  Fixture f;
  ASSERT_NE(f.display_only, nullptr);
  ASSERT_TRUE(f.select("/trigger"));
  EXPECT_FALSE(f.display_only->isVisibleTo(&f.dialog));
  EXPECT_TRUE(f.selection().expose_as_ar_tool);
  ASSERT_TRUE(f.select("/fibonacci"));
  EXPECT_FALSE(f.display_only->isVisibleTo(&f.dialog));
  EXPECT_TRUE(f.selection().expose_as_ar_tool);
  ASSERT_TRUE(f.select("/switch"));
  EXPECT_TRUE(f.display_only->isVisibleTo(&f.dialog));
  EXPECT_TRUE(f.display_only->isEnabled());

  // An image topic: a display exists for sensor_msgs/Image, none for CompressedImage here.
  ASSERT_TRUE(f.select("/camera/image"));
  EXPECT_TRUE(f.display_only->isEnabled());
  ASSERT_TRUE(f.select("/camera/image_raw2"));
  EXPECT_FALSE(f.display_only->isEnabled());
  EXPECT_TRUE(f.add->isEnabled());
}

TEST(AddTopicDialog, TheDetailLinesNameTheReason)
{
  Fixture f;
  ASSERT_TRUE(f.select("/switch"));
  EXPECT_TRUE(f.text().contains(
      "Will generate an AR tool that can publish (Publish asks for a second press): one switch"))
    << f.text().toStdString();
  ASSERT_TRUE(f.select("/cmd_vel"));
  EXPECT_TRUE(f.text().contains(
      "Will generate an AR tool that can publish (Publish asks for a second press): "
      "sliders and a stop button"));
  ASSERT_TRUE(f.select("/temperature"));
  EXPECT_TRUE(f.text().contains(
      "Will generate a read-only AR tool: a number needs a keyboard"));
  ASSERT_TRUE(f.select("/goal"));
  EXPECT_TRUE(f.text().contains("Will create display: rviz_default_plugins/Pose"));
  EXPECT_TRUE(f.text().contains(
      "Will generate a read-only AR tool: coordinates need a keyboard; use Picking to place "
      "a pose"));
  // The person ticks the box of a pose: the line says that the default was changed.
  f.publish->setChecked(true);
  EXPECT_TRUE(f.text().contains("(Publish asks for a second press): coordinates need"));
  EXPECT_TRUE(f.text().contains("(changed from the default)"));
  // Services and actions name their form.
  ASSERT_TRUE(f.select("/trigger"));
  EXPECT_TRUE(f.text().contains("Will generate an AR tool with its request form."));
  ASSERT_TRUE(f.select("/fibonacci"));
  EXPECT_TRUE(f.text().contains("Will generate an AR tool with its goal form."));
}
