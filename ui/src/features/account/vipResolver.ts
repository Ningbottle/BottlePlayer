/**
 * VIP 状态解析 —— 从 userStore.checkLoginStatus 抽出的纯函数。
 *
 * 背景：酷狗 get_union_vip 返回的 VIP 信息有多个来源，旧的"顶层 is_vip=1 短路"
 * 逻辑会显示过期的顶层时间、忽略 busi_vip 里有效的 SVIP 时间。这里实现
 * "扫描所有来源，取最晚且未过期的到期时间"的正确逻辑。
 *
 * Stage 5a（vip-stability 整改计划）：判真必须有未过期证据。
 *   - 顶层 is_vip=1/vip_type>0 只有在"存在有效且未过期的到期时间"时才判真；
 *     到期时间缺失或非法（非空但解析失败）→ 无可信证据，不判真。
 *   - busi_vip 音乐类（svip/music/musicpack）保留"缺省到期=无限期"的既有上游
 *     语义（广告临时 SVIP），但**非空且解析失败**的日期串不得当永久证据。
 *     其他 product_type（如 tvip）不解锁音乐；用途未以一手协议证实。
 *   - 三态语义（active/expired/unknown），供 UI 区分表述：
 *       active  → 至少一条有效且未过期的证据（isVip=true）；
 *       expired → 存在曾为 VIP 的正面证据（顶层付费标志或音乐类 busi_vip
 *                 is_vip=1），但所有日期证据均已过期 —— UI 才可以说"已过期"；
 *       unknown → 无可信证据（字段缺失、日期非法、空数据）—— UI 只能用中性
 *                 表述（如"权益状态未知/同步中"），不得声称"已过期"或"无权益"。
 *   - 非法/过期日期串一律不落 vipEndDate（避免把垃圾数据展示给用户）。
 *
 * 设计为纯函数（输入 data + 当前时间戳，输出解析结果），便于单元测试，
 * 不依赖 Tauri invoke / Date.now() 的副作用。
 */

/** 三态语义：UI 表述约束见文件头注释。 */
export type VipStatus = 'active' | 'expired' | 'unknown';

/** Music-class busi_vip product types that unlock song privileges in this app.
 *  tvip / other types are NOT in this list. Their product meaning is unconfirmed
 *  by first-party protocol evidence; comments elsewhere calling tvip "audiobook"
 *  are project assumptions, not verified definitions. */
export const MUSIC_UNLOCK_PRODUCT_TYPES = ['svip', 'music', 'musicpack'] as const;

/** One busi_vip entry as observed from authoritative detail — includes types
 *  that do NOT unlock music. Observation ≠ music VIP. */
export interface ObservedEntitlement {
  productType: string;
  isVip: boolean;
  vipEndDate: string;
  /** is_vip=1 and (missing end or end > now). Invalid non-empty dates → false. */
  active: boolean;
  /** product ∈ MUSIC_UNLOCK_PRODUCT_TYPES */
  unlocksMusic: boolean;
}

/** Music rights confirmation derived from the same snapshot.
 *  observed_non_music: there is an active busi_vip entry outside the music
 *  whitelist. UI copy must say「已观察到权益，音乐适用性待确认」— not
 *  “非音乐权益”, not music VIP, not “nothing granted”. Whitelist itself is
 *  unchanged pending first-party protocol / playback evidence. */
export type MusicPermission =
  | 'unknown'
  | 'active'
  | 'expired'
  | 'observed_non_music';

/** Clock-derived recompute of stored observations. Keeps productType/isVip/
 *  vipEndDate/unlocksMusic as historical snapshot fields; only `active` is
 *  re-evaliated against nowMs. Does not invent new upstream rows. */
export function recomputeObservedEntitlements(
  entries: ObservedEntitlement[],
  nowMs: number,
): ObservedEntitlement[] {
  return entries.map((e) => {
    if (!e.isVip) {
      return { ...e, active: false };
    }
    const raw = String(e.vipEndDate ?? '').trim();
    if (!raw) {
      // Upstream “no end” semantics for observation: keep active only if the
      // snapshot itself claimed active (no new evidence either way).
      return { ...e, active: e.active };
    }
    const ms = parseVipEndTime(raw);
    if (ms === 0) {
      return { ...e, active: false };
    }
    return { ...e, active: ms > nowMs };
  });
}

/** Derive display music-permission from clock-updated observations.
 *  An active music-whitelist row implies active music permission even when
 *  the store isVip flag has not been re-applied yet.
 *  observed_non_music UI copy:「已观察到权益，音乐适用性待确认」. */
