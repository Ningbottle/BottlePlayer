import { describe, it, expect } from 'vitest';
import { resolveVip, parseVipEndTime } from '../vipResolver';

// 固定的"现在"用于测试，避免依赖真实时间。2026-06-15 00:00:00 UTC+8。
const NOW = Date.UTC(2026, 5, 14, 16, 0, 0); // = 2026-06-15 00:00:00+08
// 一个明确在"现在"之后的到期时间（用于"未过期"场景）
const FUTURE = '2026-12-31 23:59:59';
// 一个明确在"现在"之前的到期时间（用于"已过期"场景）
const PAST = '2025-01-01 00:00:00';

describe('parseVipEndTime', () => {
  it('解析 "YYYY-MM-DD HH:MM:SS" 为有效时间戳', () => {
    expect(parseVipEndTime('2026-06-15 00:00:00')).toBeGreaterThan(0);
  });

  it('空串/undefined/null → 0', () => {
    expect(parseVipEndTime('')).toBe(0);
    expect(parseVipEndTime(undefined)).toBe(0);
    expect(parseVipEndTime(null)).toBe(0);
  });

  it('无法解析的字符串 → 0', () => {
    expect(parseVipEndTime('not-a-date')).toBe(0);
  });
});

describe('resolveVip — 规则 1: 顶层付费 VIP', () => {
  it('顶层 is_vip=1 即判为 VIP，即便没有 busi_vip', () => {
    const r = resolveVip({ is_vip: 1, vip_end_time: FUTURE }, NOW);
    expect(r.isVip).toBe(true);
    expect(r.vipEndDate).toBe(FUTURE);
  });

  it('顶层 vip_type>0 也判为 VIP（字符串 is_vip="1"）', () => {
    const r = resolveVip({ is_vip: '1', vip_type: 2, vip_end_time: FUTURE }, NOW);
    expect(r.isVip).toBe(true);
  });

  it('顶层 is_vip=0 且 vip_type=0 且无 busi_vip → 非 VIP', () => {
    const r = resolveVip({ is_vip: 0, vip_type: 0 }, NOW);
    expect(r.isVip).toBe(false);
  });

  it('vipLevel 取 svip_level 优先，否则 vip_level', () => {
    expect(resolveVip({ svip_level: 5, vip_level: 1 }, NOW).vipLevel).toBe(5);
    expect(resolveVip({ vip_level: 3 }, NOW).vipLevel).toBe(3);
    expect(resolveVip({}, NOW).vipLevel).toBe(0);
  });
});

describe('resolveVip — 规则 2: busi_vip svip 广告/临时 SVIP', () => {
  it('busi_vip svip 未过期 → isVip=true，到期时间取 svip 的', () => {
    const svipEnd = '2026-06-16 12:00:00'; // NOW 之后
    const r = resolveVip(
      { is_vip: 0, vip_end_time: PAST, busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: svipEnd }] },
      NOW,
    );
    expect(r.isVip).toBe(true);
    expect(r.vipEndDate).toBe(svipEnd);
  });

  it('busi_vip svip 已过期 → 不入选，isVip 保持顶层判定', () => {
    const r = resolveVip(
      { is_vip: 0, vip_end_time: PAST, busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: PAST }] },
      NOW,
    );
    // 顶层 is_vip=0，svip 过期，所以非 VIP；无活跃权益时 vipEndDate 置空（不再兜底展示死日期）
    expect(r.isVip).toBe(false);
    expect(r.vipEndDate).toBe('');
  });

  it('busi_vip svip 缺省 vip_end_time（永久/广告临时）→ 判为 VIP 且按最高优先', () => {
    const r = resolveVip(
      { is_vip: 0, vip_end_time: PAST, busi_vip: [{ product_type: 'svip', is_vip: 1 }] },
      NOW,
    );
    expect(r.isVip).toBe(true);
    // 空串不参与 consider（无 str），但顶层 PAST 过期不入选 → bestStr 空，兜底取顶层
    // 注意：此处验证行为与原实现一致 —— svip 无 endStr 时 consider('') 直接 return
  });

  it('busi_vip tvip 不解锁歌曲（product_type != svip）→ 不影响 isVip', () => {
    const r = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'tvip', is_vip: 1, vip_end_time: FUTURE }] },
      NOW,
    );
    expect(r.isVip).toBe(false);
  });

  it('busi_vip svip is_vip=0 → 不算 SVIP 激活', () => {
    const r = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'svip', is_vip: 0, vip_end_time: FUTURE }] },
      NOW,
    );
    expect(r.isVip).toBe(false);
  });

  it('busi_vip music/musicpack 未过期 → 解锁（每日免费听歌 VIP）', () => {
    const music = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'music', is_vip: 1, vip_end_time: FUTURE }] },
      NOW,
    );
    expect(music.isVip).toBe(true);
    expect(music.vipEndDate).toBe(FUTURE);

    const pack = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'musicpack', is_vip: 1, vip_end_time: FUTURE }] },
      NOW,
    );
    expect(pack.isVip).toBe(true);
  });

  it('busi_vip music 已过期 → 不解锁', () => {
    const r = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'music', is_vip: 1, vip_end_time: PAST }] },
      NOW,
    );
    expect(r.isVip).toBe(false);
  });
});

