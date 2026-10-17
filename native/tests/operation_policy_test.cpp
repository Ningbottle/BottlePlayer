#include "echo/execution/OperationPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace echo::execution;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
int main() {
  try {
    for (const auto& policy : operationPolicies) {
      require(policy.path.starts_with('/'), "Policy path must be absolute");
      require(policy.deadlineMs > 0 && policy.deadlineMs <= 60000, "Invalid deadline");
      require(policy.methods > 0 && (policy.methods & ~7u) == 0, "Invalid method mask");
      for (const auto& other : operationPolicies) {
        if (&policy == &other) continue;
        require(policy.path != other.path || policy.match != other.match, "Duplicate policy");
      }
#ifdef NDEBUG
      if (policy.availability == Availability::Debug) continue;
#endif
      require(&PolicyForPath(policy.path) == &policy || policy.match == PathMatch::Prefix,
              "Exact policy shadowed by a prefix");
    }
    require(PolicyForPath("/song/url/next").executionClass == ExecutionClass::SongUrl, "Song prefix lost");
    require(PolicyForPath("/login/qr/check/next").executionClass == ExecutionClass::LoginPoll, "Specific login prefix lost");
    require(PolicyForPath("/login/qr/new").executionClass == ExecutionClass::LoginKey, "Login prefix lost");
    require(PolicyForPath("/unknown").executionClass == ExecutionClass::Generic, "Unknown fallback lost");
    require(PolicyForPath("/settings/device").commitPolicy == CommitPolicy::Identity, "Device commit policy lost");
    require(PolicyForPath("/auth/logout").cancelPolicy == CancelPolicy::FinishAcceptedControl, "Logout cancellation changed");
    require(PolicyForPath("/playlist/tracks/add").retryPolicy == RetryPolicy::NeverAutomatic, "Mutation became retryable");
#ifdef NDEBUG
    require(PolicyForPath("/diagnostics/signature-family").availability == Availability::Always, "Debug route exposed in Release");
#else
    require(PolicyForPath("/diagnostics/signature-family").availability == Availability::Debug, "Debug route unavailable");
#endif
    std::cout << "Operation policy contracts passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
