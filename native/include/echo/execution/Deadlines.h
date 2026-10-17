#pragma once

namespace echo::execution {
inline constexpr long kDeadlineSongUrlMs = 10000;
inline constexpr long kDeadlineImageMs = 8000;
inline constexpr long kDeadlineLoginPollMs = 10000;
inline constexpr long kDeadlineSearchMs = 12000;
inline constexpr long kDeadlinePlaylistMs = 12000;
inline constexpr long kDeadlineGenericMs = 12000;
inline constexpr long kDeadlineLoginKeyMs = 12000;
inline constexpr long kDeadlineSignatureFamilyProbeMs = 60000;
inline constexpr long kSignatureFamilyProbeBudgetMs = 45000;
inline constexpr long kFrontendTimeoutMs = 14000;
}
