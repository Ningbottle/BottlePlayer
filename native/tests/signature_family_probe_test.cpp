// EchoSignatureFamilyProbeTest — /diagnostics/signature-family (debug-only
// route) offline contract.
//
// What this pins:
//   * no session            -> native_probe_no_session, zero transport calls;
//   * partial injection     -> native_probe_partial_transport (never half-real
//                              network, half-fake);
//   * happy path            -> 4 executed probes (VIP/playlist ×
//                              Standard/Concept) on ONE immutable snapshot,
//                              whole-profile records, stable fingerprints,
//                              usable_for_selection=true;
//   * business failure      -> 20017 stays an interpretable business rejection
//                              (transport_ok + json_parseable +
//                              business_rejection_valid), the round stays
//                              usable for comparison, but the verdict is
//                              "no family choice" — a rejection is NOT a
//                              success and never selects a family;
//   * timeout+success       -> round unusable (insufficient_transport_failed):
//                              a timed-out Standard never proves Concept;
//   * structure anomaly+success (VIP data without authoritative fields,
//                              playlist info:null) -> round unusable
//                              (insufficient_unknown_structure): presence
//                              alone is not validity;
//   * all transport failed  -> round unusable, every record transport_error;
//   * playlist counterexamples (2026-09-15 review finding 2): entries
//                              missing identifiers / non-object elements /
//                              all-filtered -> NOT a success (normalized
//                              status + entry usability verified, raw_/
//                              normalized_/skipped_count recorded); a legal
//                              empty list still passes;
//   * account_has_rights (2026-09-15 review finding 3): a non-empty
//                              busi_vip array is not current rights —
//                              expired history is false/none, positive
//                              expired evidence is expired, insufficient
//                              info is unknown; active requires unexpired
//                              evidence aligned with vipResolver;
//   * in-memory backfill (empty persisted mid/uuid) is NOT a snapshot
//                              mismatch: the resolved-identity comparison
//                              uses the same derivation as the request path;
//   * budget exhaustion     -> later probes are NOT_RUN with null statuses,
//                              never recorded as business failures;
//   * background interference (session rotated mid-probe) -> interfered=true,
//                              usable_for_selection=false, while the probes
//                              themselves still used the snapshot credentials.
//
// The route exists only in Debug builds; Release non-registration is covered
// by route_contract_test.cpp.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "echo/core/CompatRoutes.h"
#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"
#include "echo/core/KuGouProfile.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::cerr << "CHECK failed: " << msg << " (" << #cond << ")"    \
                << std::endl;                                         \
      std::exit(1);                                                   \
    }                                                                 \
  } while (0)

std::filesystem::path TestDbPath(const wchar_t* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::wstring(L"echomusic-sigfam-") + name + L".db");
  std::filesystem::remove(path);
  std::filesystem::remove(path.wstring() + L"-wal");
  std::filesystem::remove(path.wstring() + L"-shm");
  return path;
}

struct CapturedCall {
  std::string url;
  std::string body;
};

echo::core::DeviceInfo SeedRegisteredDevice(echo::storage::Database& db) {
  echo::core::DeviceInfo device;
  device.dfid = "sigfamdfid00000000000000001";
  device.guid = "sigfam-guid-1234-5678-901234567890";
  device.mid = "123456789012345678901234567890123456789";
  // Persist uuid as well: EnsureDeviceReady backfills empty mid/uuid in
  // memory only, and the happy-path assertions compare snapshot fingerprints
  // against the persisted after-read. A fully populated row keeps the two
  // equal (the dedicated backfill test below covers the empty case).
  device.uuid = "sigfam-uuid-1234567890abcdef1234567890abcd";
  device.registered = true;
  const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
  device.appid = profile.appid;
  device.clientver = profile.clientver;
  echo::storage::DeviceRepository(db).Save(device);
  return device;
}

