// EchoSongUrlContractTest — v5/v6 output shape, quality selection, preview/paid error, hash normalization.
// Extracted from basic_contract_tests.cpp (lines 3344-3413) for independent build.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/SongUrlService.h"
#include "echo/core/HttpClient.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

int main() {
  std::cout << "[SongUrlContract] started" << std::endl;
#if defined(_MSC_VER)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── Resolve public Interface shape contract ─────────────────────────
  // Pins the normalized output shape so P3 (SongUrlService refactor)
  // has a contract to validate against.
  std::cout << "[SongUrlContract] Testing Resolve output shape..." << std::endl;
  {
    echo::core::SongUrlService svc([](
        const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"hash":"ABC123","url":"http://cdn.example/abc.flac","backup_url":["http://cdn.example/bak.flac"],"fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","albumid":966846,"album_audio_id":32100650,"audio_id":20505418,"timeLength":269000,"bitRate":320,"extName":"flac","privilege":10,"pay_type":3})",
          ""};
    }, {});

    const auto result = svc.Resolve("ABC123", "", "");
    // Top-level shape
    assert(result.contains("status"));
    assert(result.contains("url"));
    assert(result.contains("data"));
    // data sub-shape: play_url, backup_url, hash, metadata
    assert(result["data"].contains("play_url"));
    assert(result["data"]["play_url"] == "http://cdn.example/abc.flac");
    assert(result["data"].contains("backup_url"));
    assert(result["data"]["backup_url"].is_array());
    assert(result["data"].contains("hash"));
    assert(result["data"].contains("song_name"));
    assert(result["data"].contains("singer_name"));
    assert(result["data"].contains("time_length"));
    assert(result["data"].contains("bit_rate"));
    assert(result["data"].contains("ext_name"));
    assert(result["data"].contains("privilege"));
    assert(result["data"].contains("pay_type"));
    assert(result["data"].contains("album_audio_id"));
    assert(result["data"].contains("audio_id"));
    assert(result["data"].contains("album_id"));

    std::cout << "  [ok] Resolve output shape contract" << std::endl;
  }

  // ── ResolveV6PrivUrl output shape ────────────────────────────────────
  std::cout << "[SongUrlContract] Testing ResolveV6PrivUrl output shape..." << std::endl;
  {
    // MUST inject POST mock — ResolveV6PrivUrl uses POST, not GET.
    // Without mock, falls to real HttpClient and test becomes unreliable.
    echo::core::SongUrlService svc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected GET in v6 test"};
        },
        [](const std::string&,
           const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[{"url":"http://cdn.example/vip-320.mp3","info":{"bitrate":320,"filesize":5000,"extname":"mp3","fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","timeLength":240000}},{"url":"http://cdn.example/vip-128.mp3","info":{"bitrate":128,"filesize":2000,"extname":"mp3","fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","timeLength":240000}}]})",
              ""};
        });

    echo::core::DeviceInfo device;
    device.dfid = "v6-dfid";
    device.guid = "v6-guid";
    const auto v6 = svc.ResolveV6PrivUrl("VIPHASH", "123", "42", "tok", "vipTok", 3, device);
    // Strong assertions: status must be 1, not just "contains status"
    assert(v6["status"] == 1);
    assert(v6["url"] == "http://cdn.example/vip-320.mp3");
    assert(v6["play_url"] == "http://cdn.example/vip-320.mp3");
    assert(v6.contains("data"));
    assert(v6["data"]["play_url"] == "http://cdn.example/vip-320.mp3");
    assert(v6["data"]["hash"] == "viphash");  // v6 normalizes to lowercase
    assert(v6["data"]["quality"] == "320");   // highest bitrate selected
    assert(v6["data"]["available_qualities"].size() == 2);

    std::cout << "  [ok] ResolveV6PrivUrl output shape contract (POST mock, strong assertions)" << std::endl;
  }

  // ── Resolve forwards the persisted VIP session identity to v6 ─────────
  std::cout << "[SongUrlContract] Testing VIP session forwarding..." << std::endl;
  {
    std::string capturedBody;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::SongUrlService svc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected GET"};
        },
        [&](const std::string&,
            const std::string& body,
            const std::unordered_map<std::string, std::string>& headers) {
          capturedBody = body;
          capturedHeaders = headers;
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[{"url":"http://cdn.example/yp/full/vip.flac","info":{"bitrate":320,"extname":"flac"}}]})",
              ""};
        });

    echo::core::DeviceInfo device;
    device.dfid = "registered-dfid-value";
    device.mid = "123456789012345678901234567890123456789";
    device.registered = true;
    const auto result = svc.Resolve(
        "VIPHASH", "0", "123", "320", "", "42", "normal-token",
        device, "vip-token", 3);
    const auto body = nlohmann::json::parse(capturedBody);
    assert(result.value("status", 0) == 1);
    assert(body.value("vip", 0) == 3);
    assert(body["tracker_param"].value("viptoken", std::string{}) == "vip-token");
    assert(body.value("token", std::string{}) == "normal-token");
    assert(capturedHeaders["dfid"] == device.dfid);
    assert(capturedHeaders["mid"] == device.mid);
    std::cout << "  [ok] VIP token, VIP type, token, and dfid forwarded" << std::endl;
  }

  // ── Stage 6a: quality switch must keep URL and is_preview consistent ──
  std::cout << "[SongUrlContract] Testing quality-switch preview-flag consistency..." << std::endl;
  {
    // v6 returns two qualities: 320 full (/full/) and 128 preview (/yp/p_).
    // The best-bitrate pick is the 320 full URL (is_preview=false). The
    // caller then requests quality=128 — the Resolve quality replacement
    // swaps the top-level URL to the 128 preview entry. Contract (plan 6a):
    // the output is_preview must be recomputed from the FINAL URL, and every
    // available_qualities entry must carry its own is_preview flag.
    echo::core::SongUrlService svc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected v5 fallback"};
        },
        [](const std::string&,
           const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[)"
              R"({"url":"http://cdn.example/yp/full/320.flac","info":{"bitrate":320,"filesize":9000,"extname":"flac"}},)"
              R"({"url":"http://cdn.example/yp/p_128/preview.flac","info":{"bitrate":128,"filesize":1000,"extname":"mp3"}}]})",
              ""};
        });

    echo::core::DeviceInfo device;
    device.dfid = "q-dfid";
    device.mid = "123456789012345678901234567890123456789";
    device.registered = true;
    const auto result = svc.Resolve(
        "MIXEDHASH", "0", "123", "128", "", "42", "normal-token",
        device, "vip-token", 3);

    // Pre-condition: the quality replacement selected the 128 preview URL.
    const auto finalUrl = result.value("url", std::string{});
    assert(finalUrl.find("/yp/p_") != std::string::npos);

    // 6a contract #1: is_preview must describe the FINAL url — a preview
    // clip must never be reported as full playback.
    assert(result.value("is_preview", false) == true);

    // 6a contract #2: every available_qualities entry carries its own
    // preview flag derived from its own URL shape.
    const auto& qualities = result["data"]["available_qualities"];
    assert(qualities.is_array() && qualities.size() == 2);
    assert(qualities[0].contains("is_preview") && qualities[0]["is_preview"] == false);
    assert(qualities[1].contains("is_preview") && qualities[1]["is_preview"] == true);

    // F10 元数据一致：切到 128/mp3 后，data 元数据描述最终条目（码率/扩展名
    // 随之重算），而不是停留在最高码率的 320/flac。
    assert(result["data"]["bit_rate"] == 128);
    assert(result["data"]["ext_name"] == "mp3");

    // The reported quality follows the switched selection.
    assert(result.value("quality", std::string{}) == "128");
    std::cout << "  [ok] quality switch keeps URL/preview flag consistent" << std::endl;
  }

  // ── Stage 6a (reverse): switching to a FULL url must declare full ─────
  std::cout << "[SongUrlContract] Testing reverse switch (preview best → full entry)..." << std::endl;
  {
    // Mirror direction: the best-bitrate pick is a MARKER-LESS synthetic
    // URL — under the current F9 heuristic (no /full/ → preview) it counts
    // as preview and is not "degraded" (no /yp/p_ marker), so the request
    // stays on the v6 path. NOTE for 6b: once the delivery matrix
    // reclassifies marker-less URLs as unknown, this fixture must be
    // revisited (a true /yp/p_ best would take the v5 fallback instead).
    // After the replacement the output must declare FULL playback
    // (is_preview=false) — the old implementation kept the preview flag
    // from the original pick.
    echo::core::SongUrlService svc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected v5 fallback"};
        },
        [](const std::string&,
           const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[)"
              R"({"url":"http://cdn.example/synth/320.flac","info":{"bitrate":320,"filesize":9000,"extname":"flac"}},)"
              R"({"url":"http://cdn.example/full/128.flac","info":{"bitrate":128,"filesize":1000,"extname":"mp3"}}]})",
              ""};
        });

    echo::core::DeviceInfo device;
    device.dfid = "q-dfid";
    device.mid = "123456789012345678901234567890123456789";
    device.registered = true;
    const auto result = svc.Resolve(
        "MIXEDHASH2", "0", "123", "128", "", "42", "normal-token",
        device, "vip-token", 3);

    // Pre-condition: the replacement selected the /full/ entry.
    const auto finalUrl = result.value("url", std::string{});
    assert(finalUrl.find("/full/") != std::string::npos);
    // 6a contract: a full URL must be declared full (is_preview=false).
    assert(result.value("is_preview", true) == false);
    std::cout << "  [ok] reverse switch (preview best → full entry) declares full playback" << std::endl;
  }

  // ── V6 quality selection: requested quality must be selected ──────────
  std::cout << "[SongUrlContract] Testing V6 quality selection..." << std::endl;
  {
    echo::core::SongUrlService svc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected v5 fallback"};
        },
        [](const std::string&,
           const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[{"url":"http://cdn.example/song-128.mp3","info":{"bitrate":128,"filesize":1000,"extname":"mp3","songName":"歌名","singerName":"歌手","timeLength":269000}},{"url":"http://cdn.example/song-320.mp3","info":{"bitrate":320,"filesize":2000,"extname":"mp3","songName":"歌名","singerName":"歌手","timeLength":269000}}]})",
              ""};
        });

    echo::core::DeviceInfo device;
    device.dfid = "q-dfid";
    device.guid = "q-guid";
    const auto result128 = svc.Resolve(
        "ABC123", "", "32100650", "128", "", "42", "tok", device);
    assert(result128["status"] == 1);
    assert(result128["url"] == "http://cdn.example/song-128.mp3");
    assert(result128["play_url"] == "http://cdn.example/song-128.mp3");
    assert(result128["data"]["quality"] == "128");
    assert(result128["data"]["available_qualities"].size() == 2);

    std::cout << "  [ok] V6 quality selection contract" << std::endl;
  }

  // ── Empty hash must return error ─────────────────────────────────────
  std::cout << "[SongUrlContract] Testing empty hash error..." << std::endl;
  {
    echo::core::SongUrlService svc;
    const auto emptyResult = svc.Resolve("", "", "");
    assert(emptyResult["status"] == 0);
    assert(emptyResult.contains("error"));
    assert(!emptyResult["error"].get<std::string>().empty());

    std::cout << "  [ok] Empty hash returns error" << std::endl;
  }

  // ── Hash normalization: case-insensitive + trimmed ───────────────────
  std::cout << "[SongUrlContract] Testing hash normalization..." << std::endl;
  {
    std::string capturedUrl;
    echo::core::SongUrlService svc([&capturedUrl](
        const std::string& url,
        const std::unordered_map<std::string, std::string>&) {
      capturedUrl = url;
      return echo::core::HttpResult{
          200,
          R"({"status":1,"url":"http://cdn.example/abc.mp3","backup_url":[]})",
          ""};
    }, {});

    svc.Resolve("  ABCdef123  ", "", "");
    // The v5 URL should contain the lowercased, trimmed hash
    assert(capturedUrl.find("abcdef123") != std::string::npos);

    std::cout << "  [ok] Hash normalization contract" << std::endl;
  }

  // Stage 6b / B1-3 / B1-R4: classify the response that owns the selected URL.
  //
  // The duration unit is read from the FIELD NAME only — never inferred from
  // magnitude — AND only when the EXACT (endpoint, response-level, field)
  // combination it was read from is listed in SongUrlService's explicit
  // evidence table. A duration observed at one endpoint/level is NOT
  // generalised into a contract for every endpoint or every nested object.
  //
  // As of this batch the table holds exactly ONE row:
  //
  //   (v5 /url, Envelope, "timeLength") -> seconds (×1000)
  //     evidence: outputs/vip-stability-audit-2026-09-05/
  //               live-session-probe-results.json
  //               song.data.raw.timeLength = 294 beside
  //               song.data.raw.hash_offset.end_ms = 60000 on a real ~5 min
  //               track; `raw` is the v5 envelope (SongUrlService.cpp raw =
  //               upstream), so the sample is (v5, Envelope). Reading 294 as
  //               millis would put the 60000 ms window far past the whole
  //               track — not self-consistent — hence seconds.
  //
  // EVERY other combination is UNEVIDENCED and supplies NO unit basis:
  //   * v6 /v6/priv_url at Envelope / Data / Item / Info;
  //   * v5 /url at Data (the v5 side only ever passes its envelope).
  // With a window opening at zero and no independent clip evidence, an
  // unevidenced source therefore yields "unknown" — never "full", and the
  // single v5 sample must not be promoted into a v6/full contract.
  //
  // Aliases / unevidenced spellings never supply the unit themselves:
  //   duration     seconds  seconds on Catalog/Playlist/Rank; never captured
  //                         on /song/url.
  //   time_length  seconds  PROJECT ALIAS emitted by this service from
  //                         timeLength (BuildSongUrlOutput / quality switch).
  //   timelen      millis   PROJECT ALIAS built by Catalog/Playlist/Rank as
  //                         duration*1000.
  //   timelength   seconds  unverified spelling; never captured anywhere.
  // They are read ONLY to catch a self-contradictory payload: once a basis is
  // established, any of these present in the same response must agree with it,
  // else the unit is unresolved (nullopt) and the caller must not decide.
  struct DeliveryCase {
    const char* url;
    nlohmann::json evidence;
    const char* expectedV5;  // /v5/url surface (envelope evidence IS sourced)
    const char* expectedV6;  // /v6/priv_url surface (no sourced evidence)
    const char* why;
  };
  const DeliveryCase deliveryCases[] = {
      // ── No marker at all ────────────────────────────────────────────────
      {"https://cdn.example/opaque.mp3", {}, "unknown", "unknown", "no path marker, no clip evidence"},
      {"https://cdn.example/opaque.mp3?redirect=/full/song", {}, "unknown", "unknown", "/full/ only in the query"},
      {"https://cdn.example/full/song.mp3", {}, "full", "full", "explicit /full/ path, no clip evidence"},

      // ── Independent clip evidence wins over the path ────────────────────
      {"https://cdn.example/yp/p_0_999/song.mp3", {}, "preview", "preview", "/yp/p_ clip path"},
      {"https://cdn.example/full/song.mp3", {{"fail_process", {"pkg", "buy"}}}, "preview", "preview", "fail_process pkg/buy"},
      {"https://cdn.example/opaque.mp3", {{"fail_process", 12}}, "preview", "preview", "fail_process 12"},
      {"https://cdn.example/opaque.mp3", {{"hash_offset", {{"start_ms", 30000}, {"end_ms", 90000}}}}, "preview", "preview", "window opens past 0"},
      {"https://cdn.example/full/song.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 60000}}}, {"timeLength", 240}}, "preview", "unknown", "60000ms of a 240s track (basis on v5 only)"},

      // ── SOURCED basis: (v5 envelope) timeLength is second-valued ────────
      {"https://cdn.example/full/sourced-full.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timeLength", 294}}, "full", "unknown", "whole 294s track (v5 sourced; v6 unevidenced)"},
      {"https://cdn.example/full/sourced-short-full.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 30000}}}, {"timeLength", 30}}, "full", "unknown", "whole 30s track (v5 sourced; v6 unevidenced)"},
      // B1 review RED: landing end_ms on the millisecond reading of a
      // second-valued field is not proof of a whole track — 294 ms of a 294 s
      // track is a truncation, so this must never be reported as full.
      {"https://cdn.example/full/sourced-red.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294}}}, {"timeLength", 294}}, "preview", "unknown", "294ms of a 294s track (v5); v6 has no basis"},
      {"https://cdn.example/full/sourced-outruns.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 999999}}}, {"timeLength", 294}}, "unknown", "unknown", "window outruns the track"},
      {"https://cdn.example/full/sourced-negative.mp3", {{"hash_offset", {{"start_ms", -1}, {"end_ms", 60000}}}, {"timeLength", 294}}, "unknown", "unknown", "invalid window"},
      // An agreeing alias adds no conflict.
      {"https://cdn.example/full/sourced-alias-agrees.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timeLength", 294}, {"time_length", 294}}, "full", "unknown", "agreeing alias keeps the v5 basis; v6 unevidenced"},

      // ── UNEVIDENCED source: alias alone states no unit ──────────────────
      {"https://cdn.example/full/unresolved.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 60000}}}}, "unknown", "unknown", "no duration field"},
      {"https://cdn.example/full/alias-timelen.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 269000}}}, {"timelen", 269000}}, "unknown", "unknown", "timelen is a project alias"},
      {"https://cdn.example/full/alias-time_length.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"time_length", 294}}, "unknown", "unknown", "time_length is a project alias"},
      {"https://cdn.example/full/alias-duration.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"duration", 294}}, "unknown", "unknown", "duration never captured on /song/url"},

      // ── CONFLICT: the sourced field disagrees with another field ────────
      {"https://cdn.example/full/conflict.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timeLength", 294}, {"timelen", 294}}, "unknown", "unknown", "294000ms vs 294ms"},
      {"https://cdn.example/full/conflict2.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timeLength", 294}, {"time_length", 600}}, "unknown", "unknown", "294000ms vs 600000ms"},

      // ── B1-R4 source discrimination (RED while sources are unseparated) ─
      // (a) v5 envelope evidence says full; (b) the very same payload on the
      //     v6 surface must stay unknown — the single v5 sample is NOT a v6
      //     contract.
      {"https://cdn.example/full/src-a-b.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timeLength", 294}}, "full", "unknown", "(a) v5 envelope sourced -> full; (b) same payload on v6 -> unknown"},
      // (d) an evidenced source contradicted by a second documented spelling.
      {"https://cdn.example/full/src-d-conflict.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timeLength", 294}, {"duration", 600}}, "unknown", "unknown", "(d) v5 basis contradicted by duration"},
      // (e) an alias alone states nothing on either surface.
      {"https://cdn.example/full/src-e-alias.mp3", {{"hash_offset", {{"start_ms", 0}, {"end_ms", 294000}}}, {"timelen", 294000}}, "unknown", "unknown", "(e) alias alone -> unknown on both surfaces"},

      // ── Abnormal windows stay unclaimed ─────────────────────────────────
      {"https://cdn.example/full/impossible.mp3", {{"hash_offset", {{"start_ms", 90000}, {"end_ms", 30000}}}}, "unknown", "unknown", "end <= start"},
  };
  int deliveryFailures = 0;
  for (const auto& c : deliveryCases) {
    auto payload = c.evidence.is_object() ? c.evidence : nlohmann::json::object();
    payload["status"] = 1;
    // Present the selected URL as a single-element array so BOTH surfaces
    // expose a top-level delivery AND a data.available_qualities entry; the
    // source-model expectation must hold at every one of those locations.
    payload["url"] = nlohmann::json::array({c.url});
    echo::core::SongUrlService svc(
        [&](const auto&, const auto&) { return echo::core::HttpResult{200, payload.dump(), ""}; },
        [&](const auto&, const auto&, const auto&) { return echo::core::HttpResult{200, payload.dump(), ""}; });
    const auto v6 = svc.ResolveV6PrivUrl("hash", "", "", "", "", 0, {});
    echo::core::SongUrlService v5svc(
        [&](const auto&, const auto&) { return echo::core::HttpResult{200, payload.dump(), ""}; },
        {});
    const auto v5 = v5svc.Resolve("hash", "", "");
    // Report every mismatch instead of aborting on the first one: a RED run has
    // to show the whole affected surface, not a single line.
    auto expect = [&](const std::string& surface, const nlohmann::json& holder,
                      const char* expected) {
      const std::string actual = holder.value("delivery", "missing");
      const bool actualPreview = holder.value("is_preview", false);
      const bool wantPreview = std::string(expected) == "preview";
      if (actual == expected && actualPreview == wantPreview) return;
      std::fprintf(stderr,
                   "[SongUrlContract] delivery FAILED [%s] %s: expected %s(is_preview=%d) got %s(is_preview=%d) — %s\n",
                   surface.c_str(), c.url, expected, wantPreview ? 1 : 0,
                   actual.c_str(), actualPreview ? 1 : 0, c.why);
      ++deliveryFailures;
    };
    auto expectAll = [&](const std::string& name, const char* expected,
                         const nlohmann::json& result) {
      expect(name, result, expected);
      if (!result.contains("data")) return;
      expect(name + ".data", result["data"], expected);
      if (!result["data"].contains("available_qualities")) return;
      int index = 0;
      for (const auto& entry : result["data"]["available_qualities"]) {
        expect(name + ".qualities[" + std::to_string(index) + "]", entry, expected);
        ++index;
      }
    };
    expectAll("v6", c.expectedV6, v6);
    expectAll("v5", c.expectedV5, v5);
  }
  if (deliveryFailures != 0) {
    std::fprintf(stderr, "[SongUrlContract] %d delivery mismatch(es)\n", deliveryFailures);
  }

  // ── B1-R4 (c): a non-evidenced LEVEL must not establish the unit ───────
  // On the v6 surface the duration is carried by data[].info / the data[]
  // element, never by an evidenced (endpoint, level); on the v5 surface only
  // the ENVELOPE is ever read, so a nested duration is invisible. In both
  // cases the unit stays unestablished → unknown (never full).
  {
    int levelFailures = 0;
    // v6 data-array payload: timeLength lives in info and on the data element.
    const char* v6Payload = R"({"status":1,"data":[
      {"url":["https://cdn.example/full/level-info.mp3"],
       "info":{"bitrate":320,"timeLength":294}}],
      "hash_offset":{"start_ms":0,"end_ms":294000}})";
    echo::core::SongUrlService v6svc(
        [](const auto&, const auto&) { return echo::core::HttpResult{500, "{}", "unexpected GET"}; },
        [&](const auto&, const auto&, const auto&) { return echo::core::HttpResult{200, v6Payload, ""}; });
    const auto v6 = v6svc.ResolveV6PrivUrl("hash", "", "", "", "", 0, {});
    auto expectUnknownV6 = [&](const std::string& surface, const nlohmann::json& holder) {
      const std::string actual = holder.value("delivery", "missing");
      const bool actualPreview = holder.value("is_preview", false);
      if (actual == "unknown" && actualPreview == false) return;
      std::fprintf(stderr,
                   "[SongUrlContract] source-level FAILED [v6] %s: expected unknown(is_preview=0) got %s(is_preview=%d) — timeLength in info/data[] is an unevidenced level\n",
                   surface.c_str(), actual.c_str(), actualPreview ? 1 : 0);
      ++levelFailures;
    };
    expectUnknownV6("v6", v6);
    if (v6.contains("data")) {
      expectUnknownV6("v6.data", v6["data"]);
      if (v6["data"].contains("available_qualities")) {
        int index = 0;
        for (const auto& entry : v6["data"]["available_qualities"]) {
          expectUnknownV6("v6.qualities[" + std::to_string(index) + "]", entry);
          ++index;
        }
      }
    }

    // v5 payload whose duration is NESTED (not on the envelope). The v5 side
    // only ever reads the envelope, so this too must stay unknown.
    const char* v5Payload = R"({"status":1,
      "url":["https://cdn.example/full/level-v5-nested.mp3"],
      "data":{"timeLength":294},
      "hash_offset":{"start_ms":0,"end_ms":294000}})";
    echo::core::SongUrlService v5svc(
        [&](const auto&, const auto&) { return echo::core::HttpResult{200, v5Payload, ""}; }, {});
    const auto v5 = v5svc.Resolve("hash", "", "");
    const std::string v5Actual = v5.value("delivery", "missing");
    const bool v5Preview = v5.value("is_preview", false);
    if (v5Actual != "unknown" || v5Preview != false) {
      std::fprintf(stderr,
                   "[SongUrlContract] source-level FAILED [v5] v5: expected unknown(is_preview=0) got %s(is_preview=%d) — nested (non-envelope) timeLength is an unevidenced level\n",
                   v5Actual.c_str(), v5Preview ? 1 : 0);
      ++levelFailures;
    }
    if (levelFailures == 0) {
      std::cout << "  [ok] source-level discrimination (v6 info/data, v5 nested) stays unknown" << std::endl;
    } else {
      std::fprintf(stderr, "[SongUrlContract] %d source-level mismatch(es)\n", levelFailures);
      deliveryFailures += levelFailures;
    }
  }

  // Per-entry restrictions override the path; they must not bleed into siblings.
  {
    echo::core::SongUrlService svc({}, [](const auto&, const auto&, const auto&) {
      return echo::core::HttpResult{200, R"({"status":1,"data":[
        {"url":"https://cdn.example/full/high","info":{"bitrate":320}},
        {"url":"https://cdn.example/full/low","info":{"bitrate":128,"fail_process":12}}
      ]})", ""};
    });
    const auto result = svc.Resolve("hash", "", "", "128", "", "", "", {});
    assert(result.value("delivery", "missing") == "preview");
    assert(result["data"]["available_qualities"][0].value("delivery", "missing") == "full");
    assert(result["data"]["available_qualities"][1].value("delivery", "missing") == "preview");
    assert(result["attempts"][0]["fail_process"] == 12);
  }
  // A main-path rejection cannot relabel a successful anonymous full stream.
  {
    int calls = 0;
    echo::core::SongUrlService svc([&](const auto&, const auto&) {
      return echo::core::HttpResult{200, ++calls == 1
        ? R"({"status":2,"errcode":20018,"fail_process":["pkg","buy"]})"
        : R"({"status":1,"url":"https://cdn.example/full/free.mp3"})", ""};
    }, {});
    const auto result = svc.Resolve("hash", "", "");
    assert(calls == 2);
    assert(result.value("delivery", "missing") == "full");
    assert(!result.value("is_preview", true));
  }

  // Stage 6c: retain failed attempts, including transport/parse failures.
  {
    int calls = 0;
    echo::core::SongUrlService svc([&](const auto&, const auto&) {
      return echo::core::HttpResult{200, ++calls == 1
        ? R"({"status":2,"errcode":20018,"fail_process":["pkg","buy"],"hash_offset":{"offset_hash":"clip","end_ms":60000}})"
        : R"({"status":1,"url":"https://cdn.example/clip?token=private"})", ""};
    }, {});
    const auto result = svc.Resolve("hash", "", "", "128", "", "42", "secret", {});
    const auto attempts = result.value("attempts", nlohmann::json::array());
    assert(attempts.size() == 2);
    assert(attempts[0]["endpoint"] == "v5-main");
    assert(attempts[0]["http_status"] == 200);
    assert(attempts[0]["errcode"] == 20018);
    assert(attempts[0]["fail_process"] == nlohmann::json({"pkg", "buy"}));
    assert(attempts[0]["anonymous"] == false);
    assert(attempts[1]["endpoint"] == "v5-preview");
    assert(attempts[1]["anonymous"] == true);
    assert(attempts[1]["delivery"] == "preview");
    assert(attempts[1]["quality"] == "128");
    assert(attempts.dump().find("private") == std::string::npos);
    assert(attempts.dump().find("secret") == std::string::npos);
    assert(result["data"]["attempts"] == attempts);
    assert(result["data"]["raw"]["status"] == 1);
  }
  {
    int calls = 0;
    echo::core::SongUrlService svc([&](const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200, calls == 1 ? R"({"status":2,"errcode":20018})" : "invalid JSON", ""};
    }, [](const auto&, const auto&, const auto&) {
      return echo::core::HttpResult{403, R"({"errcode":20028})", ""};
    });
    const auto result = svc.Resolve("hash", "", "");
    const auto attempts = result.value("attempts", nlohmann::json::array());
    assert(attempts.size() == 3);
    assert(attempts[0]["endpoint"] == "v6");
    assert(attempts[0]["http_status"] == 403);
    assert(attempts[0]["errcode"] == 20028);
    assert(attempts[1]["errcode"] == 20018);
    assert(attempts[2]["endpoint"] == "v5-anon");
    assert(attempts[2]["error"] == "invalid_json");
    assert(result["status"] == 0);
  }
  {
    echo::core::SongUrlService svc([](const auto&, const auto&) {
      return echo::core::HttpResult{0, "", "transport failure"};
    }, [](const auto&, const auto&, const auto&) {
      return echo::core::HttpResult{0, "", "transport failure"};
    });
    const auto result = svc.Resolve("hash", "", "");
    const auto attempts = result.value("attempts", nlohmann::json::array());
    assert(attempts.size() == 2);
    assert(attempts[0]["error"] == "transport");
    assert(attempts[1]["error"] == "transport");
  }
  {
    echo::core::SongUrlService svc([](const auto&, const auto&) {
      return echo::core::HttpResult{200, R"({"status":2,"errcode":20018})", ""};
    }, [](const auto&, const auto&, const auto&) {
      return echo::core::HttpResult{200, R"({"status":1,"data":[{"url":"https://cdn.example/yp/p_0_99/clip","info":{"bitrate":128}}]})", ""};
    });
    const auto result = svc.Resolve("hash", "", "");
    assert(result["status"] == 1);
    assert(result["delivery"] == "preview");
    assert(result.value("attempts", nlohmann::json::array()).size() == 3);
  }

  // Owner-log four-step fixture (09-08): v6 20018/token api error → v5 MAIN
  // 20018 → ANON pkg/buy + 0–60000 ms → PREVIEW /yp/p_…/ → local status=1.
  // Synthetic minimal payloads from visible facts; not a full upstream body.
  // Note: SongUrlHttpGet is (url, headers) — inspect the signed URL, not a params map.
  {
    int getCalls = 0;
    echo::core::SongUrlService svc(
        [&](const std::string& url, const auto&) {
          ++getCalls;
          const bool hasToken = url.find("token=") != std::string::npos
              && url.find("token=&") == std::string::npos;
          const bool isFreePart = url.find("IsFreePart=1") != std::string::npos;
          if (hasToken) {
            return echo::core::HttpResult{200,
              R"({"status":2,"error_code":20018,"errcode":20018,"message":"token api error."})", ""};
          }
          if (!isFreePart) {
            return echo::core::HttpResult{200,
              R"({"status":2,"error_code":20018,"fail_process":["pkg","buy"],"hash_offset":{"offset_hash":"cliphash","start_ms":0,"end_ms":60000}})", ""};
          }
          return echo::core::HttpResult{200,
            R"({"status":1,"url":"https://cdn.example/yp/p_0_960131/safe.mp3","extName":"mp3","bitRate":128})", ""};
        },
        [](const auto&, const auto&, const auto&) {
          return echo::core::HttpResult{200,
            R"({"status":0,"error_code":20018,"errcode":20018,"message":"token api error."})", ""};
        });
    const auto result = svc.Resolve("ownerhash", "1", "1", "128", "", "42", "tok", {});
    assert(result.value("status", 0) == 1);
    assert(result.value("delivery", "") == "preview");
    assert(result.value("is_preview", false) == true);
    const auto url = result.value("url", std::string{});
    assert(url.find("/yp/p_") != std::string::npos);
    assert(url.find("safe.mp3") != std::string::npos);
    const auto attempts = result.value("attempts", nlohmann::json::array());
    assert(attempts.size() >= 3);
    bool sawV6Auth = false, sawMainAuth = false, sawPreview = false;
    for (const auto& a : attempts) {
      if (!a.is_object()) continue;
      if (a.value("endpoint", "") == "v6" && a.value("errcode", 0) == 20018) {
        assert(a.value("reject_class", "") == "auth_rejected");
        assert(a.value("message", std::string{}).find("token api error") != std::string::npos);
        sawV6Auth = true;
      }
      if (a.value("endpoint", "") == "v5-main" && a.value("errcode", 0) == 20018) {
        assert(a.value("reject_class", "") == "auth_rejected");
        sawMainAuth = true;
      }
      if (a.value("endpoint", "") == "v5-preview" && a.value("delivery", "") == "preview") {
        sawPreview = true;
      }
    }
    assert(sawV6Auth && sawMainAuth && sawPreview);
    // status=1 preview is playable result, not VIP success.
    assert(result.value("delivery", "") == "preview");
    assert(getCalls >= 2);
    std::cout << "  [ok] owner-log four-step auth→preview fixture" << std::endl;
  }

  // Bare 20018 with no URL must not be classified as missing VIP package.
  {
    echo::core::SongUrlService svc(
        [](const auto&, const auto&) {
          return echo::core::HttpResult{200,
            R"({"status":2,"errcode":20018,"message":"token api error."})", ""};
        },
        [](const auto&, const auto&, const auto&) {
          return echo::core::HttpResult{200,
            R"({"status":0,"errcode":20018,"message":"token api error."})", ""};
        });
    const auto result = svc.Resolve("hash", "", "", "128", "", "42", "tok", {});
    assert(result.value("status", 1) == 0);
    assert(result.value("error_code", "") == "native_song_auth_rejected");
    assert(result.value("vip_required", true) == false);
    assert(result.value("error", std::string{}).find("VIP 音乐包") == std::string::npos);
    const auto attempts = result.value("attempts", nlohmann::json::array());
    assert(!attempts.empty());
    bool anyAuth = false;
    for (const auto& a : attempts) {
      if (a.is_object() && a.value("reject_class", "") == "auth_rejected") anyAuth = true;
    }
    assert(anyAuth);
    std::cout << "  [ok] bare 20018 classifies as auth_rejected, not vip_required" << std::endl;
  }

  if (deliveryFailures != 0) {
    std::cout << "[SongUrlContract] failed" << std::endl;
    return 1;
  }
  std::cout << "[SongUrlContract] All tests passed!" << std::endl;
  return 0;
}
