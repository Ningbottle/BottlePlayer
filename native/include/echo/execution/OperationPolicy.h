#pragma once
#include "echo/execution/Deadlines.h"
#include <string_view>

namespace echo::execution {
enum class ExecutionClass { SongUrl, Search, Playlist, LoginPoll, Image, Generic, LoginKey };
enum class PathMatch { Exact, Prefix };
enum class Availability { Always, Debug };
enum class CancelPolicy { Cooperative, FinishAcceptedControl };
enum class RetryPolicy { ReadOnlyTransport, NeverAutomatic };
enum class CommitPolicy { Read, Identity, Control, Mutation };
struct OperationPolicy {
  std::string_view path;
  PathMatch match;
  ExecutionClass executionClass;
  long deadlineMs;
  unsigned methods;
  Availability availability;
  CommitPolicy commitPolicy;
  CancelPolicy cancelPolicy;
  RetryPolicy retryPolicy;
};
inline constexpr OperationPolicy operationPolicies[] = {
#define ECHO_OPERATION(path, match, kind, deadline, methods, available, commit, cancel, retry) \
  {path, PathMatch::match, ExecutionClass::kind, deadline, methods, Availability::available, CommitPolicy::commit, CancelPolicy::cancel, RetryPolicy::retry},
#include "echo/execution/OperationPolicies.h"
#undef ECHO_OPERATION
};
inline const OperationPolicy& PolicyForPath(std::string_view path) {
  for (const auto& policy : operationPolicies) {
#ifdef NDEBUG
    if (policy.availability == Availability::Debug) continue;
#endif
    if ((policy.match == PathMatch::Exact && path == policy.path) ||
        (policy.match == PathMatch::Prefix && path.starts_with(policy.path))) return policy;
  }
  static constexpr OperationPolicy fallback{"", PathMatch::Prefix, ExecutionClass::Generic, kDeadlineGenericMs, 3, Availability::Always, CommitPolicy::Read, CancelPolicy::Cooperative, RetryPolicy::ReadOnlyTransport};
  return fallback;
}
}
