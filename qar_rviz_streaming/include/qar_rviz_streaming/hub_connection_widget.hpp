// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__HUB_CONNECTION_WIDGET_HPP_
#define QAR_RVIZ_STREAMING__HUB_CONNECTION_WIDGET_HPP_

/// @file hub_connection_widget.hpp
/// @brief The "QAROS Hub" group of the Connection tab: the Cancel button of a join in progress,
/// the identity and state lines and the list of targets. Qt Widgets only: no RViz, no ROS, no
/// QAROS.
///
/// The plugin joins the Hub of this PC as a local app. The Hub of another PC is reached by making
/// the Hub of this PC a member of it; the plugin has no pairing of its own.
///
/// The widget holds no session logic. The host gives it a @ref ConnectionView (connection_view.hpp)
/// and the rows of the target list, and hears about what the person does through signals. Every
/// label that changes at run time is width neutral (it never asks its layout for more room).

#include <string>
#include <vector>

#include <QGroupBox>
#include <QString>

#include "qar_rviz_streaming/connection_view.hpp"

class QLabel;
class QListWidget;
class QPushButton;

namespace qar_rviz_streaming
{

/// @brief One row of the target list.
struct HubTargetRow
{
  std::string name;      ///< Device name or display name.
  std::string category;  ///< For example "Headset".
  std::string presence;  ///< "online", "unknown", ...
  bool served = true;
  std::string not_served_reason;
};

/// @brief The text of one row.
std::string hub_target_row_text(const HubTargetRow & row);

class HubConnectionWidget : public QGroupBox
{
  Q_OBJECT

public:
  explicit HubConnectionWidget(QWidget * parent = nullptr);

  /// @brief Shows @p view: the state and identity lines and the state of the Cancel button.
  void apply(const ConnectionView & view);

  /// @brief Replaces the rows of the target list.
  void set_targets(const std::vector<HubTargetRow> & rows);

  // The controls, for tests.
  QPushButton * cancel_button() const { return cancel_button_; }
  QLabel * identity_label() const { return identity_label_; }
  QLabel * state_label() const { return state_label_; }
  QListWidget * target_list() const { return target_list_; }

Q_SIGNALS:
  void cancelRequested();

private:
  QPushButton * cancel_button_ = nullptr;
  QLabel * identity_label_ = nullptr;
  QLabel * state_label_ = nullptr;
  QListWidget * target_list_ = nullptr;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__HUB_CONNECTION_WIDGET_HPP_
