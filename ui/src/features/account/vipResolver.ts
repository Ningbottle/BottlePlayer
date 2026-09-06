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
 *   3) tvip-only → 免费用户（不解锁）
 * 到期时间：仅在 active 时返回"最晚且未过期"的那条；过期/非法串一律不落。
 *
 * @param d get_union_vip 响应的 data 字段
 * @param nowMs 当前时间戳（传入而非内部 Date.now()，便于测试）
 */
export function resolveVip(d: any, nowMs: number = Date.now()): VipResolution {
  if (!d) {
    return { isVip: false, vipStatus: 'unknown', vipEndDate: '', vipLevel: 0, vipType: 0 };
  }

  const vipLevel = Number(d.svip_level || d.vip_level || 0);
  const vipType = Number(d.vip_type || 0);

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
      // 音乐类权益（svip/music/musicpack）未过期 → 解锁歌曲；tvip 是听书，不解锁。
      const unlocksSongs = ['svip', 'music', 'musicpack'].includes(String(b.product_type || ''));
      const bIsVip = b.is_vip === 1 || b.is_vip === '1';
      if (!unlocksSongs || !bIsVip) continue;
      const state = endDateState(b.vip_end_time);
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

  return {
    isVip,
    vipStatus,
    vipEndDate,
    vipLevel,
    vipType,
    nickname: d.nickname,
    pic: d.pic,
  };
}
