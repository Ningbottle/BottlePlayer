import { reactive } from 'vue';
import { describeBackendError } from '../../platform/tauri/nativeClient';
import {
  registerDevice,
  fetchUserDetail,
  fetchVipDetail,
  claimDailyVipSong,
  claimYouthVipAd,
  claimYouthDayVip,
  claimYouthDayVipUpgrade,
} from './accountGateway';
import { resolveVip, type VipStatus } from './vipResolver';
import {
  notifyAccountReady,
  notifyAccountCleared,
  notifyLocalLogout,
} from './accountEffects';

export interface UserState {
  isLoggedIn: boolean;
  deviceReady: boolean;
  userId: string;
  username: string;
  avatar: string;
  vipLevel: number;
  vipType: number;
  isVip: boolean;
  vipEndDate: string;
  /** Stage 5a 三态：active（有效未过期）/ expired（曾为 VIP 且全部日期已
   *  过期，UI 可说"已过期"）/ unknown（无可信证据，UI 只能中性表述）。
   *  由 resolveVip 产出，applyVipSnapshot 在 applied 时透传。 */
  vipStatus: VipStatus;
  loading: boolean;
  claimMessage: string;
  /** Stage 5b: 领取受理 ≠ 权益有效。通道成功后进入"待确认"，直到权威接口
   *  给出结论（生效/未生效）或重查耗尽（未确认）。 */
  vipClaimPending: boolean;
  /** Stage 5c: 当前领取阶段（''=空闲 / 'device' / 'ad' / 'fallback' /
   *  'confirm'），供 UI 展示与取消按钮的可见性。 */
  claimStage: '' | 'device' | 'ad' | 'fallback' | 'confirm';
  /** Stage 5c: 取消请求标志。领取流程在各阶段边界检查；由 cancelClaim() 置位。 */
  claimCancelRequested: boolean;
}

export const userStore = reactive<UserState>({
  isLoggedIn: false,
  deviceReady: false,
  userId: '',
  username: '未登录',
  avatar: '',
  vipLevel: 0,
  vipType: 0,
  isVip: false,
  vipStatus: 'unknown',
  vipEndDate: '',
  loading: false,
  claimMessage: '',
  vipClaimPending: false,
  claimStage: '',
  claimCancelRequested: false,
});

function resetVipState() {
  userStore.vipLevel = 0;
  userStore.vipType = 0;
  userStore.isVip = false;
  userStore.vipStatus = 'unknown';
  userStore.vipEndDate = '';
  userStore.vipClaimPending = false;
}

/** Stage 5c: 用户请求取消当前领取。在阶段边界（下一轮广告上报前、倒计时
 *  每 1s、下一 条兜底通道前、下一次确认重查前）生效：停止后续工作、
 *  loading 复位、消息如实说明"已取消"。已进入的 FFI 调用本身不可中断。 */
export function cancelClaim() {
  userStore.claimCancelRequested = true;
}

function resetLoginState() {
  userStore.isLoggedIn = false;
  userStore.deviceReady = false;
  userStore.userId = '';
  userStore.username = '未登录';
  userStore.avatar = '';
  resetVipState();
  // Account dropped: the composition-configured effects reconcile Library
  // state (clearing the in-memory favorite set) without the account store
  // knowing about it.
  notifyAccountCleared();
}

export interface VipDeviceResult {
  ok: boolean;
  error?: string;
}

function isUsableDfid(dfid: unknown): boolean {
  return typeof dfid === 'string' && dfid.trim() !== '' && dfid.trim() !== '-';
}

function isAuthoritativeVipDetail(detail: any): boolean {
  return !!detail
    && detail.status === 1
    && detail.authoritative !== false
    && detail.data
    && typeof detail.data === 'object';
}

