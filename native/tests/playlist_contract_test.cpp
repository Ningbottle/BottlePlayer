// EchoPlaylistContractTest — tracks output shape, id/listid/global_collection_id, user playlist normalization.
// Extracted from basic_contract_tests.cpp (lines 3415-3478) for independent build.

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/PlaylistService.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/Crypto.h"
#include "echo/core/HttpClient.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::string QueryValue(const std::string& url, const std::string& key) {
  const auto marker = key + "=";
  const auto start = url.find(marker);
  if (start == std::string::npos) return {};
  const auto valueStart = start + marker.size();
  const auto end = url.find('&', valueStart);
  return url.substr(valueStart, end == std::string::npos ? std::string::npos : end - valueStart);
}

}  // namespace

int main() {
  std::cout << "[PlaylistContract] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  // ── error_code / errcode helper ─────────────────────────────────────
  std::cout << "[PlaylistContract] Testing ReadKuGouErrorCode..." << std::endl;
  {
    using echo::core::IsKuGouErrorCode;
    using echo::core::ReadKuGouErrorCode;
    assert(ReadKuGouErrorCode({{"error_code", 20017}}) == 20017);
    assert(ReadKuGouErrorCode({{"error_code", "20017"}}) == 20017);
    assert(ReadKuGouErrorCode({{"errcode", 20017}}) == 20017);
    assert(ReadKuGouErrorCode({{"errcode", "20017"}}) == 20017);
    assert(ReadKuGouErrorCode({{"error_code", 0}}) == 0);
    assert(!ReadKuGouErrorCode(nlohmann::json::object()).has_value());
    assert(!ReadKuGouErrorCode({{"error_code", "not-a-number"}}).has_value());
    const auto conflict = ReadKuGouErrorCode({{"error_code", 20017}, {"errcode", 20018}});
    assert(conflict == 20017);
    assert(IsKuGouErrorCode({{"errcode", 20017}}, 20017));
    assert(IsKuGouErrorCode({{"error_code", "20017"}}, 20017));
    assert(!IsKuGouErrorCode({{"errcode", 0}}, 20017));
    std::cout << "  [ok] ReadKuGouErrorCode int/string/conflict table" << std::endl;
  }

  // ── GetTracks output shape contract ─────────────────────────────────
  std::cout << "[PlaylistContract] Testing GetTracks output shape..." << std::endl;
  {
    auto mockGet = [](const std::string&,
                      const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
      return {200, R"({"status":1,"data":{"info":[{"hash":"h1","filename":"A - Song","duration":240,"album_audio_id":1,"audio_id":2,"album_id":3,"privilege":10,"pay_type":3}],"total":1}})", ""};
    };

    echo::core::PlaylistService svc(mockGet);

    const auto tracks = svc.GetTracks("1", 1, 10);
    assert(tracks.contains("status"));
    assert(tracks.contains("data"));
    assert(tracks["data"].contains("songs"));
    assert(tracks["data"]["songs"].is_array());
    assert(tracks["data"]["songs"].size() == 1);
    const auto& song = tracks["data"]["songs"][0];
    assert(song.contains("hash"));
    assert(song.contains("songname"));
    assert(song.contains("singername"));
    assert(song.contains("timelen"));
    assert(song.contains("album_audio_id"));
    assert(song.contains("audio_id"));
    assert(song.contains("album_id"));
    assert(song.contains("privilege"));
    assert(song.contains("pay_type"));

    std::cout << "  [ok] GetTracks output shape contract" << std::endl;
  }

  // ── GetUserPlaylists output shape + id/listid/global_collection_id ───
  std::cout << "[PlaylistContract] Testing GetUserPlaylists output shape..." << std::endl;
  {
    std::string capturedUrl;
    auto mockPost = [&](const std::string& url,
                        const std::string&,
                        const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
      capturedUrl = url;
      return {200, R"({"errcode":0,"data":{"lists":[{"global_collection_id":"collection_3_42_98765_0","listid":"98765","listname":"收藏歌单","songcount":5,"img":"img.jpg"}],"total":1}})", ""};
    };

    echo::core::PlaylistService svc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);

    echo::core::DeviceInfo device;
    device.dfid = "abcdefghijklmnopqrstuvwx";
    device.mid = "123456789012345678901234567890123456789";
    device.uuid = "0123456789abcdef0123456789abcdef";
    device.guid = "12345678-1234-1234-1234-123456789abc";

    const auto userLists = svc.GetUserPlaylists(device, "42", "tok", 1, 30);
    const std::unordered_map<std::string, std::string> signedParams = {
        {"appid", QueryValue(capturedUrl, "appid")},
        {"clientver", QueryValue(capturedUrl, "clientver")},
        {"clienttime", QueryValue(capturedUrl, "clienttime")},
        {"dfid", QueryValue(capturedUrl, "dfid")},
        {"mid", QueryValue(capturedUrl, "mid")},
        {"uuid", QueryValue(capturedUrl, "uuid")},
        {"plat", QueryValue(capturedUrl, "plat")},
        {"userid", QueryValue(capturedUrl, "userid")},
        {"token", QueryValue(capturedUrl, "token")},
    };
    // 2026-09-03 配对实测：概念族被上游 20017 全拒，业务请求改标准族
    // （1005/20489 + 标准盐），与登录令牌同族。
    assert(QueryValue(capturedUrl, "appid") == "1005");
    assert(QueryValue(capturedUrl, "clientver") == "20489");
    assert(QueryValue(capturedUrl, "uuid") == "-");
    assert(QueryValue(capturedUrl, "signature") ==
           echo::core::SignatureAndroidParams(
               signedParams, R"({"page":1,"pagesize":30,"token":"tok","total_ver":979,"type":2,"userid":42})",
                echo::core::KuGouSaltKind::Standard));
    assert(userLists.contains("status"));
    assert(userLists["status"] == 1);
    assert(userLists.contains("data"));
    assert(userLists["data"].contains("list"));
    assert(userLists["data"]["list"].is_array());
    assert(userLists["data"]["list"].size() == 1);
    const auto& pl = userLists["data"]["list"][0];
    // id/listid/global_collection_id must all be present and correct
    assert(pl.contains("id"));
    assert(pl.contains("global_collection_id"));
    assert(pl.contains("listid"));
    assert(pl.contains("name"));
    assert(pl.contains("songcount"));
    assert(pl.contains("img"));
    assert(pl["id"] == "collection_3_42_98765_0");
    assert(pl["listid"] == "98765");
    assert(pl["global_collection_id"] == "collection_3_42_98765_0");
    assert(pl["id"] != pl["listid"]);
    assert(!pl.contains("specialid") || pl["specialid"] != pl["id"]);

    std::cout << "  [ok] GetUserPlaylists output shape + id contract" << std::endl;
  }

  // ── Non-zero errcode must not become status=1 just because lists exist ─
  std::cout << "[PlaylistContract] Testing error code beats non-empty lists..." << std::endl;
  {
    auto mockPost = [](const std::string&, const std::string&,
                       const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{200,
          R"({"errcode":20017,"data":{"lists":[{"global_collection_id":"collection_3_42_98765_0","listid":"98765","listname":"P1"}],"total":1}})",
          ""};
    };
    echo::core::PlaylistService svc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);
    echo::core::DeviceInfo device;
    const auto userLists = svc.GetUserPlaylists(device, "42", "tok", 1, 30);
    assert(userLists.value("status", 1) == 0);
    assert(echo::core::ReadKuGouErrorCode(userLists) == 20017);
    assert(userLists["data"]["list"].is_array());
    std::cout << "  [ok] non-zero errcode keeps status=0" << std::endl;
  }

  // ── Mixed/invalid user playlist IDs ─────────────────────────────────
  std::cout << "[PlaylistContract] Testing invalid user playlist IDs are skipped..." << std::endl;
  {
    auto mockPost = [](const std::string&, const std::string&,
                       const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{200,
          R"({"errcode":0,"data":{"lists":[
            {"global_collection_id":"collection_3_42_1_0","listid":"1","listname":"valid"},
            {"listid":"2","listname":"missing-gid"},
            {"global_collection_id":"collection_3_42_3_0","listname":"missing-listid"}
          ],"total":3}})",
          ""};
    };
    echo::core::PlaylistService svc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);
    echo::core::DeviceInfo device;
    const auto userLists = svc.GetUserPlaylists(device, "42", "tok", 1, 30);
    assert(userLists.value("status", 0) == 1);
    assert(userLists["data"]["list"].size() == 1);
    assert(userLists["data"]["list"][0]["id"] == "collection_3_42_1_0");
    assert(userLists["data"].value("skipped_invalid_id_count", 0) == 2);
    std::cout << "  [ok] mixed valid/invalid IDs" << std::endl;
  }

  {
    auto mockPost = [](const std::string&, const std::string&,
                       const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{200,
          R"({"errcode":0,"data":{"lists":[{"listid":"2","listname":"missing-gid"}],"total":1}})",
          ""};
    };
    echo::core::PlaylistService svc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);
    echo::core::DeviceInfo device;
    const auto userLists = svc.GetUserPlaylists(device, "42", "tok", 1, 30);
    assert(userLists.value("status", 1) == 0);
    assert(userLists.value("error_code", "") == "native_user_playlist_id_contract_invalid");
    assert(userLists["data"]["list"].empty());
    std::cout << "  [ok] all-invalid IDs are a contract error, not empty success" << std::endl;
  }

  {
    auto mockPost = [](const std::string&, const std::string&,
                       const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{200, R"({"errcode":0,"data":{"lists":[],"total":0}})", ""};
    };
    echo::core::PlaylistService svc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);
    echo::core::DeviceInfo device;
    const auto userLists = svc.GetUserPlaylists(device, "42", "tok", 1, 30);
    assert(userLists.value("status", 0) == 1);
    assert(userLists["data"]["list"].empty());
    std::cout << "  [ok] true empty success stays status=1" << std::endl;
  }

  // ── Empty/invalid playlist id returns empty result ────────────────────
  std::cout << "[PlaylistContract] Testing empty id edge cases..." << std::endl;
  {
    echo::core::PlaylistService svc;
    const auto empty0 = svc.GetTracks("", 1, 10);
    assert(empty0["status"] == 1);
    assert(empty0["data"]["songs"].is_array());
    assert(empty0["data"]["songs"].empty());

    const auto empty1 = svc.GetTracks("0", 1, 10);
    assert(empty1["data"]["songs"].empty());

    const auto empty2 = svc.GetTracks("null", 1, 10);
    assert(empty2["data"]["songs"].empty());

    std::cout << "  [ok] Empty/0/null id returns empty tracks" << std::endl;
  }

  // ── GetTracks branch exclusivity ────────────────────────────────────
  std::cout << "[PlaylistContract] Testing GetTracks URL branches..." << std::endl;
  {
    std::string capturedUrl;
    auto mockGet = [&](const std::string& url,
                       const std::unordered_map<std::string, std::string>&)
        -> echo::core::HttpResult {
      capturedUrl = url;
      return {200,
              R"({"status":1,"data":{"info":[{"hash":"h1","filename":"A - Song","duration":240,"album_audio_id":1,"audio_id":2,"album_id":3,"privilege":10,"pay_type":3}],"total":1}})",
              ""};
    };
    echo::core::PlaylistService svc(mockGet);
    echo::core::DeviceInfo device;
    device.dfid = "abcdefghijklmnopqrstuvwx";
    device.mid = "123456789012345678901234567890123456789";
    device.guid = "12345678-1234-1234-1234-123456789abc";

    const auto collection = svc.GetTracks(device, "collection_3_42_98765_0", 1, 10);
    assert(capturedUrl.find("https://pubsongs.kugou.com/v2/get_other_list_file_nofilt") == 0);
    assert(capturedUrl.find("special/song") == std::string::npos);
    assert(QueryValue(capturedUrl, "global_collection_id") == "collection_3_42_98765_0");
    assert(QueryValue(capturedUrl, "appid") == "3116");
    assert(QueryValue(capturedUrl, "clientver") == "11440");
    assert(collection["data"].contains("list"));
    assert(collection["data"]["list"].is_array());
    assert(collection["data"]["list"].size() == 1);

    const auto pub = svc.GetTracks(device, "12345", 1, 10);
    assert(capturedUrl.find("http://mobilecdn.kugou.com/api/v3/special/song") == 0);
    assert(capturedUrl.find("pubsongs") == std::string::npos);
    assert(QueryValue(capturedUrl, "specialid") == "12345");
    assert(pub["data"].contains("list"));
    assert(pub["data"]["list"].size() == 1);

    std::cout << "  [ok] pubsongs vs special/song are exclusive" << std::endl;
  }

  std::cout << "[PlaylistContract] All tests passed!" << std::endl;
  return 0;
}
