// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__PANEL_DISPLAY_NAMES_HPP_
#define QAR_RVIZ_STREAMING__PANEL_DISPLAY_NAMES_HPP_

/// @file panel_display_names.hpp
/// @brief The display name a new QarOS panel is created under: the name that was asked for
/// the first time it is started, a numbered one every time after. Standard library only.
///
/// ## What this is for
///
/// With a Visualizer attached, the Visualizer crashed (an access violation inside its browser
/// engine) in every observed case in which the hidden Displays panel was created again after
/// it had been closed, with the title unchanged as well as with a numbered one. That is a
/// defect of the Visualizer. A new display name does not prevent that crash, and the restarted
/// Visualizer showed an empty or black scene now and then with a numbered title as well as with
/// the same title as before. The plugin hands out a numbered title as a precaution only. The
/// display name is the title shown in the headset and Visualizer.
///
/// ## The rule
///
///  * The first QarOS panel started under a name keeps that name.
///  * Every later start under the same name gets the next number after it: `<name> (2)`,
///    `<name> (3)`, and so on.
///  * The count belongs to the name the caller ASKED for, not to the name that was handed
///    out. A name that already ends in a number, `<name> (2)`, is simply another name with a
///    count of its own.
///  * Only the starts the caller reports are counted. A start that revives a retired panel
///    creates no QarOS panel, so the caller does not report it.
///
/// PanelDisplayNames is thread-safe. A host that creates QarOS panels from several places
/// keeps ONE object for the whole process, so that every panel counts against the same names.
///
/// See docs/TROUBLESHOOTING.md, "The Visualizer crashes when the hidden Displays panel is
/// started again".

#include <map>
#include <mutex>
#include <string>

namespace qar_rviz_streaming
{

/// @brief The display name for a panel that is started under @p base after @p earlier_uses
/// earlier starts under that same name.
///
/// @p base itself for no earlier start (@p earlier_uses <= 0), and for an empty @p base, which
/// is left to the panel manager (it titles such a panel with its key). Otherwise
/// `base + " (" + N + ")"` with N = @p earlier_uses + 1, so the second start is "(2)".
inline std::string numbered_display_name(const std::string & base, int earlier_uses)
{
  if (earlier_uses <= 0 || base.empty()) {
    return base;
  }
  return base + " (" + std::to_string(static_cast<long long>(earlier_uses) + 1) + ")";
}

/// @brief Counts, per display name, how many QarOS panels were started under it, and says
/// which name the next start has to use. Thread-safe. Not copyable, because it owns a mutex.
class PanelDisplayNames
{
public:
  PanelDisplayNames() = default;

  PanelDisplayNames(const PanelDisplayNames &) = delete;
  PanelDisplayNames & operator=(const PanelDisplayNames &) = delete;

  /// @brief Count one more start under exactly @p wanted and return the display name that
  /// start has to use: `numbered_display_name(wanted, n)` with n = the starts counted under
  /// @p wanted before this one. Names that differ in any character are different names, each
  /// with a count of its own.
  std::string claim(const std::string & wanted)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const int earlier = starts_[wanted]++;
    return numbered_display_name(wanted, earlier);
  }

  /// @brief How many starts were counted under exactly @p wanted; 0 when there is none.
  int started(const std::string & wanted) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = starts_.find(wanted);
    return found == starts_.end() ? 0 : found->second;
  }

private:
  mutable std::mutex mutex_;
  std::map<std::string, int> starts_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__PANEL_DISPLAY_NAMES_HPP_
