// R-external (agent B, 2026-09-28): read-only real-environment probe for the
// upstream playhistory pagination contract (bp cursor field semantics).
//
// Safety protocol (per audit handoff):
//   - The real database is COPIED to a work dir; the original file is never
//     opened or written by this probe. The app must not be running.
//   - Only GET /user/history (a read-only route the app itself calls) is
//     sent, at most twice (page 1, then page 2 with the discovered cursor).
//   - Credentials (session token) are never printed or written to the
//     sanitized capture; song values are redacted — only field NAMES and
//     structure survive.
//   - Raw upstream responses stay in the work dir for forensic review and
//     are never printed.
//
// Usage: bp_pagination_probe.exe <real_app_data_dir> <sanitized_output.json>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "echo/core/C_API.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string ReadCResponse(char* raw) {
  std::string s = raw ? raw : "";
  if (raw) EchoFreeString(raw);
  return s;
}

bool LooksLikeCursorKey(const std::string& key) {
  for (const char* candidate : {"bp", "cursor", "max_time", "last_time",
                                "end_time", "next", "timestamp", "time"}) {
    if (key == candidate) return true;
  }
  return false;
}

// Redact all leaf values: the sanitized capture keeps structure + key names
// (incl. array length) but no personal content.
json Redact(const json& node, int depth = 0) {
  if (depth > 8 || node.is_primitive()) {
    if (node.is_string()) return "<redacted:string>";
    if (node.is_number()) return "<redacted:number>";
    return "<redacted>";
  }
  if (node.is_array()) {
    json out = json::array();
    if (!node.empty()) out.push_back(Redact(node[0], depth + 1));
    return out;  // shape: one redacted sample element
  }
  json out = json::object();
  for (auto it = node.begin(); it != node.end(); ++it) {
    out[it.key()] = Redact(it.value(), depth + 1);
  }
  return out;
}

void CollectKeyNames(const json& node, std::set<std::string>& keys,
                     int depth = 0) {
  if (depth > 6) return;
  if (node.is_object()) {
    for (auto it = node.begin(); it != node.end(); ++it) {
      keys.insert(it.key());
      CollectKeyNames(it.value(), keys, depth + 1);
    }
  } else if (node.is_array() && !node.empty()) {
    CollectKeyNames(node[0], keys, depth + 1);
  }
}

