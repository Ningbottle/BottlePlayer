#pragma once

#include <filesystem>
#include <future>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/Dto.h"

namespace echo::core {

constexpr unsigned int kBackendEventMessage = 0x8000 + 0x4543;

enum class BackendEventKind {
  ApiCompleted,
  LoginStateChanged,
  PlaybackStateChanged,
  PlaybackProgress,
  OutputDevicesChanged,
  NetworkError,
  SessionExpired,
};

struct BackendEvent {
  BackendEventKind kind = BackendEventKind::ApiCompleted;
  std::string correlationId;
  nlohmann::json payload;
};

class IBackendEventSink {
 public:
  virtual ~IBackendEventSink() = default;
  virtual void OnBackendEvent(const BackendEvent& event) = 0;
};

class IBackendFacade {
 public:
  virtual ~IBackendFacade() = default;

  virtual std::future<DeviceInfo> EnsureDeviceReady() = 0;
  virtual std::future<nlohmann::json> BeginQrLogin() = 0;
  virtual std::future<nlohmann::json> PollQrLogin(std::string key) = 0;
  virtual std::future<nlohmann::json> SearchSongs(std::string keywords, int page, int pageSize) = 0;
  virtual std::future<nlohmann::json> GetPlaylistDetail(std::string id) = 0;
  virtual std::future<nlohmann::json> ResolveSongUrl(std::string hash, std::string quality) = 0;
};

std::unique_ptr<IBackendFacade> CreateBackendFacade();
std::unique_ptr<IBackendFacade> CreateBackendFacade(std::filesystem::path databasePath);

}  // namespace echo::core
