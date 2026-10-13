// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__HTML_ESCAPE_HPP_
#define QAR_UI_MODEL__HTML_ESCAPE_HPP_

/// @file html_escape.hpp
/// @brief The one escaping routine every untrusted string passes through on
/// its way into the page.
///
/// A ROS topic name, a message-type name, a widget label derived from a field
/// name, a string field's value: none of them may end up as markup. There is
/// deliberately ONE function for text content and attribute values alike — it
/// escapes the quote characters too — so a call site cannot pick the weaker
/// variant by mistake. C0 control characters (other than tab/newline/return)
/// and ill-formed UTF-8 are replaced by U+FFFD so the document stays
/// well-formed whatever bytes a publisher sends.

#include <string>
#include <string_view>

namespace qar_ui_model
{

/// @brief Escape `&`, `<`, `>`, `"`, `'`; replace control characters and
/// ill-formed UTF-8 with U+FFFD. Safe for element text and quoted attributes.
std::string html_escape(std::string_view text);

void html_escape_to(std::string & out, std::string_view text);

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__HTML_ESCAPE_HPP_
