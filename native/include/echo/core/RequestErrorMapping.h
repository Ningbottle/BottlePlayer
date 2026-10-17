#pragma once

#include <string>
#include <string_view>

namespace echo::core {

struct CApiRequestErrorMapping {
  int httpStatus = 504;
  std::string errorCode;
};

// The scheduler uses a generic deadline token internally. At the C API
// boundary, that deadline is the caller's route budget and should keep the
// same reason as deadlines detected inside HttpClient.
inline std::string_view NormalizeCApiRequestError(std::string_view reason) {
  return reason == "job_deadline" ? "total_budget_exhausted" : reason;
}

// Request-control admission failures are local conflicts/capacity limits, not
// route deadline failures. Keep their status and stable token distinct so
// callers do not mistake them for a timed-out upstream request.
inline CApiRequestErrorMapping MapCApiRequestError(std::string_view reason) {
  if (reason == "scheduler_shutdown") {
    return {503, "scheduler_shutdown"};
  }
  if (reason == "request_control_overload") {
    return {503, "request_control_overload"};
  }
  if (reason == "duplicate_request_id") {
    return {409, "duplicate_request_id"};
  }
  return {};
}

}  // namespace echo::core
