#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace echo::win32_app {

enum class ImageSlotStatus {
  Empty,
  Loading,
  Ready,
  Failed,
};

struct ImageSlotDecision {
  bool shouldStartLoad = false;
  std::string key;
};

struct ImageSlotPayload {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> bgra;
};

class ImageSlot {
 public:
  ImageSlotDecision Request(std::string key);
  void Complete(std::string key, ImageSlotPayload payload);
  void Fail(std::string key);
  void Clear();

  ImageSlotStatus Status() const;
  const std::string& Key() const;
  const ImageSlotPayload* Payload() const;

 private:
  std::string key_;
  ImageSlotStatus status_ = ImageSlotStatus::Empty;
  ImageSlotPayload payload_;
};

}  // namespace echo::win32_app
