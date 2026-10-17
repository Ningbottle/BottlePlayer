#pragma once
#include "echo/execution/Deadlines.h"
#include "echo/core/HttpClient.h"
namespace echo::core {
using execution::kDeadlineSongUrlMs;
using execution::kDeadlineImageMs;
using execution::kDeadlineLoginPollMs;
using execution::kDeadlineSearchMs;
using execution::kDeadlinePlaylistMs;
using execution::kDeadlineGenericMs;
using execution::kDeadlineLoginKeyMs;
using execution::kDeadlineSignatureFamilyProbeMs;
using execution::kSignatureFamilyProbeBudgetMs;
using execution::kFrontendTimeoutMs;
static_assert(kDeadlineLoginKeyMs >= kHttpGetDefaultBudgetMs);
}
