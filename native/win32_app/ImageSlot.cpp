#include "echo/win32_app/ImageSlot.h"

#include <utility>

namespace echo::win32_app {

ImageSlotDecision ImageSlot::Request(std::string key) {
  if (key.empty()) {
    Clear();
    return {};
  }

  if (key_ == key && (status_ == ImageSlotStatus::Loading || status_ == ImageSlotStatus::Ready)) {
    return {};
  }

  key_ = std::move(key);
  payload_ = {};
  status_ = ImageSlotStatus::Loading;
  return {true, key_};
}

void ImageSlot::Complete(std::string key, ImageSlotPayload payload) {
  if (key != key_) {
    return;
  }

  if (payload.bgra.empty() || payload.width == 0 || payload.height == 0) {
    status_ = ImageSlotStatus::Failed;
    payload_ = {};
    return;
  }

  payload_ = std::move(payload);
  status_ = ImageSlotStatus::Ready;
}

void ImageSlot::Fail(std::string key) {
  if (key != key_) {
    return;
  }
  payload_ = {};
  status_ = ImageSlotStatus::Failed;
}

void ImageSlot::Clear() {
  key_.clear();
  status_ = ImageSlotStatus::Empty;
  payload_ = {};
}

ImageSlotStatus ImageSlot::Status() const {
  return status_;
}

const std::string& ImageSlot::Key() const {
  return key_;
}

const ImageSlotPayload* ImageSlot::Payload() const {
  return status_ == ImageSlotStatus::Ready ? &payload_ : nullptr;
}

}  // namespace echo::win32_app
