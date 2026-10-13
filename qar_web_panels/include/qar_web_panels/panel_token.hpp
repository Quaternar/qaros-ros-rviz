// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_WEB_PANELS__PANEL_TOKEN_HPP_
#define QAR_WEB_PANELS__PANEL_TOKEN_HPP_

/// @file panel_token.hpp
/// @brief The per-panel capability token that gates a panel's URL.
///
/// The server has to be reachable from the headset, so it listens on a LAN
/// interface, where "knows host:port" is everyone on the network. A panel is
/// therefore addressed by an unguessable 128-bit token in its URL path —
/// page and WebSocket alike — minted from the OS CSPRNG. See
/// docs/ARCHITECTURE.md, "Security posture", for the threat model and residual
/// risk.

#include <string>
#include <string_view>

namespace qar_web_panels
{

/// @brief 32 lowercase hex characters = 128 random bits from QRandomGenerator::system().
std::string generate_panel_token();

/// @brief Exactly 32 characters of `[0-9a-f]`.
bool is_well_formed_token(std::string_view token);

/// @brief Constant-time comparison (for equal lengths), so a lookup does not
/// leak how many leading characters of a guess were right.
bool tokens_equal(std::string_view a, std::string_view b);

}  // namespace qar_web_panels

#endif  // QAR_WEB_PANELS__PANEL_TOKEN_HPP_
