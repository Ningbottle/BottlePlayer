/**
 * VIP 状态解析 —— 从 userStore.checkLoginStatus 抽出的纯函数。
 *
 * 背景：酷狗 get_union_vip 返回的 VIP 信息有多个来源，旧的"顶层 is_vip=1 短路"
 * 逻辑会显示过期的顶层时间、忽略 busi_vip 里有效的 SVIP 时间。这里实现
 * "扫描所有来源，取最晚且未过期的到期时间"的正确逻辑。
 *
 * 设计为纯函数（输入 data + 当前时间戳，输出解析结果），便于单元测试，
 * 不依赖 Tauri invoke / Date.now() 的副作用。
 */

/** 解析结果：写入 userStore 的字段。 */
export interface VipResolution {
  isVip: boolean;
  /** 展示用的到期时间字符串（"YYYY-MM-DD HH:MM:SS" 原样保留，或空）。 */
  vipEndDate: string;
  vipLevel: number;
  vipType: number;
  /** /user/vip/detail 顺带返回的昵称/头像，供回填（无则空）。 */
  nickname?: string;
  pic?: string;
}

/**
 * 把 "YYYY-MM-DD HH:MM:SS" 解析为毫秒时间戳。
 * - 空串/无法解析 → 0（调用方约定 0 表示"永久或无法解析"，按最高优先处理）
 * - 注意：原串带空格分隔，需替换为 'T' 才能被 Date 跨浏览器稳定解析
 */
export function parseVipEndTime(s: unknown): number {
  const str = String(s || '');
  if (!str) return 0;
  const t = new Date(str.replace(' ', 'T')).getTime();
  return isNaN(t) ? 0 : t;
}

/**
 * 解析 get_union_vip 的 data 对象，按以下规则判定 VIP 状态：
 *   1) 顶层 is_vip=1 或 vip_type>0 → 付费 VIP
 *   2) busi_vip[] 里 product_type="svip" 且 is_vip=1 且未过期 → 广告/临时 SVIP（解锁歌曲）
 *   3) tvip-only → 免费用户（不解锁）
 * 到期时间：扫描所有有效来源，取"最晚且未过期"的；0（永久/无法解析）按最高优先。
 *
 * @param d get_union_vip 响应的 data 字段
 * @param nowMs 当前时间戳（传入而非内部 Date.now()，便于测试）
 */
export function resolveVip(d: any, nowMs: number = Date.now()): VipResolution {
  if (!d) {
    return { isVip: false, vipEndDate: '', vipLevel: 0, vipType: 0 };
  }

  const vipLevel = Number(d.svip_level || d.vip_level || 0);
  const vipType = Number(d.vip_type || 0);

  let isVip = d.is_vip === 1 || d.is_vip === '1' || Number(d.vip_type) > 0;

  let bestStr = '';
  let bestRank = -1;
  const consider = (str: string) => {
    if (!str) return;
    const ms = parseVipEndTime(str);
    // 永久/无法解析 → 最高优先；否则用其时间戳参与比较
    const rank = ms === 0 ? Number.MAX_SAFE_INTEGER : ms;
    if ((ms === 0 || ms > nowMs) && rank > bestRank) {
      bestRank = rank;
      bestStr = str;
    }
  };

  if (Array.isArray(d.busi_vip)) {
    for (const b of d.busi_vip) {
      if (!b) continue;
      // 音乐类权益（svip/music/musicpack）未过期 → 解锁歌曲；tvip 是听书，不解锁。
      const unlocksSongs = ['svip', 'music', 'musicpack'].includes(String(b.product_type || ''));
      const bIsVip = b.is_vip === 1 || b.is_vip === '1';
      if (unlocksSongs && bIsVip) {
        const endStr = String(b.vip_end_time || '');
        if (!endStr || parseVipEndTime(endStr) === 0 || parseVipEndTime(endStr) > nowMs) {
          isVip = true;
          consider(endStr);
        }
      }
    }
  }
  // 顶层时间作为候选（过期则不入选）
  consider(String(d.vip_end_time || d.end_time || ''));

  // 无活跃权益时不再回退到已死的顶层时间（会把过期 VIP 显示成有效日期）；
  // bestStr 为空 = 当前无有效 VIP，界面按"普通用户"呈现。
  const vipEndDate = bestStr;

  return {
    isVip,
    vipEndDate,
    vipLevel,
    vipType,
    nickname: d.nickname,
    pic: d.pic,
  };
}
