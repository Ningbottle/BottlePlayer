#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using SearchHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class SearchService {
 public:
  SearchService();
  explicit SearchService(SearchHttpGet httpGet);

  nlohmann::json Search(std::string keywords, std::string type, int page, int pageSize) const;
  nlohmann::json Hot(int count) const;
  nlohmann::json Suggest(std::string keywords, int count) const;

 private:
  SearchHttpGet httpGet_;
};

}  // namespace echo::core