function applyVipSnapshot(detail: any, opts: { allowDowngrade: boolean }): 'applied' | 'kept' | 'pending' {
  if (!isAuthoritativeVipDetail(detail)) {
    return 'kept';
  }
  const resolved = resolveVip(detail.data, Date.now());
  if (!opts.allowDowngrade && userStore.isVip && !resolved.isVip) {
    return 'pending';
  }
  userStore.vipLevel = resolved.vipLevel;
  userStore.vipType = resolved.vipType;
  userStore.isVip = resolved.isVip;
  // Stage 5a 三态透传：applied 时权威结论已落地，expired/unknown 由 UI
  // 按各自表述约束展示（expired 才能说"已过期"，unknown 只能中性表述）。
  userStore.vipStatus = resolved.vipStatus;
  userStore.vipEndDate = resolved.vipEndDate;
  if (resolved.nickname && !userStore.username.startsWith(resolved.nickname)) {
    userStore.username = resolved.nickname;
  }
  if (resolved.pic && !userStore.avatar) {
    userStore.avatar = resolved.pic;
  }
  return 'applied';
}

function formatDeviceGateFailure(result: VipDeviceResult): string {
  if (result.error === 'request_timeout') {
    return '领取失败：请求超时，请稍后重试';
  }
  if (result.error === 'circuit_open') {
    return '领取失败：服务暂时繁忙，请稍后重试';
  }
  return '领取失败：设备注册失败（设备未完成注册或指纹不可用）';
}

export function formatVipClaimFailure(result: any): string {
  const detail = [result?.error_msg, result?.error, result?.msg, result?.message]
    .find((value) => typeof value === 'string' && value.trim())?.trim() ?? '';
  const code = result?.error_code;
  const hasCode = code !== undefined && code !== null && String(code) !== '' && Number(code) !== 0;
  if (Number(code) === 130012) {
    return '今天已经领过了';
  }
  // 51002：领取/上报均被上游拒绝（VIP 到期与否都一样，实测）。原因未公开，
  // 可能是活动风控/频次限制——如实展示，不编造含义。
  if (Number(code) === 51002) {
    return '领取失败：酷狗活动暂不可领（错误码 51002），请稍后再试或在官方 App 内领取';
  }
  if (detail.includes('已领') || detail.includes('已经领')) {
    return '今天已经领过了';
  }
  if (detail) {
    const suffix = hasCode && !detail.includes(String(code)) ? `（错误码 ${code}）` : '';
    return `领取失败：${detail}${suffix}`;
  }
  if (hasCode) {
    return `领取失败：酷狗返回错误码 ${code}`;
  }
  return '领取失败：酷狗未返回具体原因';
}

export async function ensureVipDeviceReady(): Promise<VipDeviceResult> {
  try {
    // This is an ensure operation, not a device reset. The backend registers
    // an unregistered/legacy device and otherwise returns the persisted one.
    // Forcing every restored session makes r_register_dev rotate a valid dfid
    // and destabilizes protected APIs. A real 20017 still has one isolated
    // refresh/retry in the native user-playlist route.
    const result = await registerDevice();
    const data = result?.data && typeof result.data === 'object' ? result.data : {};
    const registered = data.registered === true;
    if (result?.status === 1 && registered && isUsableDfid(typeof data.dfid === 'string' ? data.dfid : undefined)) {
      userStore.deviceReady = true;
      return { ok: true };
    }
    userStore.deviceReady = false;
    return { ok: false, error: 'device_registration_failed' };
  } catch (error: any) {
    userStore.deviceReady = false;
    const message = error?.message || String(error);
    if (message.includes('request_timeout')) {
      return { ok: false, error: 'request_timeout' };
    }
    if (message.includes('circuit_open')) {
      return { ok: false, error: 'circuit_open' };
    }
    return { ok: false, error: 'device_registration_failed' };
  }
}

