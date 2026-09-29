import { describe, it, expect } from 'vitest';
import {
  resolveVip,
  parseVipEndTime,
  liveVipView,
  recomputeObservedEntitlements,
  deriveMusicPermission,
} from '../vipResolver';

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
    expect(resolveVip(null, NOW)).toEqual({
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
      vipLevel: 0,
      vipType: 0,
      observedEntitlements: [],
      musicPermission: 'unknown',
    });
    expect(resolveVip(undefined, NOW)).toEqual({
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
      vipLevel: 0,
      vipType: 0,
      observedEntitlements: [],
      musicPermission: 'unknown',
    });
    expect(resolveVip({}, NOW).isVip).toBe(false);
    expect(resolveVip({}, NOW).musicPermission).toBe('unknown');
  });
});

describe('resolveVip — 观察字段时钟重算（不解锁）', () => {
  it('recomputeObservedEntitlements: 跨截止后 active=false，历史字段保留', () => {
    const entries = [
      {
        productType: 'tvip',
        isVip: true,
        vipEndDate: FUTURE,
        active: true,
        unlocksMusic: false,
      },
    ];
    const still = recomputeObservedEntitlements(entries, NOW);
    expect(still[0].active).toBe(true);
    const past = recomputeObservedEntitlements(entries, parseVipEndTime(FUTURE) + 1);
    expect(past[0].active).toBe(false);
    expect(past[0].productType).toBe('tvip');
    expect(past[0].vipEndDate).toBe(FUTURE);
  });

  it('deriveMusicPermission: 观察到权益适用性待确认，而非“非音乐权益”断言', () => {
    const activeOther = [
      { productType: 'tvip', isVip: true, vipEndDate: FUTURE, active: true, unlocksMusic: false },
    ];
    expect(
      deriveMusicPermission(activeOther, { musicVipActive: false, hadMusicExpiryEvidence: false }),
    ).toBe('observed_non_music');
    const expiredOther = recomputeObservedEntitlements(activeOther, parseVipEndTime(FUTURE) + 1);
    expect(
      deriveMusicPermission(expiredOther, { musicVipActive: false, hadMusicExpiryEvidence: false }),
    ).toBe('unknown');
  });
});

describe('resolveVip — 观察字段（不解锁，用途未确认）', () => {
  it('tvip 未过期：isVip=false，observed active=true unlocksMusic=false，musicPermission=observed_non_music', () => {
    const r = resolveVip(
      {
        is_vip: 0,
        busi_vip: [{ product_type: 'tvip', is_vip: 1, vip_end_time: FUTURE }],
      },
      NOW,
    );
    expect(r.isVip).toBe(false);
    expect(r.musicPermission).toBe('observed_non_music');
    expect(r.observedEntitlements).toHaveLength(1);
    expect(r.observedEntitlements[0]).toMatchObject({
      productType: 'tvip',
      isVip: true,
      active: true,
      unlocksMusic: false,
      vipEndDate: FUTURE,
    });
  });

  it('tvip 已过期：不计入 active 观察，musicPermission 非 observed_non_music', () => {
    const r = resolveVip(
      {
        is_vip: 0,
        busi_vip: [{ product_type: 'tvip', is_vip: 1, vip_end_time: PAST }],
      },
      NOW,
    );
    expect(r.isVip).toBe(false);
    expect(r.observedEntitlements[0].active).toBe(false);
    expect(r.musicPermission).not.toBe('observed_non_music');
  });

  it('tvip is_vip=0：观察为 inactive，不构成音乐权限', () => {
    const r = resolveVip(
      {
        is_vip: 0,
        busi_vip: [{ product_type: 'tvip', is_vip: 0, vip_end_time: FUTURE }],
      },
      NOW,
    );
    expect(r.isVip).toBe(false);
    expect(r.observedEntitlements[0].active).toBe(false);
    expect(r.musicPermission).toBe('unknown');
  });

  it('fixture 形态（tvip 有效 + svip 失效 + 顶层 0）：不解锁音乐，但观察到 tvip', () => {
    const r = resolveVip(
      {
        is_vip: 0,
        vip_type: 0,
        busi_vip: [
          { product_type: 'tvip', is_vip: 1, vip_end_time: '2026-09-19 16:51:19' },
          { product_type: 'svip', is_vip: 0, vip_end_time: '2026-09-15 20:19:08' },
        ],
      },
      Date.parse('2026-09-18T16:51:19+08:00'),
    );
    expect(r.isVip).toBe(false);
    expect(r.musicPermission).toBe('observed_non_music');
    expect(r.observedEntitlements.find((e) => e.productType === 'tvip')?.active).toBe(true);
    expect(r.observedEntitlements.find((e) => e.productType === 'svip')?.active).toBe(false);
  });

  it('music 未过期时 musicPermission=active，tvip 不改变解锁结论', () => {
    const r = resolveVip(
      {
        is_vip: 0,
        busi_vip: [
          { product_type: 'tvip', is_vip: 1, vip_end_time: FUTURE },
          { product_type: 'music', is_vip: 1, vip_end_time: FUTURE },
        ],
      },
      NOW,
    );
    expect(r.isVip).toBe(true);
    expect(r.musicPermission).toBe('active');
  });
});

