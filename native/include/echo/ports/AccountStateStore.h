#pragma once
#include "echo/model/Identity.h"
#include "echo/execution/OperationContext.h"

namespace echo::ports {
class AccountStateStore {
 public:
  virtual ~AccountStateStore() = default;
  virtual model::IdentitySnapshot CaptureIdentity() = 0;
  virtual model::CommitResult CommitRegistration(model::IdentityVersion,
      const model::RegisteredDevicePatch&, const execution::OperationContext&) = 0;
};
}