export async function checkLoginStatus() {
  userStore.loading = true;
  try {
    const detail = await fetchUserDetail();
    if (detail && detail.status === 1 && detail.data) {
      userStore.userId = String(detail.data.userid || '');
      userStore.username = detail.data.nickname || detail.data.username || '听歌用户';
      userStore.avatar = detail.data.pic || detail.data.avatar || '';
      userStore.deviceReady = false;

      // Register before any VIP claim so the request cannot race ahead with
      // the placeholder dfid="-". The backend is idempotent once registered.
      // /song/url returns full VIP audio (instead of 60s previews) and
      // /user/playlist stops returning error_code 20017. The backend is
      // idempotent — re-calls return the persisted device once registered=true.
      const deviceResult = await ensureVipDeviceReady();
      userStore.isLoggedIn = true;
      if (!deviceResult.ok) {
        console.warn('Device upgrade failed', deviceResult.error);
      } else {
        // Playlist-backed favorites use the same registered-device contract.
        // Publish login first, then reconcile after registration has completed.
        void notifyAccountReady(userStore.userId);
      }

      // VIP 解析抽到 vipResolver.resolveVip（纯函数，有单元测试覆盖）。
      // 规则摘要：顶层 is_vip/vip_type → 付费；busi_vip[svip] 未过期 → 临时 SVIP；
      // 到期时间取所有来源里"最晚且未过期"的。旧"顶层短路"bug 已由测试锁定。
      try {
        const vip = await fetchVipDetail();
        const outcome = applyVipSnapshot(vip, { allowDowngrade: true });
        if (outcome === 'kept') {
          console.warn('VIP detail returned no authoritative state; keeping prior VIP state');
        }
      } catch (e) {
        console.warn('VIP detail refresh failed; keeping login state', e);
      }

      // 不再静默自动领取：每日免费 VIP 应由用户在账户中心手动领取，
      // 自动续领会让 VIP 状态永不过期（用户反馈“vip一直无法过期”），
      // 且对标准 token 的播放无任何帮助。
    } else {
      resetLoginState();
    }
  } catch (e) {
    console.error('Check login status error', e);
    resetLoginState();
  } finally {
    userStore.loading = false;
  }
}

// ── Stage 5b: 领取受理 ≠ 权益有效 ─────────────────────────────────────────
//
// 通道成功（status===1）只代表"领取请求被上游受理"，不等于权益已生效。
// 流程：受理 → vipClaimPending=true（待确认）→ 立即用权威接口确认一次：
//   - 权威确认生效        → isVip=true + 到期时间，pending=false，"已激活"；
//   - 权威明确返回无权益  → isVip=false，pending=false，如实说明"未生效"
//                           （绝不说"已过期"——那是 5a expired 的表述）；
//   - 查询失败/非权威     → 保持待确认，启动有上限重查（默认 3 次 × 10s，
//                           可取消）；重查耗尽 → pending=false，转"未确认"。
// 既有 VIP（领取前 isVip 已为 true）由 applyVipSnapshot 的 allowDowngrade:
// false 守卫保护，权威抖动不会降级既有权益（login refresh 的语义不变）。

/** 确认重查默认参数（可在 claimVip opts 里注入以便测试）。 */
export const VIP_CONFIRM_RECHECK_ATTEMPTS = 3;
export const VIP_CONFIRM_RECHECK_INTERVAL_MS = 10_000;

type ConfirmOutcome =
  | { kind: 'active' }
  | { kind: 'inactive' }
  | { kind: 'unavailable' };

async function tryConfirmOnce(): Promise<ConfirmOutcome> {
  try {
    const vip = await fetchVipDetail();
    const outcome = applyVipSnapshot(vip, { allowDowngrade: false });
    if (outcome === 'applied') {
      return userStore.isVip ? { kind: 'active' } : { kind: 'inactive' };
    }
    if (outcome === 'pending') {
      // 既有权益被 allowDowngrade 守卫保护（未降级），权威已给出结论。
      return { kind: 'active' };
    }
    return { kind: 'unavailable' };
  } catch {
    return { kind: 'unavailable' };
  }
}

