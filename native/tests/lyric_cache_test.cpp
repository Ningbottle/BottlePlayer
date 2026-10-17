#include "echo/core/LyricService.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using echo::core::HttpResult;
using echo::core::LyricService;
using namespace std::chrono_literals;

const std::string kSearch = R"({"status":200,"candidates":[{"id":"fixture-id","accesskey":"fixture-key"}],"info":[]})";
const std::string kDetail = R"({"status":200,"content":"WzAwOjAwXUZpeHR1cmU="})";
int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
  if (!ok) ++failures;
}
bool Hit(const nlohmann::json& result) {
  return result.contains("diagnostics") && result["diagnostics"].value("cache_hit", false);
}

int main() {
  {
    for (const auto* payload : {
        R"({"status":200,"errcode":200,"candidates":[{"id":"id","accesskey":"key"}]})",
        R"({"status":"200","errcode":"200","candidates":[{"id":12,"accesskey":" key "}]})"}) {
      int calls = 0;
      LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, payload, {}}; });
      const auto first = service.Search("hash");
      const auto repeat = service.Search("hash");
      Check(first["status"] == 200 && calls == 1 && Hit(repeat),
            "real search status=200 errcode=200 is normalized and cached");
      Check(first["candidates"][0]["id"].is_string() && first["candidates"][0]["accesskey"] == "key",
            "candidate identities are normalized to trimmed strings");
    }
    LyricService download([](const auto&, const auto&) {
      return HttpResult{200, R"({"status":200,"errcode":200,"content":"WzAwOjAwXUZpeHR1cmU="})", {}};
    });
    Check(download.GetDetail("id", "key")["status"] == 0,
          "download does not inherit the search-only errcode=200 success rule");
    LyricService candidates([](const auto&, const auto&) {
      return HttpResult{200, R"({"status":200,"candidates":[null,{}, {"id":"valid","accesskey":"key"}]})", {}};
    });
    const auto filtered = candidates.Search("hash");
    Check(filtered["status"] == 200 && filtered["candidates"].size() == 1 &&
          filtered["candidates"][0]["id"] == "valid", "bad leading candidates do not hide usable lyrics");
    LyricService missing([](const auto&, const auto&) { return HttpResult{200, R"({"status":200})", {}}; });
    Check(missing.Search("hash")["status"] == 0, "missing candidates is an error rather than genuine empty lyrics");
  }
  {
    for (const auto* payload : {R"({"status":200,"content":""})", R"({"status":200,"content":"!!!!"})",
        R"({"status":200,"content":"/w=="})", R"({"status":200,"content":"SGVsbG8="})",
        R"({"status":200,"content":"WzAwOjAwXQ=="})",
        R"({"status":200,"content":"77u/WzAwOjAwXQ=="})",
        R"({"status":200,"content":"WzAwOjAwXUZpeHR1cmV="})"}) {
      int calls = 0;
      LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, payload, {}}; });
      try {
        const auto first = service.GetDetail("id", "key");
        const auto repeat = service.GetDetail("id", "key");
        Check(first["status"] == 0 && calls == 2 && !Hit(repeat),
              "empty corrupted non-UTF8 or non-lyric downloads fail without positive caching");
      } catch (...) { Check(false, "invalid lyric bytes never escape as an exception"); }
    }
    LyricService chinese([](const auto&, const auto&) {
      return HttpResult{200, R"({"status":"200","errcode":"0","content":"WzAwOjAxLjAwXeS4reaWh+atjOivjQ=="})", {}};
    });
    const auto lyric = chinese.GetDetail("id", "key");
    Check(lyric["status"] == 200 && lyric["lyric"] == "[00:01.00]中文歌词" && Hit(chinese.GetDetail("id", "key")),
          "UTF8 timed lyric and string statuses remain valid and reusable");
  }
  {
    int searchCalls = 0;
    LyricService service([&](const std::string& url, const auto&) {
      if (url.find("/search?") != std::string::npos) {
        ++searchCalls;
        return HttpResult{200, R"({"status":200,"errcode":200,"candidates":[{"id":"one","accesskey":"key"},{"id":"two","accesskey":"key"}]})", {}};
      }
      return HttpResult{200, R"({"status":0,"errcode":123})", {}};
    });
    service.Search("hash");
    service.GetDetail("two", "key");
    Check(!Hit(service.Search("hash")) && searchCalls == 2,
          "rejected fallback candidate invalidates its source search cache");
  }
  {
    int calls = 0;
    LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, kSearch, {}}; });
    const auto first = service.Search("  ABCDEF  ");
    const auto repeat = service.Search("abcdef");
    Check(calls == 1 && !Hit(first) && Hit(repeat), "search success reused across hash case and whitespace");
    Check(first["candidates"] == repeat["candidates"] && first["data"] == repeat["data"],
          "cached search preserves the existing payload contract");
    service.Search("different-hash");
    Check(calls == 2, "different songs never share a search cache entry");
  }
  {
    int calls = 0;
    LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, kDetail, {}}; });
    const auto first = service.GetDetail(" id ", " key ");
    const auto repeat = service.GetDetail("id", "key");
    Check(calls == 1 && Hit(repeat) && first["lyric"] == "[00:00]Fixture" && first["data"] == repeat["data"],
          "decoded lyric success is reused without changing fields");
    service.GetDetail("id", "another-key");
    service.GetDetail("another-id", "key");
    Check(calls == 3, "detail cache identity includes both id and accesskey");
  }
  {
    int calls = 0;
    LyricService service([&](const auto&, const auto&) {
      if (++calls == 1) return HttpResult{0, {}, "WinHTTP timeout 12002", true};
      return HttpResult{200, kSearch, {}};
    });
    service.Search("hash");
    const auto recovered = service.Search("hash");
    const auto repeat = service.Search("hash");
    Check(calls == 2 && !Hit(recovered) && Hit(repeat), "transient search failure is never cached");
  }
  {
    for (const auto& payload : {
        R"({"status":0,"candidates":[{"id":"id","accesskey":"key"}]})",
        R"({"status":200,"candidates":[]})",
        R"({"status":200,"candidates":[{"id":"id"}]})",
        "bad-json", "[]", "null"}) {
      int calls = 0;
      LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, payload, {}}; });
      service.Search("hash");
      const auto repeat = service.Search("hash");
      Check(calls == 2 && !Hit(repeat), "failed/empty/invalid search result is not cached");
    }
    for (const auto& payload : {R"({"status":0,"content":"WzAwOjAwXUZpeHR1cmU="})", R"({"status":200,"content":""})", "bad-json"}) {
      int calls = 0;
      LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, payload, {}}; });
      service.GetDetail("id", "key");
      const auto repeat = service.GetDetail("id", "key");
      Check(calls == 2 && !Hit(repeat), "failed/empty/invalid download is not cached");
    }
  }
  {
    auto now = std::chrono::steady_clock::time_point{};
    int calls = 0;
    LyricService service([&](const std::string& url, const auto&) {
      ++calls;
      return HttpResult{200, url.find("/search?") != std::string::npos ? kSearch : kDetail, {}};
    }, [&] { return now; });
    service.Search("hash");
    service.GetDetail("id", "key");
    now += 599s;
    Check(Hit(service.Search("hash")) && Hit(service.GetDetail("id", "key")) && calls == 2,
          "successful entries are valid before TTL");
    now += 2s;
    Check(!Hit(service.Search("hash")) && Hit(service.GetDetail("id", "key")) && calls == 3,
          "search candidates refresh after ten minutes");
    now += 6h;
    Check(!Hit(service.GetDetail("id", "key")) && calls == 4, "download content refreshes after six hours");
  }
  {
    for (const auto& payload : {
        R"({"status":200,"error_code":123,"candidates":[{"id":"id","accesskey":"key"}]})",
        R"({"status":200,"error_code":null,"errcode":123,"candidates":[{"id":"id","accesskey":"key"}]})",
        R"({"status":200,"candidates":"nope"})",
        R"({"status":200,"candidates":[{}, {"id":"id"}]})"}) {
      int calls = 0;
      LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, payload, {}}; });
      service.Search("hash");
      const auto repeat = service.Search("hash");
      Check(calls == 2 && !Hit(repeat), "business code or unusable first candidate vetoes cache");
    }
    for (const auto& payload : {"[]", "null", R"({"status":200,"candidates":"nope"})"}) {
      LyricService service([&](const auto&, const auto&) { return HttpResult{200, payload, {}}; });
      Check(service.Search("hash")["status"] == 0, "malformed success is not exposed as valid candidates");
    }
  }
  {
    int searchCalls = 0;
    LyricService service([&](const std::string& url, const auto&) {
      if (url.find("/search?") != std::string::npos) { ++searchCalls; return HttpResult{200, kSearch, {}}; }
      return HttpResult{200, R"({"status":0,"error_code":123,"content":""})", {}};
    });
    service.Search("hash");
    Check(Hit(service.Search("hash")), "search success cached before key rejection");
    service.GetDetail("fixture-id", "fixture-key");
    Check(!Hit(service.Search("hash")) && searchCalls == 2,
          "download business rejection invalidates matching stale search key");
  }
  {
    for (bool detail : {false, true}) {
      auto payload = nlohmann::json::parse(detail ? kDetail : kSearch);
      payload["error_code"] = 0;
      payload["errcode"] = 17;
      LyricService service([&](const auto&, const auto&) { return HttpResult{200, payload.dump(), {}}; });
      const auto out = detail ? service.GetDetail("id", "key") : service.Search("hash");
      Check(out["status"] == 0 && out["error_code"] == 17 && out["diagnostics"]["upstream_error_code"] == 17,
            "nonzero errcode is not hidden behind status=200 and error_code=0");
      payload.erase("error_code");
      payload.erase("errcode");
      payload.erase("status");
      const auto missing = detail ? service.GetDetail("id", "key") : service.Search("hash");
      Check(missing["status"] == 0 && !Hit(missing), "missing upstream status cannot become positive cache");
    }
    int calls = 0;
    LyricService malformed([&](const auto&, const auto&) {
      ++calls;
      auto payload = nlohmann::json::parse(kDetail);
      payload["content"] = payload["content"].get<std::string>() + "!";
      return HttpResult{200, payload.dump(), {}};
    });
    malformed.GetDetail("id", "key");
    Check(!Hit(malformed.GetDetail("id", "key")) && calls == 2, "corrupted base64 is never cached as decoded content");
  }
  {
    int calls = 0;
    LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, kDetail, {}}; });
    service.GetDetail("ab", "c");
    service.GetDetail("a", "bc");
    Check(calls == 2 && Hit(service.GetDetail("ab", "c")), "length-encoded detail keys cannot collide");
    LyricService oversized([&](const auto&, const auto&) {
      ++calls;
      auto payload = nlohmann::json::parse(kSearch);
      payload["padding"] = std::string(150000, 'x');
      return HttpResult{200, payload.dump(), {}};
    });
    const auto before = calls;
    oversized.Search("hash");
    Check(!Hit(oversized.Search("hash")) && calls == before + 2, "oversized success is returned but never cached");
  }
  {
    int calls = 0;
    LyricService service([&](const auto&, const auto&) { ++calls; return HttpResult{200, kSearch, {}}; });
    for (int i = 0; i < 129; ++i) service.Search("hash-" + std::to_string(i));
    Check(Hit(service.Search("hash-128")) && calls == 129, "recent successful entry remains cached");
    service.Search("hash-0");
    Check(calls == 130, "bounded cache evicts oldest entries");
  }
  {
    std::atomic<int> active{0}, peak{0};
    LyricService service([&](const auto&, const auto&) {
      const auto current = ++active;
      auto observed = peak.load();
      while (observed < current && !peak.compare_exchange_weak(observed, current)) {}
      std::this_thread::sleep_for(50ms);
      --active;
      return HttpResult{200, kSearch, {}};
    });
    std::thread first([&] { service.Search("one"); });
    std::thread second([&] { service.Search("two"); });
    first.join();
    second.join();
    Check(peak == 2 && Hit(service.Search("one")) && Hit(service.Search("two")),
          "cache lock is never held across upstream I/O");
  }
  return failures ? 1 : 0;
}