export function deriveMusicPermission(
  entries: ObservedEntitlement[],
  opts: { musicVipActive: boolean; hadMusicExpiryEvidence: boolean },
): MusicPermission {
  if (opts.musicVipActive || entries.some((e) => e.active && e.unlocksMusic)) {
    return 'active';
  }
  if (opts.hadMusicExpiryEvidence) return 'expired';
  if (entries.some((e) => e.active && !e.unlocksMusic)) return 'observed_non_music';
  return 'unknown';
}

/** 解析结果：写入 userStore 的字段。 */
export interface VipResolution {
  isVip: boolean;
  vipStatus: VipStatus;
  /** 展示用的到期时间字符串（"YYYY-MM-DD HH:MM:SS" 原样保留，或空）。
   *  仅在 vipStatus==='active' 时非空，且必然是可解析的未过期时间。 */
  vipEndDate: string;
  vipLevel: number;
  vipType: number;
  /** /user/vip/detail 顺带返回的昵称/头像，供回填（无则空）。 */
  nickname?: string;
  pic?: string;
  /** All busi_vip entries from this snapshot (music + non-music). */
  observedEntitlements: ObservedEntitlement[];
  musicPermission: MusicPermission;
}

/**
 * 把 "YYYY-MM-DD HH:MM:SS" 解析为毫秒时间戳。
 * - 空串/无法解析 → 0
 * - 注意：原串带空格分隔，需替换为 'T' 才能被 Date 跨浏览器稳定解析
 * （调用方用 endDateState 区分"缺失"与"非法"，不再把 0 当"永久"）
 */
export function parseVipEndTime(s: unknown): number {
  const str = String(s || '');
  if (!str) return 0;
  const t = new Date(str.replace(' ', 'T')).getTime();
  return isNaN(t) ? 0 : t;
}

/** 单个到期时间的证据状态：缺失（空/未提供）、非法（非空但解析失败）、有效。 */
type EndDateState =
  | { kind: 'missing' }
  | { kind: 'invalid' }
  | { kind: 'valid'; ms: number; raw: string };

function endDateState(s: unknown): EndDateState {
  const raw = String(s ?? '').trim();
  if (!raw) return { kind: 'missing' };
  const ms = parseVipEndTime(raw);
  if (ms === 0) return { kind: 'invalid' };
  return { kind: 'valid', ms, raw };
}

/**
 * 解析 get_union_vip 的 data 对象，按以下规则判定 VIP 状态：
 *   1) 顶层 is_vip=1 / vip_type>0 且顶层到期时间有效未过期 → 付费 VIP
 *      （顶层日期缺失/非法 → 无可信证据，不判真）
 *   2) busi_vip[] 里 product_type ∈ {svip,music,musicpack} 且 is_vip=1：
 *        - 缺省/空到期时间 → 上游"无限期"语义 → 判真（既有行为，测试锁定）
 *        - 有效未过期 → 判真
 *        - 非空但解析失败 → 不判真（不得当永久）
 *        - 有效已过期 → 过期证据
 *   3) tvip / 其他非白名单 product_type：不解锁音乐（isVip 不受影响），
 *      但记入 observedEntitlements，musicPermission='observed_non_music'。
 *      用途未用一手协议证实；不得把注释当已证事实，也不得直接当 SVIP。
 * 到期时间：仅在 active 时返回"最晚且未过期"的那条；过期/非法串一律不落。
 *
 * @param d get_union_vip 响应的 data 字段
 * @param nowMs 当前时间戳（传入而非内部 Date.now()，便于测试）
 */