async function confirmVipAfterClaim(opts: {
  recheckAttempts: number;
  recheckIntervalMs: number;
}): Promise<string> {
  userStore.claimStage = 'confirm';
  let outcome = await tryConfirmOnce();
  if (userStore.claimCancelRequested) return '已取消领取';
  if (outcome.kind === 'active') {
    userStore.vipClaimPending = false;
    return userStore.vipEndDate
      ? `✓ 已激活每日 VIP，到期：${userStore.vipEndDate}`
      : '✓ 已激活每日 VIP';
  }
  if (outcome.kind === 'inactive') {
    userStore.vipClaimPending = false;
    return '领取上报成功，但权益未生效（权威接口未见 VIP），请稍后再次查询';
  }

  // 首次确认不可用（失败/非权威）→ 有上限重查，可取消（P3：与广告倒计时
  // 相同的 1s 步进粒度，取消最迟 1s 生效，而不是整个 interval）。
  for (let attempt = 1; attempt <= opts.recheckAttempts; attempt++) {
    if (userStore.claimCancelRequested) return '已取消领取';
    userStore.claimMessage = `权益确认中（重试 ${attempt}/${opts.recheckAttempts}）…`;
    let remainMs = opts.recheckIntervalMs;
    while (remainMs > 0) {
      if (userStore.claimCancelRequested) return '已取消领取';
      const step = Math.min(1000, remainMs);
      await sleep(step);
      remainMs -= step;
    }
    if (userStore.claimCancelRequested) return '已取消领取';
    outcome = await tryConfirmOnce();
    if (userStore.claimCancelRequested) return '已取消领取';
    if (outcome.kind === 'active') {
      userStore.vipClaimPending = false;
      return userStore.vipEndDate
        ? `✓ 已激活每日 VIP，到期：${userStore.vipEndDate}`
        : '✓ 已激活每日 VIP';
    }
    if (outcome.kind === 'inactive') {
      userStore.vipClaimPending = false;
      return '领取上报成功，但权益未生效（权威接口未见 VIP），请稍后再次查询';
    }
  }
  // 重查耗尽：转"未确认"，不是"有效"。
  userStore.vipClaimPending = false;
  return '权益状态未确认（多次查询失败），可稍后在账户中心重试';
}

// 主领取链路（2026-09-02 实测定案）：
//   1) 广告上报循环——唯一实测真实发放 VIP 的通道（单次领到 3 小时 svip）。
//      上游约定：每看一条 30 秒广告上报一次，间隔 30 秒、最多 8 次，
//      哪一轮被拒（撞墙/风控）立即断路。
//   2) 广告被拒时，其余三条通道（直接领取/听歌上报/看广告升级）各试一次——
//      哪条能通取决于酷狗上游活动与风控，墙会随时间移动（130012→51002）。
// 任一通道成功即同步权威权益；全失败时如实展示最后一次上游返回。
// 关键：各通道成功响应不携带到期时间，成功与否只看 status===1；
// 到期时间的权威来源是 /user/vip/detail (get_union_vip)。