describe('resolveVip — Stage 5a: 判真必须有未过期证据（vipStatus 三态）', () => {
  it('顶层 is_vip=1 但到期时间已过期 → isVip=false 且 vipStatus="expired"（不再判真）', () => {
    const r = resolveVip({ is_vip: 1, vip_type: 1, vip_end_time: PAST }, NOW);
    expect(r.isVip).toBe(false);
    expect(r.vipStatus).toBe('expired');
    expect(r.vipEndDate).toBe('');
  });

  it('顶层 is_vip=1 但到期时间非法 → isVip=false，vipStatus="unknown"，非法串不落 vipEndDate', () => {
    const r = resolveVip({ is_vip: 1, vip_type: 1, vip_end_time: 'not-a-date' }, NOW);
    expect(r.isVip).toBe(false);
    expect(r.vipStatus).toBe('unknown');
    expect(r.vipEndDate).toBe('');
    expect(r.vipEndDate).not.toContain('not-a-date');
  });

  it('顶层 is_vip=1 但完全没有到期时间 → 无可信证据，isVip=false 且 "unknown"', () => {
    const r = resolveVip({ is_vip: 1, vip_type: 1 }, NOW);
    expect(r.isVip).toBe(false);
    expect(r.vipStatus).toBe('unknown');
  });

  it('busi_vip svip is_vip=1 但到期时间非法 → 不判真（不得当永久）', () => {
    const r = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: 'not-a-date' }] },
      NOW,
    );
    expect(r.isVip).toBe(false);
    expect(r.vipStatus).toBe('unknown');
  });

  it('busi_vip music 非法日期 → 不解锁', () => {
    const r = resolveVip(
      { is_vip: 0, busi_vip: [{ product_type: 'music', is_vip: 1, vip_end_time: 'garbage-date' }] },
      NOW,
    );
    expect(r.isVip).toBe(false);
  });

  it('有效未过期证据 → vipStatus="active"', () => {
    const r = resolveVip({ is_vip: 1, vip_end_time: FUTURE }, NOW);
    expect(r.vipStatus).toBe('active');
    expect(r.isVip).toBe(true);
  });

  it('权威 is_vip=0（明确无权益）→ isVip=false，无过期表述依据 → "unknown"', () => {
    const r = resolveVip({ is_vip: 0, vip_type: 0 }, NOW);
    expect(r.isVip).toBe(false);
    expect(r.vipStatus).toBe('unknown');
  });

  it('顶层过期 + busi_vip svip 未过期 → active（有效证据仍在）', () => {
    const svipEnd = '2026-06-16 12:00:00';
    const r = resolveVip(
      { is_vip: 1, vip_end_time: PAST, busi_vip: [{ product_type: 'svip', is_vip: 1, vip_end_time: svipEnd }] },
      NOW,
    );
    expect(r.isVip).toBe(true);
    expect(r.vipStatus).toBe('active');
    expect(r.vipEndDate).toBe(svipEnd);
  });
});

