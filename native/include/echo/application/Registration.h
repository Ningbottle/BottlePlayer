#pragma once
#include "echo/ports/AccountStateStore.h"
#include <functional>

namespace echo::application {
struct RegistrationResult {
  model::CommitResult commit;
  model::DeviceInfo device;
};
// Provider owns protocol/transport. Application owns the decision to commit its
// registration product against the identity used to send it.
inline RegistrationResult RegisterDevice(ports::AccountStateStore& store,
    const model::IdentitySnapshot& identity, const execution::OperationContext& operation,
    const std::function<std::string()>& registerUpstream) {
  if (!operation.CanContinue()) return {
      operation.Reason() == execution::StopReason::Deadline ? model::CommitResult::DeadlineBeforeCommit : model::CommitResult::CancelledBeforeCommit,
      identity.device};
  const auto dfid = registerUpstream();
  if (dfid.empty()) return {model::CommitResult::UpstreamFailure, identity.device};
  const auto result = store.CommitRegistration(identity.version, {dfid}, operation);
  auto device = identity.device;
  if (result == model::CommitResult::Applied) { device.dfid = dfid; device.registered = true; }
  return {result, std::move(device)};
}
}