export const AD_LOOP_MAX = 8;
export const AD_LOOP_INTERVAL_MS = 30_000;

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function runAdClaimLoop(
  adLoopMax: number,
  adIntervalMs: number,
): Promise<{ ok: boolean; cancelled: boolean; last?: any }> {
  let last: any;
  let ok = false;
  for (let i = 1; i <= adLoopMax; i++) {
    // Stage 5c: 阶段边界检查取消 —— 不再发起下一轮广告上报。
    if (userStore.claimCancelRequested) {
      return { ok, cancelled: true, last };
    }
    userStore.claimMessage = `广告上报（${i}/${adLoopMax}）…`;
    try {
      last = await claimYouthVipAd();
    } catch (e: any) {
      last = { error: describeBackendError(e, '网络异常或接口调用出错') };
      break;
    }
    if (last?.status === 1) {
      ok = true;
      if (i >= adLoopMax) break;
      // 成功不立即收尾：循环上报可能累计时长；倒计时后下一轮，被拒即停。
      // 倒计时每 1s 检查一次取消（Stage 5c）。
      let remainMs = adIntervalMs;
      while (remainMs > 0) {
        if (userStore.claimCancelRequested) {
          return { ok, cancelled: true, last };
        }
        const step = Math.min(1000, remainMs);
        userStore.claimMessage =
          `广告上报（${i}/${adLoopMax}）成功，${Math.ceil(remainMs / 1000)} 秒后下一轮…`;
        await sleep(step);
        remainMs -= step;
      }
      continue;
    }
    // 被上游拒绝：立即断路，不再消耗上报次数。
    break;
  }
  return { ok, cancelled: false, last };
}

export async function claimVip(
  opts?: {
    adLoopMax?: number;
    adIntervalMs?: number;
    /** 确认重查次数（默认 3；测试注入 0 保持确定性）。 */
    recheckAttempts?: number;
    /** 确认重查间隔（默认 10s；测试注入小值）。 */
    recheckIntervalMs?: number;
  },
): Promise<void> {
  if (!userStore.isLoggedIn) {
    userStore.claimMessage = '请先登录！';
    return;
  }
  const adLoopMax = opts?.adLoopMax ?? AD_LOOP_MAX;
  const adIntervalMs = opts?.adIntervalMs ?? AD_LOOP_INTERVAL_MS;
  const recheckAttempts = opts?.recheckAttempts ?? VIP_CONFIRM_RECHECK_ATTEMPTS;
  const recheckIntervalMs = opts?.recheckIntervalMs ?? VIP_CONFIRM_RECHECK_INTERVAL_MS;
  userStore.claimCancelRequested = false;
  userStore.loading = true;
  userStore.claimMessage = '正在领取每日免费 VIP…';
  try {
    userStore.claimStage = 'device';
    const deviceResult = await ensureVipDeviceReady();
    if (userStore.claimCancelRequested) {
      userStore.claimMessage = '已取消领取';
      return;
    }
    if (!deviceResult.ok) {
      userStore.claimMessage = formatDeviceGateFailure(deviceResult);
      return;
    }
    void notifyAccountReady(userStore.userId);

    // 1) 广告上报循环（主通道）
    userStore.claimStage = 'ad';
    const ad = await runAdClaimLoop(adLoopMax, adIntervalMs);
    if (ad.cancelled) {
      userStore.claimMessage = '已取消领取';
      return;
    }
    if (ad.ok) {
      // Stage 5b: 受理 ≠ 生效 —— 进入"待确认"，由权威接口给出结论。
      userStore.vipClaimPending = true;
      userStore.claimStage = 'confirm';
      userStore.claimMessage = '领取成功，正在确认权益…';
      userStore.claimMessage = await confirmVipAfterClaim({
        recheckAttempts,
        recheckIntervalMs,
      });
      return;
    }

    // 2) 其余通道逐个兜底
    userStore.claimStage = 'fallback';
    const fallbacks: { label: string; call: () => Promise<any> }[] = [
      { label: '直接领取', call: claimYouthDayVip },
      { label: '听歌上报', call: claimDailyVipSong },
      { label: '看广告升级', call: claimYouthDayVipUpgrade },
    ];
    let lastFailure: any = ad.last;
    for (const fb of fallbacks) {
      if (userStore.claimCancelRequested) {
        userStore.claimMessage = '已取消领取';
        return;
      }
      userStore.claimMessage = `广告上报受限，尝试「${fb.label}」…`;
      let result: any;
      try {
        result = await fb.call();
      } catch (e: any) {
        lastFailure = { error: describeBackendError(e, '网络异常或接口调用出错') };
        continue;
      }
      if (userStore.claimCancelRequested) {
        userStore.claimMessage = '已取消领取';
        return;
      }
      if (result?.status === 1) {
        // Stage 5b: 受理 ≠ 生效（与主通道一致）。
        userStore.vipClaimPending = true;
        userStore.claimStage = 'confirm';
        userStore.claimMessage = '领取成功，正在确认权益…';
        userStore.claimMessage = await confirmVipAfterClaim({
          recheckAttempts,
          recheckIntervalMs,
        });
        return;
      }
      lastFailure = result;
    }
    userStore.claimMessage = formatVipClaimFailure(lastFailure);
  } catch (e: any) {
    console.error('Claim VIP error', e);
    userStore.claimMessage = `领取失败：${describeBackendError(e, '网络异常或接口调用出错')}`;
  } finally {
    userStore.loading = false;
    userStore.claimStage = '';
    userStore.claimCancelRequested = false;
  }
}