std::string SendGet(const char* path, const std::string& queryJson) {
  char* raw = nullptr;
  EchoHandleRequest("GET", path, queryJson.c_str(), "{}", "", &raw);
  return ReadCResponse(raw);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: bp_pagination_probe.exe <real_app_data_dir> <sanitized_out.json>\n";
    return 2;
  }
  const fs::path sourceDir = argv[1];
  const fs::path sanitizedOut = argv[2];

  if (!fs::exists(sourceDir / "bottlemusic.db")) {
    std::cerr << "no bottlemusic.db under source dir; nothing to probe\n";
    return 2;
  }

  const fs::path workDir = sanitizedOut.parent_path() /
                           ("bp-probe-work-" +
                            std::to_string(
                                std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()));
  std::error_code ec;
  fs::create_directories(workDir, ec);
  fs::copy_file(sourceDir / "bottlemusic.db", workDir / "bottlemusic.db",
                fs::copy_options::overwrite_existing, ec);
  if (ec) {
    std::cerr << "db copy failed: " << ec.message() << "\n";
    return 2;
  }
  for (const char* sidecar : {"-wal", "-shm"}) {
    fs::path src = sourceDir / ("bottlemusic.db" + std::string(sidecar));
    if (fs::exists(src)) {
      std::error_code ec2;
      fs::copy_file(src, workDir / ("bottlemusic.db" + std::string(sidecar)),
                    fs::copy_options::overwrite_existing, ec2);
    }
  }
  std::cout << "[probe] db copied to " << workDir.string()
            << " (original untouched)\n";

  json report;
  report["probe"] = "bp_pagination_probe";
  report["readonly"] = true;
  report["upstream"] = "gateway.kugou.com/playhistory/v1/get_songs";

  if (EchoInitializeWithPathsV2(workDir.string().c_str()) != 0) {
    report["init"] = "failed";
    std::ofstream(sanitizedOut) << report.dump(2);
    return 1;
  }

  // Page 1: minimal read-only request (2 entries to keep the sample small).
  const std::string resp1 = SendGet("/user/history", "{\"pagesize\":\"2\"}");
  {
    std::ofstream raw(workDir / "raw-page1.json");
    raw << resp1;
  }
  json r1 = json::parse(resp1, nullptr, false);
  report["page1_http_status"] = r1.value("status", -1);
  json page1 = json::object();
  if (r1.is_object() && r1.contains("body")) {
    const json& body = r1["body"];
    page1["redacted_body"] = Redact(body);
    if (body.is_object()) {
      page1["upstream_status"] = body.value("status", -1);
      if (body.contains("error_code")) page1["upstream_error_code"] = body["error_code"];
      if (body.contains("data") && body["data"].is_object()) {
        const json& data = body["data"];
        std::set<std::string> dataKeys;
        CollectKeyNames(data, dataKeys);
        page1["data_key_names"] = std::vector<std::string>(dataKeys.begin(),
                                                           dataKeys.end());
        if (data.contains("songs") && data["songs"].is_array()) {
          page1["songs_returned"] = data["songs"].size();
          std::set<std::string> songKeys;
          CollectKeyNames(data["songs"], songKeys);
          page1["song_field_names"] = std::vector<std::string>(songKeys.begin(),
                                                               songKeys.end());
          // Cursor discovery: which song-level fields carry plausible cursor
          // names? Values stay redacted; only names are reported.
          json candidates = json::array();
          if (!data["songs"].empty() && data["songs"][0].is_object()) {
            for (auto it = data["songs"][0].begin();
                 it != data["songs"][0].end(); ++it) {
              if (LooksLikeCursorKey(it.key())) {
                candidates.push_back(it.key());
              }
            }
          }
          page1["cursor_candidate_fields"] = candidates;
          // Top-level data cursor candidates (some APIs paginate at data level).
          json dataCandidates = json::array();
          for (auto it = data.begin(); it != data.end(); ++it) {
            if (LooksLikeCursorKey(it.key())) dataCandidates.push_back(it.key());
          }
          page1["data_level_cursor_candidates"] = dataCandidates;
        }
      }
    }
  }
  report["page1"] = page1;

  // Page 2: only if a cursor candidate exists at data level or a plausible
  // song-level field was found (bp round-trip is read-only; 2 GETs total).
  const std::string cursorField = page1.contains("data_level_cursor_candidates") &&
                                          !page1["data_level_cursor_candidates"].empty()
                                      ? page1["data_level_cursor_candidates"][0]
                                            .get<std::string>()
                                      : "";
  if (!cursorField.empty() && r1.is_object() && r1.contains("body") &&
      r1["body"].is_object() && r1["body"].contains("data") &&
      r1["body"]["data"].is_object()) {
    const std::string cursorValue =
        r1["body"]["data"].value(cursorField, "");
    if (!cursorValue.empty()) {
      json q = {{"pagesize", "2"}, {"bp", cursorValue}};
      const std::string resp2 = SendGet("/user/history", q.dump());
      {
        std::ofstream raw(workDir / "raw-page2.json");
        raw << resp2;
      }
      json r2 = json::parse(resp2, nullptr, false);
      json page2 = json::object();
      page2["bp_field_sent"] = cursorField;
      page2["bp_value_redacted"] = "<redacted>";
      page2["http_status"] = r2.value("status", -1);
      if (r2.is_object() && r2.contains("body") && r2["body"].is_object()) {
        const json& body2 = r2["body"];
        page2["upstream_status"] = body2.value("status", -1);
        if (body2.contains("data") && body2["data"].is_object() &&
            body2["data"].contains("songs") && body2["data"]["songs"].is_array()) {
          page2["songs_returned"] = body2["data"]["songs"].size();
          // Distinctness only: page 2 must not repeat page 1's entries.
          bool sameFirst = false;
          if (r1["body"].contains("data") &&
              r1["body"]["data"].contains("songs") &&
              !r1["body"]["data"]["songs"].empty() &&
              !body2["data"]["songs"].empty()) {
            sameFirst = r1["body"]["data"]["songs"][0] ==
                        body2["data"]["songs"][0];
          }
          page2["first_song_same_as_page1"] = sameFirst;
        }
      }
      report["page2_with_bp"] = page2;
    } else {
      report["page2_with_bp"] = "skipped: data-level cursor value empty";
    }
  } else {
    report["page2_with_bp"] =
        "skipped: no data-level cursor candidate found on page 1";
  }

  (void)EchoShutdown();
  std::ofstream out(sanitizedOut);
  out << report.dump(2);
  std::cout << "[probe] sanitized capture written to "
            << sanitizedOut.string() << "\n";
  std::cout << "[probe] raw responses retained in " << workDir.string()
            << " (not printed)\n";
  return 0;
}