describe('resolveVip — 边界', () => {
  it('null/undefined data → 非 VIP 空状态', () => {
    expect(resolveVip(null, NOW)).toEqual({
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
      vipLevel: 0,
      vipType: 0,
      observedEntitlements: [],
      musicPermission: 'unknown',
    });
    expect(resolveVip(undefined, NOW)).toEqual({
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
      vipLevel: 0,
      vipType: 0,
      observedEntitlements: [],
      musicPermission: 'unknown',
    });
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

describe('liveVipView — 缓存权益随时间转换，不把过期写成即将到期', () => {
  const endDate = '2026-09-15 20:19:08';
  const threeMinutesBefore = Date.parse('2026-09-15T20:16:08');
  const atExpiry = Date.parse('2026-09-15T20:19:08');
  const afterExpiry = Date.parse('2026-09-15T20:19:09');

  it('到期前仍显示剩余分钟，并保持有效', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: endDate },
      threeMinutesBefore,
    );
    expect(live.isVip).toBe(true);
    expect(live.vipStatus).toBe('active');
    expect(live.remainingLabel).toBe('剩 3 分钟');
    expect(live.remainingLabel).not.toContain('即将到期');
  });

  it('等于到期时刻起视为已过期，不再显示即将到期', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: endDate },
      atExpiry,
    );
    expect(live.isVip).toBe(false);
    expect(live.vipStatus).toBe('expired');
    expect(live.remainingLabel).toBe('会员已过期');
    expect(live.remainingLabel).not.toContain('即将到期');
  });

  it('超过到期后，即使缓存仍是会员，也显示已过期', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: endDate },
      afterExpiry,
    );
    expect(live.isVip).toBe(false);
    expect(live.vipStatus).toBe('expired');
    expect(live.remainingLabel).toBe('会员已过期');
  });

  it('无到期时间的有效缓存视为无期限，不因时钟变成过期', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: '' },
      afterExpiry,
    );
    expect(live.isVip).toBe(true);
    expect(live.vipStatus).toBe('active');
    expect(live.remainingLabel).toBe('无期限');
  });

  it('非法到期时间不得当成永久会员', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: 'not-a-date' },
      threeMinutesBefore,
    );
    expect(live.isVip).toBe(false);
    expect(live.vipStatus).toBe('unknown');
    expect(live.remainingLabel).toBe('权益状态未知');
  });

  it('权威已过期快照保持已过期表述', () => {
    const live = liveVipView(
      { isVip: false, vipStatus: 'expired', vipEndDate: '' },
      threeMinutesBefore,
    );
    expect(live.isVip).toBe(false);
    expect(live.vipStatus).toBe('expired');
    expect(live.remainingLabel).toBe('会员已过期');
  });

  it('未知快照保持中性表述', () => {
    const live = liveVipView(
      { isVip: false, vipStatus: 'unknown', vipEndDate: '' },
      threeMinutesBefore,
    );
    expect(live.isVip).toBe(false);
    expect(live.vipStatus).toBe('unknown');
    expect(live.remainingLabel).toBe('权益状态未知');
  });

  it('续领后的新截止时间重新显示剩余时间', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: '2026-09-15 23:19:08' },
      afterExpiry,
    );
    expect(live.isVip).toBe(true);
    expect(live.vipStatus).toBe('active');
    expect(live.remainingLabel).toMatch(/^剩 \d+ 小时/);
    expect(live.remainingLabel).not.toContain('已过期');
  });

  it('最后一小时内仍有效时标记为临近到期', () => {
    const live = liveVipView(
      { isVip: true, vipStatus: 'active', vipEndDate: endDate },
      Date.parse('2026-09-15T19:30:08'),
    );
    expect(live.isVip).toBe(true);
    expect(live.urgent).toBe(true);
    expect(live.remainingLabel).toBe('剩 49 分钟');
  });
});
