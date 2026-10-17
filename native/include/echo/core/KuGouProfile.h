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

// 日志/诊断用的人类可读 profile 名：按 appid/clientver/saltKind 反查预置表，
// 返回 "standard" / "concept"；都匹配不上时返回 "custom"。
// 禁止在日志里手写 profile 名——必须经此函数或 GetKuGouProfile 派生。
std::string KuGouProfileName(const KuGouProfileParams& profile);

// 日志/诊断用盐名："lite" / "standard"。
const char* KuGouSaltKindName(KuGouSaltKind kind);

// 返回概念版 URL 硬编码常量（后续可替换为 MakcRe dataMap 动态化）
ConceptUrlParams GetConceptUrlParams();

// 扫码登录专用 appid（/v2/qrcode 要求 appid=1001 或 1014；取 1001）
inline constexpr const char* QrLoginAppId = "1001";

// /v5/url 专用 clientver（MakcRe song_url.js dataMap 显式覆盖概念版默认值 11440）
inline constexpr const char* V5UrlClientver = "11430";

// 项目全局基线。
constexpr KuGouEdition kProjectEdition = KuGouEdition::Concept;

// tvip（每日领取发放的那一档）能否解锁一首付费歌的完整播放。
//
// 默认 false，即"不确定时按不能播处理"。09-29 那次 tvip 有效时 /v5/url 返回
// priv_status=0、fail_process=["pkg","buy"]，只能回退 60 秒试听；唯一的 v6 探测
// 又在 7.3 秒时超时，因此"tvip 可完整播放"至今没有被一手证据证实。
//
// 这个常量只影响文案，不参与取链：一首歌到底播多长，永远只看该首歌自己的
// delivery。改为 true 需要 A0 重测给出 v6 明文完整链接的实测记录。
inline constexpr bool kTvipUnlocksPlayback = false;

}  // namespace echo::core
