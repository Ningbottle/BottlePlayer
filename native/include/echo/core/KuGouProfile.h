#pragma once

#include <string>

namespace echo::core {

// 项目身份：本项目只服务概念版；Standard 仅作为对照诊断存在。
// 注意：MakcRe 里的 "lite" 是 platform 名，不是业务 edition；不放进这里。
enum class KuGouEdition {
  Concept,
  Standard,
};

// 签名盐种类：与 edition 解耦，由 profile 决定，调用方不直接选。
enum class KuGouSaltKind {
  Lite,      // KuGou Android lite/concept 客户端使用
  Standard,  // KuGou Android 标准客户端使用
};

// ── 概念版 / 标准版 核心参数 ─────────────────────────────────────────────
struct KuGouProfileParams {
  std::string appid;       // "3116" / "1005"
  std::string clientver;   // "11440" / "20489"
  std::string busiType;    // "concept" / ""
  KuGouSaltKind saltKind;  // Lite / Standard
};

// 概念版 URL 硬编码常量（集中化，防止散落在 SongUrlService 各处）
struct ConceptUrlParams {
  std::string pageId;   // e.g. "967177915"
  std::string pid;    // e.g. "411"
  std::string ppageId; // e.g. "356753938,823673182,967485191"
};

// 返回给定 edition 的固定参数。
// 这些字面量是项目中唯一允许出现 3116 / 1005 / 11440 / 20489 的地方。
KuGouProfileParams GetKuGouProfile(KuGouEdition edition);

// 返回概念版 URL 硬编码常量（后续可替换为 MakcRe dataMap 动态化）
ConceptUrlParams GetConceptUrlParams();

// 扫码登录专用 appid（/v2/qrcode 要求 appid=1001 或 1014；取 1001）
inline constexpr const char* QrLoginAppId = "1001";

// /v5/url 专用 clientver（MakcRe song_url.js dataMap 显式覆盖概念版默认值 11440）
inline constexpr const char* V5UrlClientver = "11430";

// 项目全局基线。
constexpr KuGouEdition kProjectEdition = KuGouEdition::Concept;

}  // namespace echo::core
