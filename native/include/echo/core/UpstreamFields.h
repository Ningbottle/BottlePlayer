#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

// Shared reading of the three top-level fields upstream business responses
// use to signal success/failure. Reading is deliberately non-interpreting:
// numeric parsing differs per endpoint dialect (mobilecdn's strict
// from_chars in MobileCatalogResponse.h, SongUrlService's prefix stoi,
// LyricService's string comparison), so this header carries presence and
// the raw JSON form only. "Field present but invalid" and "field absent"
// stay distinguishable — see native/tests/upstream_semantics_test.cpp for
// the pinned per-dialect classifications.
namespace echo::core::upstream {

struct Field {
  const nlohmann::json* raw = nullptr;  // nullptr = key absent at top level

  explicit operator bool() const { return raw != nullptr; }
  const nlohmann::json& operator*() const { return *raw; }
};

struct Fields {
  Field status;      // "status"
  Field errorCode;   // "error_code"
  Field errcode;     // "errcode"
};

inline Fields Read(const nlohmann::json& response) {
  Fields fields;
  if (!response.is_object()) return fields;
  const auto read = [&response](std::string_view key) -> Field {
    const auto it = response.find(key);
    return it == response.end() ? Field{} : Field{&*it};
  };
  fields.status = read("status");
  fields.errorCode = read("error_code");
  fields.errcode = read("errcode");
  return fields;
}

}  // namespace echo::core::upstream