void SeedSession(echo::storage::Database& db, const std::string& token) {
  echo::core::SessionInfo session;
  session.userId = "42";
  session.token = token;
  session.t1 = "t1-snapshot";
  echo::storage::SessionRepository(db).Save(session);
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  std::cout << "[SignatureFamilyProbe] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  using echo::core::DeviceInfo;
  using echo::core::HttpResult;
  using echo::core::LoginHttpGet;
  using echo::core::LoginHttpPost;
  using echo::core::SessionInfo;

  // ── 1. No session: refuse before any transport exists. ────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"no-session"));
    db.Initialize();
    SeedRegisteredDevice(db);

    int transportCalls = 0;
    LoginHttpGet get = [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
      ++transportCalls;
      return HttpResult{0, "", "must not be called"};
    };
    LoginHttpPost post = [&](const std::string&, const std::string&,
                             const std::unordered_map<std::string, std::string>&) {
      ++transportCalls;
      return HttpResult{0, "", "must not be called"};
    };
    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    CHECK(resp.httpStatus == 200, "no-session still answers a JSON envelope");
    CHECK(resp.body.value("status", 0) == 0, "no-session is a failure envelope");
    CHECK(resp.body.value("error_code", "") == "native_probe_no_session",
          "no-session error code");
    CHECK(transportCalls == 0, "no transport call may happen without a session");
    db.Close();
  }
  std::cout << "  [ok] no session -> native_probe_no_session, zero transports" << std::endl;

  // ── 2. Partial injection: both verbs or nothing. ──────────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"partial"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "tok");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      return HttpResult{0, "", "unused"};
    };
    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, nullptr, 45000);
    CHECK(resp.body.value("error_code", "") == "native_probe_partial_transport",
          "GET-only injection must be refused");
    db.Close();
  }
  std::cout << "  [ok] partial transport injection refused" << std::endl;

  // ── 3. Happy path: 4 executed probes on one snapshot. ─────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"happy"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-1");

    std::vector<CapturedCall> calls;
    LoginHttpGet get = [&](const std::string& url,
                           const std::unordered_map<std::string, std::string>&) {
      calls.push_back({url, ""});
      // Standard family VIP: authoritative shape, explicitly NO vip rights.
      if (Contains(url, "appid=1005")) {
        return HttpResult{200,
                          R"({"status":1,"data":{"is_vip":0,"vip_type":0,"busi_vip":[]}})", ""};
      }
      // Concept family VIP: authoritative VIP rights (vip_token empty is
      // irrelevant to this endpoint — the fixture never carries one).
      return HttpResult{200,
                        R"({"status":1,"data":{"is_vip":1,"vip_type":1,"busi_vip":[{"product_type":"music","is_vip":1}]}})",
                        ""};
    };
    LoginHttpPost post = [&](const std::string& url, const std::string& body,
                             const std::unordered_map<std::string, std::string>&) {
      calls.push_back({url, body});
      // 审阅发现 2（2026-09-15）后条目必须可归一化：gid 需要 collection_
      // 前缀、listid 必须数字，否则生产层会丢弃条目/降级 status。
      return HttpResult{200,
                        R"({"status":1,"data":{"info":[{"gid":"collection_3_42_1001_0","listid":"1001","name":"liked","songcount":3}],"total":1}})",
                        ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    CHECK(resp.body.value("status", 0) == 1, "happy path envelope status");
    const auto& data = resp.body["data"];
    CHECK(data.value("usable_for_selection", false), "clean round is usable for selection");
    CHECK(!data.value("interfered", true), "clean round has no interference");
    CHECK(data.value("session_fp", "") == data.value("session_fp_after", "x"),
          "session fingerprint stable");
    CHECK(data.value("device_fp", "") == data.value("device_fp_after", "x"),
          "device fingerprint stable");
    CHECK(data.value("vip_verdict", "") == "both_succeed_no_config_change",
          "VIP verdict for a clean double success");
    CHECK(data.value("playlist_verdict", "") == "both_succeed_no_config_change",
          "playlist verdict for a clean double success");

    const auto& probes = data["probes"];
    CHECK(probes.size() == 4, "four probe records");
    const char* expectedFamilies[] = {"vip", "vip", "playlist", "playlist"};
    const char* expectedProfiles[] = {"standard", "concept", "standard", "concept"};
    for (int i = 0; i < 4; ++i) {
      const auto& p = probes[i];
      CHECK(p.value("executed", false), "probe executed");
      CHECK(p.value("result", "") == "executed", "probe result marker");
      CHECK(std::string(p.value("family", "")) == expectedFamilies[i], "probe family order");
      CHECK(std::string(p.value("profile", "")) == expectedProfiles[i], "probe profile order");
      CHECK(p.value("response_shape_valid", false),
            "authoritative shape counts as success (empty rights / empty list included)");
      CHECK(p.value("normalized_status", 0) == 1, "normalized status success");
      // 计划 1c：成功记录的三类判别字段。
      CHECK(p.value("transport_ok", false), "success implies transport ok");
      CHECK(p.value("json_parseable", false), "success implies parseable JSON");
      CHECK(p.value("success_payload_valid", false), "success payload valid");
      CHECK(!p.value("business_rejection_valid", true), "success is not a rejection");
      CHECK(p.value("response_contract_valid", false), "contract valid via success path");
      CHECK(std::string(p.value("result_class", "")) == "success", "result class success");
    }
    CHECK(!data.value("snapshot_mismatch", true), "clean round has no snapshot mismatch");
    CHECK(data.contains("snapshot_captured_at") && data.contains("baseline_read_at"),
          "snapshot/baseline timestamps recorded");
    CHECK(data.contains("request_identity") && data["request_identity"].is_object(),
          "request identity summary present");
    CHECK(data["request_identity"].value("token_len", 0) ==
              static_cast<int>(std::string("snap-token-1").size()),
          "request identity records the snapshot token length");
    CHECK(probes[0].value("appid", "") == "1005", "VIP Standard appid");
    CHECK(probes[0].value("clientver", "") == "20489", "VIP Standard clientver");
    CHECK(probes[0].value("salt_kind", "") == "standard", "VIP Standard salt");
    CHECK(probes[1].value("appid", "") == "3116", "VIP Concept appid");
    CHECK(probes[1].value("clientver", "") == "11440", "VIP Concept clientver");
    CHECK(probes[1].value("salt_kind", "") == "lite", "VIP Concept salt");

    // 审阅发现 2（2026-09-15）：成功记录同时携带条目计数（可用条目 1 条，
    // 无丢弃）。合法非空歌单 = raw_count == normalized_count。
    for (int i = 2; i < 4; ++i) {
      CHECK(probes[i].value("raw_count", 0) == 1, "playlist raw_count recorded");
      CHECK(probes[i].value("normalized_count", 0) == 1, "playlist normalized_count recorded");
      CHECK(probes[i].value("skipped_count", 1) == 0, "playlist skipped_count recorded");
      CHECK(std::string(probes[i].value("playlist_shape_class", "")) == "non_empty_list",
            "raw shape class non_empty_list");
    }

    // 快照语义：所有请求都携带快照 token，与存储内容无关。
    CHECK(calls.size() == 4, "exactly four upstream calls");
    for (const auto& call : calls) {
      const bool vipCall = Contains(call.url, "get_union_vip");
      if (vipCall) {
        CHECK(Contains(call.url, "token=snap-token-1"), "VIP requests use the snapshot token");
      } else {
        CHECK(Contains(call.body, "snap-token-1"), "playlist requests use the snapshot token");
        CHECK(Contains(call.url, "get_all_list"), "playlist call hits v7/get_all_list");
      }
    }
    db.Close();
  }
  std::cout << "  [ok] happy path: 4 executed probes, snapshot respected, usable round"
            << std::endl;

  // ── 3b. Reverse order: Concept runs BEFORE Standard (judgement-table
  // retest order); records come back in execution order. ─────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"reverse"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-1");

    std::vector<CapturedCall> calls;
    LoginHttpGet get = [&](const std::string& url,
                           const std::unordered_map<std::string, std::string>&) {
      calls.push_back({url, ""});
      return Contains(url, "appid=1005")
          ? HttpResult{200, R"({"status":1,"data":{"is_vip":0,"vip_type":0}})", ""}
          : HttpResult{200, R"({"status":1,"data":{"is_vip":1,"vip_type":1}})", ""};
    };
    LoginHttpPost post = [&](const std::string& url, const std::string& body,
                             const std::unordered_map<std::string, std::string>&) {
      calls.push_back({url, body});
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000, true);
    const auto& data = resp.body["data"];
    CHECK(data.value("order", "") == "reverse", "round records the reverse order");
    CHECK(data.value("usable_for_selection", false), "reverse round still usable");
    CHECK(!data.value("snapshot_mismatch", true), "reverse round snapshot consistent");
    const auto& probes = data["probes"];
    CHECK(probes.size() == 4, "four probe records in reverse mode too");
    const char* expectedFamilies[] = {"vip", "vip", "playlist", "playlist"};
    const char* expectedProfiles[] = {"concept", "standard", "concept", "standard"};
    for (int i = 0; i < 4; ++i) {
      CHECK(std::string(probes[i].value("family", "")) == expectedFamilies[i],
            "reverse probe family order");
      CHECK(std::string(probes[i].value("profile", "")) == expectedProfiles[i],
            "reverse probe profile order");
      CHECK(probes[i].value("executed", false), "reverse probe executed");
    }
    // 执行顺序：Concept 的上游调用发生在 Standard 之前。
    CHECK(Contains(calls[0].url, "appid=3116"), "first upstream call is Concept");
    CHECK(Contains(calls[1].url, "appid=1005"), "second upstream call is Standard");
    db.Close();
  }
  std::cout << "  [ok] reverse order: Concept probes run first, records in order"
            << std::endl;

  // ── 4. Business rejection: 20017 is an interpretable rejection that may
  // take part in the comparison, but it is NOT a success and cannot pick a
  // family by itself. ─────────────────────────────────────────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"bizfail"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-2");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      // HTTP 200 + status=0 + 20017: not an upstream success.
      return HttpResult{200, R"({"status":0,"error_code":20017,"error":"session conflict"})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":0,"error_code":20017})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(data.value("usable_for_selection", false),
          "round stays usable: interpretable rejections participate in the comparison");
    CHECK(data.value("vip_verdict", "") == "both_business_rejected_no_family_choice",
          "20017 in both families -> no family choice, not a config change");
    CHECK(data.value("playlist_verdict", "") == "both_business_rejected_no_family_choice",
          "playlist 20017 in both families -> no family choice either");
    const auto& probes = data["probes"];
    for (int i = 0; i < 4; ++i) {
      CHECK(probes[i].value("executed", false), "probe executed");
      CHECK(!probes[i].value("response_shape_valid", true),
            "HTTP 200 without authoritative shape is NOT upstream success");
      CHECK(probes[i].value("upstream_status", 1) == 0, "upstream status recorded as 0");
      CHECK(probes[i].value("upstream_error_code", 0) == 20017,
            "upstream error code preserved");
      // 计划 1c：业务拒绝是可解释的契约响应——传输与解析均成功，只有
      // success_payload_valid 为 false。
      CHECK(probes[i].value("transport_ok", false), "rejection arrived over a healthy HTTP layer");
      CHECK(probes[i].value("json_parseable", false), "rejection body is parseable JSON");
      CHECK(probes[i].value("business_rejection_valid", false),
            "status=0 + error_code counts as a valid business rejection");
      CHECK(probes[i].value("response_contract_valid", false),
            "contract valid via the rejection path");
      CHECK(std::string(probes[i].value("result_class", "")) == "business_rejection",
            "result class business_rejection");
    }
    db.Close();
  }
  std::cout << "  [ok] business rejection stays upstream, participates but never selects"
            << std::endl;

  // ── 4b. Timeout + success: a timed-out Standard never proves Concept. ──
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"timeout-success"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-t");

    LoginHttpGet get = [](const std::string& url,
                          const std::unordered_map<std::string, std::string>&) {
      return Contains(url, "appid=1005")
          ? HttpResult{0, "", "WinHttp send request failed: 12002 (timeout)"}
          : HttpResult{200, R"({"status":1,"data":{"is_vip":1,"vip_type":1}})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(!data.value("usable_for_selection", true),
          "timeout + success is NOT a family-selection basis");
    CHECK(data.value("vip_verdict", "") == "insufficient_transport_failed",
          "VIP verdict for timeout + success");
    CHECK(data.value("playlist_verdict", "") == "both_succeed_no_config_change",
          "playlist pair still healthy in the same round");
    const auto& probes = data["probes"];
    CHECK(std::string(probes[0].value("result_class", "")) == "transport_error",
          "timed-out Standard recorded as transport_error");
    CHECK(!probes[0].value("transport_ok", true), "timed-out Standard transport_ok=false");
    CHECK(!probes[0].value("json_parseable", true), "timeout has no parseable body");
    CHECK(probes[1].value("response_shape_valid", false),
          "successful Concept record itself is still a valid success");
    db.Close();
  }
  std::cout << "  [ok] timeout + success -> round unusable, Concept not proven" << std::endl;

  // ── 4c. Structure anomaly + success: VIP status=1 without any
  // authoritative field cannot take part. ─────────────────────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"anomaly-success"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-s");

    LoginHttpGet get = [](const std::string& url,
                          const std::unordered_map<std::string, std::string>&) {
      return Contains(url, "appid=1005")
          ? HttpResult{200, R"({"status":1,"data":{}})", ""}
          : HttpResult{200, R"({"status":1,"data":{"is_vip":1,"vip_type":1}})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(!data.value("usable_for_selection", true),
          "structure anomaly + success is NOT a family-selection basis");
    CHECK(data.value("vip_verdict", "") == "insufficient_unknown_structure",
          "VIP verdict for structure anomaly + success");
    const auto& probes = data["probes"];
    CHECK(probes[0].value("transport_ok", false), "HTTP layer was healthy");
    CHECK(probes[0].value("json_parseable", false), "body parsed as JSON");
    CHECK(!probes[0].value("business_rejection_valid", true),
          "status=1 without authoritative fields is not a rejection either");
    CHECK(!probes[0].value("response_contract_valid", true), "unknown structure cannot participate");
    CHECK(std::string(probes[0].value("result_class", "")) == "unknown_structure",
          "result class unknown_structure");
    db.Close();
  }
  std::cout << "  [ok] VIP structure anomaly + success -> round unusable" << std::endl;

  // ── 4d. Playlist info:null: presence alone is not validity (plan 1b). ──
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"playlist-null-info"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-n");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"is_vip":0,"vip_type":0}})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      // info:null used to pass "field exists" checks; it must fail now.
      return HttpResult{200, R"({"status":1,"data":{"info":null,"total":0}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(!data.value("usable_for_selection", true), "info:null round is not usable");
    CHECK(data.value("playlist_verdict", "") == "insufficient_unknown_structure",
          "playlist verdict for info:null");
    const auto& probes = data["probes"];
    for (int i = 2; i < 4; ++i) {
      CHECK(probes[i].value("has_original_info", false), "info field WAS present");
      CHECK(std::string(probes[i].value("playlist_shape_class", "")) == "list_field_type_error",
            "raw type null recorded as list_field_type_error");
      CHECK(probes[i].contains("raw_list_fields") &&
                probes[i]["raw_list_fields"].is_array() &&
                probes[i]["raw_list_fields"].size() == 1 &&
                probes[i]["raw_list_fields"][0].value("type", "") == "null",
            "raw field type captured pre-normalization");
      CHECK(!probes[i].value("response_shape_valid", true), "info:null is not a valid success");
    }
    db.Close();
  }
  std::cout << "  [ok] playlist info:null -> list_field_type_error, round unusable" << std::endl;

  // ── 4e. All transports failed: nothing may be compared. ────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"all-transport-failed"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-f");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      return HttpResult{0, "", "WinHttp: cannot connect"};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{0, "", "WinHttp: cannot connect"};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(!data.value("usable_for_selection", true), "all transport failed -> unusable");
    CHECK(data.value("vip_verdict", "") == "insufficient_transport_failed",
          "VIP verdict for total transport failure");
    CHECK(data.value("playlist_verdict", "") == "insufficient_transport_failed",
          "playlist verdict for total transport failure");
    const auto& probes = data["probes"];
    for (int i = 0; i < 4; ++i) {
      CHECK(std::string(probes[i].value("result_class", "")) == "transport_error",
            "every record is transport_error");
      CHECK(!probes[i].value("transport_ok", true), "transport_ok=false");
      CHECK(!probes[i].value("json_parseable", true), "nothing parseable");
      CHECK(!probes[i].value("response_contract_valid", true), "nothing may participate");
    }
    db.Close();
  }
  std::cout << "  [ok] all transports failed -> round unusable" << std::endl;

  // ── 4f. In-memory backfill (empty persisted mid/uuid) is NOT a snapshot
  // mismatch: the identity comparison normalizes both sides through the same
  // derivation the request path uses. ─────────────────────────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"backfill"));
    db.Initialize();
    {
      echo::core::DeviceInfo device;
      device.dfid = "sigfamdfid00000000000000002";
      device.guid = "sigfam-guid-8765-4321-098765432109";
      // mid/uuid left empty on purpose: EnsureDeviceReady backfills them in
      // memory without persisting, so the snapshot device differs from the
      // persisted row.
      device.registered = true;
      const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
      device.appid = profile.appid;
      device.clientver = profile.clientver;
      echo::storage::DeviceRepository(db).Save(device);
    }
    SeedSession(db, "snap-token-b");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"is_vip":0,"vip_type":0}})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(!data.value("snapshot_mismatch", true),
          "in-memory backfill must not flag a snapshot mismatch");
    CHECK(data.value("usable_for_selection", false), "backfilled round stays usable");
    CHECK(!data.value("interfered", true), "backfill is not interference either");
    CHECK(data.value("device_fp", "") != data.value("device_fp_after", "x"),
          "snapshot fingerprint (backfilled uuid) differs from the persisted after-read");
    CHECK(data.value("device_fp", "") != data.value("device_fp_baseline", "x"),
          "and from the persisted baseline too");
    CHECK(data.value("session_fp", "") == data.value("session_fp_after", "x"),
          "session fingerprint stable");
    db.Close();
  }
  std::cout << "  [ok] in-memory backfill tolerated by the resolved-identity comparison"
            << std::endl;

  // ── 5. Budget exhaustion: late probes are NOT_RUN, never fake failures. ─
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"budget"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-3");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      return HttpResult{200, R"({"status":1,"data":{"is_vip":0}})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 200);
    const auto& data = resp.body["data"];
    CHECK(!data.value("usable_for_selection", true), "NOT_RUN round is not usable");
    CHECK(data.value("vip_verdict", "") == "insufficient_not_run",
          "budget verdict for VIP pair");
    CHECK(data.value("playlist_verdict", "") == "insufficient_not_run",
          "budget verdict for playlist pair");
    const auto& probes = data["probes"];
    CHECK(probes[0].value("executed", false), "first probe started within budget executes");
    for (int i = 1; i < 4; ++i) {
      CHECK(!probes[i].value("executed", true), "late probe not executed");
      CHECK(probes[i].value("result", "") == "NOT_RUN", "late probe marked NOT_RUN");
      CHECK(probes[i].contains("upstream_status") && probes[i]["upstream_status"].is_null(),
            "NOT_RUN must not fabricate an upstream status");
      CHECK(probes[i].value("response_shape_valid", true) == false,
            "NOT_RUN is not a success either");
    }
    db.Close();
  }
  std::cout << "  [ok] budget exhaustion -> NOT_RUN with null upstream status" << std::endl;

  // ── 6. Interference: session rotated mid-probe -> round unusable, but the
  // probes themselves stayed on the snapshot credentials. ─────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"interfere"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-4");

    std::vector<CapturedCall> calls;
    LoginHttpGet get = [&](const std::string& url,
                           const std::unordered_map<std::string, std::string>&) {
      calls.push_back({url, ""});
      return HttpResult{200, R"({"status":1,"data":{"is_vip":0}})", ""};
    };
    LoginHttpPost post = [&](const std::string& url, const std::string& body,
                             const std::unordered_map<std::string, std::string>&) {
      calls.push_back({url, body});
      // 模拟后台懒刷新：第一组歌单探测期间轮换会话。
      if (calls.size() == 3) {
        SessionInfo rotated;
        rotated.userId = "42";
        rotated.token = "rotated-midway";
        echo::storage::SessionRepository(db).Save(rotated);
      }
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(data.value("interfered", false), "mid-probe session rotation is interference");
    CHECK(!data.value("usable_for_selection", true), "interfered round is not usable");
    CHECK(data.value("vip_verdict", "") == "insufficient_interfered",
          "interference verdict for VIP pair");
    CHECK(data.value("playlist_verdict", "") == "insufficient_interfered",
          "interference verdict for playlist pair");
    CHECK(data.value("session_fp", "") != data.value("session_fp_after", ""),
          "session fingerprint changed across the round");
    // 快照语义：即使会话中途被轮换，后续请求仍然携带快照 token。
    for (const auto& call : calls) {
      if (Contains(call.url, "get_union_vip")) {
        CHECK(Contains(call.url, "token=snap-token-4"), "VIP stayed on the snapshot token");
      } else {
        CHECK(Contains(call.body, "snap-token-4"), "playlist stayed on the snapshot token");
      }
    }
    db.Close();
  }
  std::cout << "  [ok] interference detected; probes stayed on the immutable snapshot"
            << std::endl;

  // ── 7. Playlist counterexamples (2026-09-15 review finding 2): an
  // unusable non-empty list must not masquerade as an empty playlist or a
  // probe success; the upstream status stays recorded. Migrated from the
  // standalone diagnostic-counterexamples binary. ─────────────────────────
  struct PlaylistCase {
    const wchar_t* dbName;
    const char* label;
    const char* payload;  // upstream (pre-normalization) playlist response
    int expectRawCount;
    int expectNormalizedCount;
    int expectSkippedCount;
    const char* expectResultClass;
    int expectNormalizedStatus;
  };
  const PlaylistCase playlistCases[] = {
      // 缺标识条目：条目看似歌单但没有可用 id → 生产层把 status 降级为 0。
      {L"pl-missing-id", "entries missing identifiers",
       R"({"status":1,"data":{"info":[{"name":"missing-playlist-identifiers"}],"total":1}})",
       1, 0, 1, "normalization_failed", 0},
      // 非对象元素：被归一化静默丢弃且 skipped=0，status 仍是 1 —— 旧探针
      // 会把它当成合法空歌单/成功。
      {L"pl-non-object", "non-object elements silently dropped",
       R"({"status":1,"data":{"info":["ghost-entry",42],"total":2}})",
       2, 0, 0, "all_entries_unusable", 1},
      // 全部被过滤：两条看似歌单的条目都缺标识 → skipped=2、status=0。
      {L"pl-all-filtered", "all entries filtered out",
       R"({"status":1,"data":{"info":[{"name":"a"},{"specialname":"b"}],"total":2}})",
       2, 0, 2, "normalization_failed", 0},
  };
  for (const auto& playlistCase : playlistCases) {
    echo::storage::Database db;
    db.Open(TestDbPath(playlistCase.dbName));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-pl");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"is_vip":0,"vip_type":0,"busi_vip":[]}})", ""};
    };
    LoginHttpPost post = [&playlistCase](const std::string&, const std::string&,
                                         const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, playlistCase.payload, ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(!data.value("usable_for_selection", true), "unusable playlist round is not usable");
    const auto& probes = data["probes"];
    for (int i = 2; i < 4; ++i) {
      const auto& p = probes[i];
      CHECK(p.value("upstream_status", 0) == 1, "upstream status stays recorded as 1");
      CHECK(p.value("normalized_status", 1) == playlistCase.expectNormalizedStatus,
            playlistCase.label);
      CHECK(!p.value("success_payload_valid", true), "unusable list is not a success");
      CHECK(!p.value("response_shape_valid", true), "shape validity follows usability");
      CHECK(!p.value("response_contract_valid", true), "unusable list cannot participate");
      CHECK(std::string(p.value("result_class", "")) == playlistCase.expectResultClass,
            playlistCase.label);
      CHECK(p.value("raw_count", -1) == playlistCase.expectRawCount, "raw_count recorded");
      CHECK(p.value("normalized_count", -1) == playlistCase.expectNormalizedCount,
            "normalized_count recorded");
      CHECK(p.value("skipped_count", -1) == playlistCase.expectSkippedCount,
            "skipped_count recorded");
      CHECK(std::string(p.value("playlist_shape_class", "")) == "non_empty_list",
            "raw shape was a non-empty array");
    }
    db.Close();
    std::cout << "  [ok] playlist counterexample: " << playlistCase.label << std::endl;
  }

  // ── 7b. Legal empty list still passes: counts all zero, normalized
  // status=1, round usable. ────────────────────────────────────────────────
  {
    echo::storage::Database db;
    db.Open(TestDbPath(L"pl-empty-ok"));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-ple");

    LoginHttpGet get = [](const std::string&,
                          const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"is_vip":0,"vip_type":0,"busi_vip":[]}})", ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"info":[],"total":0}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    CHECK(data.value("usable_for_selection", false), "legal empty list round stays usable");
    CHECK(data.value("playlist_verdict", "") == "both_succeed_no_config_change",
          "legal empty playlist is still a success on both families");
    const auto& probes = data["probes"];
    for (int i = 2; i < 4; ++i) {
      const auto& p = probes[i];
      CHECK(p.value("normalized_status", 0) == 1, "legal empty list normalizes to status=1");
      CHECK(p.value("success_payload_valid", false), "legal empty list is a valid success");
      CHECK(p.value("response_contract_valid", false), "contract valid via success path");
      CHECK(std::string(p.value("result_class", "")) == "success", "result class success");
      CHECK(p.value("raw_count", -1) == 0, "raw_count zero");
      CHECK(p.value("normalized_count", -1) == 0, "normalized_count zero");
      CHECK(p.value("skipped_count", -1) == 0, "skipped_count zero");
      CHECK(std::string(p.value("playlist_shape_class", "")) == "empty_list",
            "shape class empty_list");
    }
    db.Close();
  }
  std::cout << "  [ok] legal empty playlist still passes with zero counts" << std::endl;

  // ── 8. account_has_rights counterexamples (2026-09-15 review finding 3):
  // a non-empty busi_vip array is NOT evidence of current rights; expired
  // history must be false; insufficient info must be unknown. success_
  // payload_valid stays true whenever the structure itself is valid. ───────
  struct VipRightsCase {
    const wchar_t* dbName;
    const char* label;
    const char* payload;
    bool expectRights;
    const char* expectState;
  };
  const VipRightsCase vipCases[] = {
      // 审阅夹具：历史到期（is_vip=0 + 2020 到期）不得标 true；结构仍有效。
      {L"vip-expired-history", "expired history is not current rights",
       R"({"status":1,"data":{"is_vip":0,"vip_type":0,"busi_vip":[{"product_type":"music","is_vip":0,"vip_end_time":"2020-01-01 00:00:00"}]}})",
       false, "none"},
      // 正面过期证据（is_vip=1 但日期已过）→ expired，仍不是当前权益。
      {L"vip-expired-flag", "positive but expired evidence",
       R"({"status":1,"data":{"is_vip":0,"vip_type":0,"busi_vip":[{"product_type":"svip","is_vip":1,"vip_end_time":"2020-06-01 12:00:00"}]}})",
       false, "expired"},
      // 信息不足：条目缺 is_vip → unknown，不得标 true。
      {L"vip-insufficient", "insufficient entry info is unknown",
       R"({"status":1,"data":{"is_vip":0,"vip_type":0,"busi_vip":[{"product_type":"music"}]}})",
       false, "unknown"},
      // 当前有效：音乐类 is_vip=1 且缺省到期 = 上游“无限期”语义。
      {L"vip-active", "music-class entry with missing expiry is active",
       R"({"status":1,"data":{"is_vip":1,"vip_type":1,"busi_vip":[{"product_type":"music","is_vip":1}]}})",
       true, "active"},
      // 有效未过期到期时间 → active。
      {L"vip-active-future", "valid unexpired end date is active",
       R"({"status":1,"data":{"is_vip":1,"vip_type":1,"busi_vip":[{"product_type":"musicpack","is_vip":1,"vip_end_time":"2030-01-01 00:00:00"}]}})",
       true, "active"},
  };
  for (const auto& vipCase : vipCases) {
    echo::storage::Database db;
    db.Open(TestDbPath(vipCase.dbName));
    db.Initialize();
    SeedRegisteredDevice(db);
    SeedSession(db, "snap-token-vr");

    LoginHttpGet get = [&vipCase](const std::string&,
                                  const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, vipCase.payload, ""};
    };
    LoginHttpPost post = [](const std::string&, const std::string&,
                            const std::unordered_map<std::string, std::string>&) {
      return HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    const auto resp = echo::core::HandleSignatureFamilyProbe(db, get, post, 45000);
    const auto& data = resp.body["data"];
    const auto& probes = data["probes"];
    for (int i = 0; i < 2; ++i) {
      const auto& p = probes[i];
      CHECK(p.value("success_payload_valid", false), "structure stays valid");
      CHECK(p.value("account_has_rights", !vipCase.expectRights) == vipCase.expectRights,
            vipCase.label);
      CHECK(std::string(p.value("account_has_rights_state", "")) == vipCase.expectState,
            vipCase.label);
    }
    db.Close();
    std::cout << "  [ok] vip rights counterexample: " << vipCase.label << std::endl;
  }

  std::cout << "[SignatureFamilyProbe] All tests passed!" << std::endl;
  return 0;
}
