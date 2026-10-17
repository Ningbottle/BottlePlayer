#include "echo/application/Registration.h"
#include "echo/execution/OperationPolicy.h"
#include <stdexcept>

static void Check(bool condition) { if (!condition) throw std::runtime_error("operation contract failed"); }
struct FakeStore : echo::ports::AccountStateStore {
  echo::model::IdentitySnapshot identity;
  int writes = 0;
  echo::model::IdentitySnapshot CaptureIdentity() override { return identity; }
  echo::model::CommitResult CommitRegistration(echo::model::IdentityVersion expected,
      const echo::model::RegisteredDevicePatch& patch, const echo::execution::OperationContext& operation) override {
    using echo::model::CommitResult;
    if (expected.accountGeneration != identity.version.accountGeneration) return CommitResult::StaleAccount;
    if (expected.deviceRevision != identity.version.deviceRevision) return CommitResult::StaleDevice;
    if (!operation.TryCommit()) return CommitResult::CancelledBeforeCommit;
    identity.device.dfid = patch.dfid; ++identity.version.deviceRevision; ++writes;
    return CommitResult::Applied;
  }
};
int main() {
  using namespace echo;
  // This executable links only model/ports/execution/application interfaces.
  FakeStore store;
  store.identity.version = {4, 9};
  auto snapshot = store.CaptureIdentity();
  execution::OperationContext operation;
  auto result = application::RegisterDevice(store, snapshot, operation, [&] {
    ++store.identity.version.deviceRevision;
    return "old-dfid";
  });
  Check(result.commit == model::CommitResult::StaleDevice && store.writes == 0);
  operation.stop->Cancel();
  bool called = false;
  result = application::RegisterDevice(store, store.identity, operation, [&] { called = true; return "unused"; });
  Check(!called && result.commit == model::CommitResult::CancelledBeforeCommit);

  execution::OperationContext admitted;
  Check(admitted.TryCommit());
  admitted.stop->Cancel();
  // The accepted transaction may finish; a later admission must fail.
  Check(!admitted.TryCommit());
  execution::OperationContext expired;
  expired.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  Check(!expired.TryCommit() && expired.Reason() == execution::StopReason::Deadline);
  Check(execution::PolicyForPath("/login/qr/check").deadlineMs == 10000);
  Check(execution::PolicyForPath("/register/dev").methods == 4);
#ifndef NDEBUG
  Check(execution::PolicyForPath("/diagnostics/signature-family").deadlineMs == 60000);
#else
  Check(execution::PolicyForPath("/diagnostics/signature-family").deadlineMs == 12000);
#endif
}