// ── 实验：单独领取通道（诊断用）─────────────────────────────────────────
// 四条通道互相独立，哪条能通取决于酷狗上游活动与风控；任一通道成功即同步
// 权益。失败时一律如实展示上游 status/error_code/error_msg，不做本地猜测。

export type VipClaimRoute = 'day' | 'listen' | 'ad' | 'day-upgrade';

export const VIP_CLAIM_ROUTES: { id: VipClaimRoute; label: string }[] = [
  { id: 'day', label: '直接领取（1天）' },
  { id: 'listen', label: '听歌上报' },
  { id: 'ad', label: '广告上报' },
  { id: 'day-upgrade', label: '看广告升级' },
];

export async function claimVipViaRoute(route: VipClaimRoute): Promise<void> {
  if (!userStore.isLoggedIn) {
    userStore.claimMessage = '请先登录！';
    return;
  }
  const label = VIP_CLAIM_ROUTES.find((r) => r.id === route)?.label ?? route;
  // 与 claimVip 的状态机对齐（批次 C 审查 P1）：开头复位取消标志，
  // finally 复位全部流程状态 —— 上一次运行的取消/阶段不得毒化本次。
  userStore.claimCancelRequested = false;
  userStore.loading = true;
  userStore.claimMessage = `尝试「${label}」…`;
  try {
    const deviceResult = await ensureVipDeviceReady();
    if (!deviceResult.ok) {
      userStore.claimMessage = formatDeviceGateFailure(deviceResult);
      return;
    }
    const calls: Record<VipClaimRoute, () => Promise<any>> = {
      day: claimYouthDayVip,
      listen: claimDailyVipSong,
      ad: claimYouthVipAd,
      'day-upgrade': claimYouthDayVipUpgrade,
    };
    const result = await calls[route]();
    if (userStore.claimCancelRequested) {
      userStore.claimMessage = '已取消领取';
      return;
    }
    if (result?.status === 1) {
      // Stage 5b: 受理 ≠ 生效（与主通道一致）。
      userStore.vipClaimPending = true;
      userStore.claimStage = 'confirm';
      userStore.claimMessage = '领取成功，正在确认权益…';
      userStore.claimMessage = await confirmVipAfterClaim({
        recheckAttempts: VIP_CONFIRM_RECHECK_ATTEMPTS,
        recheckIntervalMs: VIP_CONFIRM_RECHECK_INTERVAL_MS,
      });
      return;
    }
    userStore.claimMessage = `「${label}」${formatVipClaimFailure(result)}`;
  } catch (e: any) {
    console.error(`Claim VIP via ${route} error`, e);
    userStore.claimMessage = `「${label}」领取失败：${describeBackendError(e, '网络异常或接口调用出错')}`;
  } finally {
    userStore.loading = false;
    userStore.claimStage = '';
    userStore.claimCancelRequested = false;
  }
}

export function logoutLocal() {
  // Local clear: resetLoginState already emits accountCleared.
  resetLoginState();
  userStore.claimMessage = '';
  notifyLocalLogout();
}