describe('resolveVip — 到期时间选取（最晚未过期）', () => {
  it('svip 时间晚于顶层 → 取 svip 的', () => {
    const svipEnd = '2027-01-01 00:00:00';
    const r = resolveVip(
      { is_vip: 1, vip_end_time: '2026-07-01 00:00:00', busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: svipEnd }] },
      NOW,
    );
    expect(r.vipEndDate).toBe(svipEnd);
  });

  it('svip 过期但顶层未过期 → 取顶层', () => {
    const r = resolveVip(
      { is_vip: 1, vip_end_time: FUTURE, busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: PAST }] },
      NOW,
    );
    expect(r.vipEndDate).toBe(FUTURE);
  });

  it('所有来源都过期 → vipEndDate 置空，界面按普通用户呈现', () => {
    const r = resolveVip(
      { is_vip: 0, vip_end_time: PAST, busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: PAST }] },
      NOW,
    );
    expect(r.vipEndDate).toBe('');
  });

  it('多个 svip 项 → 取最晚的', () => {
    const earlier = '2026-08-01 00:00:00';
    const later = '2026-10-01 00:00:00';
    const r = resolveVip(
      {
        is_vip: 0,
        busi_vip: [
          { product_type: 'svip', is_vip: 1, vip_end_time: earlier },
          { product_type: 'svip', is_vip: 1, vip_end_time: later },
        ],
      },
      NOW,
    );
    expect(r.vipEndDate).toBe(later);
  });
});

describe('resolveVip — 权威/未知输入', () => {
  it('权威 data 对象按业务规则解析', () => {
    const r = resolveVip({ is_vip: 1, vip_type: 1, vip_end_time: FUTURE }, NOW);
    expect(r.isVip).toBe(true);
    expect(r.vipEndDate).toBe(FUTURE);
  });

  it('未知/缺失 data 不能被当成一次权威的“确认无 VIP”输入', () => {
    expect(resolveVip(null, NOW)).toEqual({ isVip: false, vipEndDate: '', vipLevel: 0, vipType: 0 });
    expect(resolveVip(undefined, NOW)).toEqual({ isVip: false, vipEndDate: '', vipLevel: 0, vipType: 0 });
    expect(resolveVip({}, NOW).isVip).toBe(false);
  });
});

describe('resolveVip — 边界', () => {
  it('null/undefined data → 非 VIP 空状态', () => {
    expect(resolveVip(null, NOW)).toEqual({ isVip: false, vipEndDate: '', vipLevel: 0, vipType: 0 });
    expect(resolveVip(undefined, NOW)).toEqual({ isVip: false, vipEndDate: '', vipLevel: 0, vipType: 0 });
  });

  it('busi_vip 含 null 元素 → 跳过不崩', () => {
    const r = resolveVip(
      { is_vip: 0, busi_vip: [null, { product_type: 'svip', is_vip: 1, vip_end_time: FUTURE }] },
      NOW,
    );
    expect(r.isVip).toBe(true);
  });

  it('busi_vip 不是数组 → 忽略，不影响顶层判定', () => {
    const r = resolveVip({ is_vip: 1, vip_end_time: FUTURE, busi_vip: 'not-array' }, NOW);
    expect(r.isVip).toBe(true);
    expect(r.vipEndDate).toBe(FUTURE);
  });

  it('回填 nickname/pic 透传', () => {
    const r = resolveVip({ is_vip: 1, nickname: '酷友', pic: 'http://x/a.png' }, NOW);
    expect(r.nickname).toBe('酷友');
    expect(r.pic).toBe('http://x/a.png');
  });
});
