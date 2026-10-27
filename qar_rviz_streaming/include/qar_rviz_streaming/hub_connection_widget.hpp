// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__HUB_CONNECTION_WIDGET_HPP_
#define QAR_RVIZ_STREAMING__HUB_CONNECTION_WIDGET_HPP_

/// @file hub_connection_widget.hpp
/// @brief The "QAROS Hub" group of the Connection tab: the choice of the Hub, the host, port and
/// pairing code of another PC's Hub, the Cancel and Forget buttons, the identity and state lines
/// and the list of targets. Qt Widgets only: no RViz, no ROS, no QAROS.
///
/// The widget holds no session logic. The host gives it a @ref ConnectionView (connection_view.hpp)
/// and the rows of the target list, and hears about what the person does through signals. Every
/// label that changes at run time is width neutral (it never asks its layout for more room), the
/// forget confirmation runs on a member timer (a child of the group, so it ends with it), and the
/// pairing code is held in the line edit only: it is never copied into a setting, a log line or a
/// member.

#include <string>
#include <vector>

#include <QGroupBox>
#include <QString>

#include "qar_rviz_streaming/connection_view.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

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

  /// @brief Shows @p view: the state and identity lines, the visibility of the host and port,
  /// the state of the code field, the Pair, Cancel and Forget buttons.
  void apply(const ConnectionView & view);

  /// @brief Replaces the rows of the target list.
  void set_targets(const std::vector<HubTargetRow> & rows);

  /// @brief The setting words of the three controls. The setters change the control without a
  /// signal.
  void set_mode_word(const QString & word);
  QString mode_word() const;
  void set_host_text(const QString & text);
  QString host_text() const;
  void set_port_text(const QString & text);
  QString port_text() const;

  /// @brief Empties the pairing code.
  void clear_code();
  /// @brief Puts the Forget button back to its resting text and stops the confirmation.
  void reset_forget_confirmation();
  bool forget_confirmation_pending() const;
  /// @brief How long the second click of the Forget button counts, in milliseconds (5000).
  void set_forget_confirmation_ms(int milliseconds);

  // The controls, for the host's tool tips and "set by the environment" marks, and for tests.
  QComboBox * mode_combo() const { return mode_combo_; }
  QWidget * remote_fields() const { return remote_fields_; }
  QLineEdit * host_edit() const { return host_edit_; }
  QLineEdit * port_edit() const { return port_edit_; }
  QLineEdit * code_edit() const { return code_edit_; }
  QPushButton * pair_button() const { return pair_button_; }
  QPushButton * cancel_button() const { return cancel_button_; }
  QPushButton * forget_button() const { return forget_button_; }
  QLabel * identity_label() const { return identity_label_; }
  QLabel * state_label() const { return state_label_; }
  QListWidget * target_list() const { return target_list_; }

Q_SIGNALS:
  /// The person chose another Hub mode ("auto", "local" or "remote").
  void modeEdited(const QString & word);
  /// The person finished editing the host or the port field.
  void hostEdited(const QString & text);
  void portEdited(const QString & text);
  /// Pair was pressed or Enter was pressed in the code field, and the host and the port are
  /// usable. The code field has been emptied by the time the signal is delivered.
  void pairRequested(const QString & code, const QString & host, int port);
  /// Pair was pressed and the host or the port is not usable; the code stays in its field.
  void pairRefused(const QString & reason);
  void cancelRequested();
  /// The second click of the Forget button, within the confirmation time.
  void forgetConfirmed();

private:
  void submit_code();
  void on_forget_clicked();

  QComboBox * mode_combo_ = nullptr;
  QWidget * remote_fields_ = nullptr;
  QLabel * host_label_ = nullptr;
  QLineEdit * host_edit_ = nullptr;
  QLabel * port_label_ = nullptr;
  QLineEdit * port_edit_ = nullptr;
  QLineEdit * code_edit_ = nullptr;
  QPushButton * pair_button_ = nullptr;
  QPushButton * cancel_button_ = nullptr;
  QPushButton * forget_button_ = nullptr;
  QLabel * identity_label_ = nullptr;
  QLabel * state_label_ = nullptr;
  QListWidget * target_list_ = nullptr;
  QTimer * forget_timer_ = nullptr;
  /// The resting text and tool tip of the Forget button, as the last view gave them.
  QString forget_text_;
  QString forget_tool_tip_;
  bool forget_enabled_ = false;
  bool forget_pending_ = false;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__HUB_CONNECTION_WIDGET_HPP_