export function resolveVip(d: any, nowMs: number = Date.now()): VipResolution {
  const empty = (): VipResolution => ({
    isVip: false,
    vipStatus: 'unknown',
    vipEndDate: '',
    vipLevel: 0,
    vipType: 0,
    observedEntitlements: [],
    musicPermission: 'unknown',
  });
  if (!d) {
    return empty();
  }

  const vipLevel = Number(d.svip_level || d.vip_level || 0);
  const vipType = Number(d.vip_type || 0);
  const observedEntitlements: ObservedEntitlement[] = [];

  let isVip = false;
  let hasExpiredEvidence = false;
  let bestStr = '';
  let bestMs = -1;
  const considerValid = (raw: string, ms: number) => {
    if (ms > bestMs) {
      bestMs = ms;
      bestStr = raw;
    }
  };

  if (Array.isArray(d.busi_vip)) {
    for (const b of d.busi_vip) {
      if (!b) continue;
      const productType = String(b.product_type || '');
      const unlocksSongs = (MUSIC_UNLOCK_PRODUCT_TYPES as readonly string[]).includes(productType);
      const bIsVip = b.is_vip === 1 || b.is_vip === '1';
      const state = endDateState(b.vip_end_time);
      let active = false;
      if (bIsVip) {
        if (state.kind === 'missing') {
          active = true;
        } else if (state.kind === 'valid') {
          active = state.ms > nowMs;
        }
      }
      observedEntitlements.push({
        productType: productType || '(missing)',
        isVip: bIsVip,
        vipEndDate: state.kind === 'valid' || state.kind === 'invalid' ? String(b.vip_end_time ?? '') : '',
        active,
        unlocksMusic: unlocksSongs,
      });

      // Music-class unlock (existing Stage 5a rules). tvip/other types skip here.
      if (!unlocksSongs || !bIsVip) continue;
      if (state.kind === 'missing') {
        // 缺省/空：上游"无限期"语义（广告临时 SVIP），保留既有判真行为。
        isVip = true;
      } else if (state.kind === 'valid') {
        if (state.ms > nowMs) {
          isVip = true;
          considerValid(state.raw, state.ms);
        } else {
          hasExpiredEvidence = true;
        }
      }
      // state.kind === 'invalid'：非空但解析失败 → 无可信证据，不判真（Stage 5a）。
    }
  }

  // 顶层付费标志：判真必须有"有效且未过期"的到期时间证据（Stage 5a）。
  const topFlag = d.is_vip === 1 || d.is_vip === '1' || Number(d.vip_type) > 0;
  if (topFlag) {
    const topState = endDateState(d.vip_end_time ?? d.end_time);
    if (topState.kind === 'valid') {
      if (topState.ms > nowMs) {
        isVip = true;
        considerValid(topState.raw, topState.ms);
      } else {
        hasExpiredEvidence = true;
      }
    }
    // missing/invalid：无可信证据 → 不判真，也不计入过期证据。
  }

  const vipStatus: VipStatus = isVip
    ? 'active'
    : hasExpiredEvidence
      ? 'expired'
      : 'unknown';
  // 非法/过期日期串一律不落 vipEndDate；仅 active 时返回最晚未过期的那条。
  const vipEndDate = isVip ? bestStr : '';

  const activeNonMusic = observedEntitlements.some((e) => e.active && !e.unlocksMusic);
  const musicPermission: MusicPermission = isVip
    ? 'active'
    : hasExpiredEvidence
      ? 'expired'
      : activeNonMusic
        ? 'observed_non_music'
        : 'unknown';

  return {
    isVip,
    vipStatus,
    vipEndDate,
    vipLevel,
    vipType,
    nickname: d.nickname,
    pic: d.pic,
    observedEntitlements,
    musicPermission,
  };
}

/** Last network snapshot used to derive clock-sensitive display state. */
export interface VipSnapshot {
  isVip: boolean;
  vipStatus: VipStatus;
  vipEndDate: string;
}

/** Display state that can change as wall-clock time crosses vipEndDate. */
export interface LiveVipView {
  isVip: boolean;
  vipStatus: VipStatus;
  remainingLabel: string;
  urgent: boolean;
}

function formatRemainingLabel(diffMs: number): string {
  const minutes = Math.floor(diffMs / 60_000);
  if (minutes < 60) return `剩 ${Math.max(1, minutes)} 分钟`;
  const hours = Math.floor(minutes / 60);
  return `剩 ${hours} 小时 ${minutes % 60} 分`;
}

/**
 * Derive the VIP the UI should show *now* from a stored snapshot.
 * Network refresh is not required for expiry: a cached active VIP whose
 * end date has passed is expired, not "about to expire".
 */
export function liveVipView(snapshot: VipSnapshot, nowMs: number): LiveVipView {
  const raw = String(snapshot.vipEndDate ?? '').trim();
  const parsed = raw ? parseVipEndTime(raw) : 0;
  const invalidDate = raw.length > 0 && parsed === 0;

  if (snapshot.isVip && invalidDate) {
    return {
      isVip: false,
      vipStatus: 'unknown',
      remainingLabel: '权益状态未知',
      urgent: false,
    };
  }

  if (snapshot.isVip) {
    if (!raw) {
      return {
        isVip: true,
        vipStatus: 'active',
        remainingLabel: '无期限',
        urgent: false,
      };
    }
    const diff = parsed - nowMs;
    if (diff <= 0) {
      return {
        isVip: false,
        vipStatus: 'expired',
        remainingLabel: '会员已过期',
        urgent: false,
      };
    }
    return {
      isVip: true,
      vipStatus: 'active',
      remainingLabel: formatRemainingLabel(diff),
      urgent: diff < 3_600_000,
    };
  }

  if (snapshot.vipStatus === 'expired') {
    return {
      isVip: false,
      vipStatus: 'expired',
      remainingLabel: '会员已过期',
      urgent: false,
    };
  }

  return {
    isVip: false,
    vipStatus: 'unknown',
    remainingLabel: '权益状态未知',
    urgent: false,
  };
}
