import { reactive } from 'vue';
import { describeBackendError } from '../../platform/tauri/nativeClient';
import {
  registerDevice,
  fetchUserDetail,
  fetchVipDetail,
  claimDailyVipSong,
  claimYouthVipAd,
  claimYouthDayVip,
  claimYouthDayVipConceptCandidate,
  claimYouthDayVipUpgrade,
} from './accountGateway';
import { resolveVip, parseVipEndTime, liveVipView, recomputeObservedEntitlements, deriveMusicPermission, type VipStatus, type ObservedEntitlement, type MusicPermission } from './vipResolver';
import {
  notifyAccountReady,
  notifyAccountCleared,
  notifyLocalLogout,
} from './accountEffects';

/** Monotonic claim generation: late confirm responses must not write after
 *  logout, a second claim, or an account switch. */
let claimGeneration = 0;

/** Login-check operation epoch. logoutLocal / a newer checkLoginStatus discard
 *  late identity/device/VIP/loading/clear writes from an older check. Cold
 *  start is allowed (isLoggedIn=false); ownership is generation-only. */
let loginOpGeneration = 0;

function loginStillOwned(generation: number): boolean {
  return generation === loginOpGeneration;
}

/** Login session epoch: logout, re-login, and account switch invalidate
 *  in-flight expiry rechecks even when userId matches again. */
let sessionGeneration = 0;
/** Entitlement snapshot epoch: a newer applied VIP snapshot discards an
 *  older expiry recheck for the same session. */
let entitlementGeneration = 0;

const VIP_CLOCK_MS = 30_000;
let vipClockTimer: ReturnType<typeof setInterval> | null = null;
let vipExpiryTimer: ReturnType<typeof setTimeout> | null = null;
let vipClockListening = false;
const expiryRecheckedKeys = new Set<string>();
let expiryRecheckFlightSeq = 0;
let expiryRecheckFlightId = 0;

interface ExpiryRecheckCapture {
  sessionGeneration: number;
  userId: string;
  entitlementGeneration: number;
  deadline: string;
}

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
  /** Session-scoped claim ledger: acceptance and failure are separate facts.
   *  A later rejection must never erase an earlier acceptance. Cleared on logout. */
  claimLedger: {
    lastAcceptedAt: string | null;
    lastAcceptedRoute: string | null;
    lastAcceptedSummary: string | null;
    lastFailureAt: string | null;
    lastFailureCode: string | number | null;
    lastFailureMessage: string | null;
  };
  /** busi_vip entries from the last authoritative VIP snapshot (music + non-music). */
  observedEntitlements: ObservedEntitlement[];
  /** Music rights confirmation; observed_non_music ≠ music VIP, ≠ "nothing granted". */
  musicPermission: MusicPermission;
}

const emptyClaimLedger = () => ({
  lastAcceptedAt: null as string | null,
  lastAcceptedRoute: null as string | null,
  lastAcceptedSummary: null as string | null,
  lastFailureAt: null as string | null,
  lastFailureCode: null as string | number | null,
  lastFailureMessage: null as string | null,
});

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
  claimLedger: emptyClaimLedger(),
  observedEntitlements: [],
  musicPermission: 'unknown',
});

function resetVipState() {
  userStore.vipLevel = 0;
  userStore.vipType = 0;
  userStore.isVip = false;
  userStore.vipStatus = 'unknown';
  userStore.vipEndDate = '';
  userStore.vipClaimPending = false;
  userStore.observedEntitlements = [];
  userStore.musicPermission = 'unknown';
}

/** Stage 5c: 用户请求取消当前领取。在阶段边界（下一轮广告上报前、倒计时
 *  每 1s、下一 条兜底通道前、下一次确认重查前）生效：停止后续工作、
 *  loading 复位、消息如实说明"已取消"。已进入的 FFI 调用本身不可中断。 */
export function cancelClaim() {
  userStore.claimCancelRequested = true;
}

function resetClaimLedger() {
  userStore.claimLedger = emptyClaimLedger();
}

function recordClaimAcceptance(route: string, summary: string) {
  userStore.claimLedger.lastAcceptedAt = new Date().toISOString();
  userStore.claimLedger.lastAcceptedRoute = route;
  userStore.claimLedger.lastAcceptedSummary = summary;
}

function recordClaimFailure(code: string | number | null | undefined, message: string) {
  userStore.claimLedger.lastFailureAt = new Date().toISOString();
  userStore.claimLedger.lastFailureCode =
    code === undefined || code === null || code === '' ? null : code;
  userStore.claimLedger.lastFailureMessage = message;
  // Never clear lastAccepted* here — a later rejection is a separate fact.
}

function composeFailureWithPriorAcceptance(base: string): string {
  const acc = userStore.claimLedger.lastAcceptedAt;
  if (!acc) return base;
  const route = userStore.claimLedger.lastAcceptedRoute
    ? ` · ${userStore.claimLedger.lastAcceptedRoute}`
    : '';
  return `${base}｜此前受理记录仍保留（${acc}${route}），本次拒绝不抹掉该事实`;
}

function formatInactiveAfterConfirm(): string {
  const activeNonMusic = userStore.observedEntitlements.filter((e) => e.active && !e.unlocksMusic);
  if (activeNonMusic.length > 0) {
    const types = activeNonMusic
      .map((e) => `${e.productType}${e.vipEndDate ? `至 ${e.vipEndDate}` : ''}`)
      .join('、');
    return `领取已受理；已观察到权益 ${types}，音乐适用性待确认。未将当前显示判为音乐会员，也不表示上游什么权益都没给`;
  }
  return '领取上报成功，但权益未生效（权威接口未见音乐类 VIP），请稍后再次查询';
}

function resetLoginState() {
  sessionGeneration += 1;
  stopVipClock();
  expiryRecheckedKeys.clear();
  userStore.isLoggedIn = false;
  userStore.deviceReady = false;
  userStore.userId = '';
  userStore.username = '未登录';
  userStore.avatar = '';
  resetVipState();
  resetClaimLedger();
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

/**
 * Local VIP display is only "still valid" when isVip is true and the stored
 * end date (when present) has not passed. Expired/stale cache must not block
 * authoritative no-entitlement or be treated as claim activation evidence.
 */
function isLocalVipCurrentlyValid(nowMs: number = Date.now()): boolean {
  return liveVipView(userStore, nowMs).isVip;
}

function applyVipSnapshot(detail: any, opts: { allowDowngrade: boolean }): 'applied' | 'kept' | 'pending' {
  if (!isAuthoritativeVipDetail(detail)) {
    return 'kept';
  }
  const resolved = resolveVip(detail.data, Date.now());
  // Always record authoritative busi_vip observation — even when prior music
  // VIP display is kept (pending). Observation ≠ music unlock.
  userStore.observedEntitlements = resolved.observedEntitlements;
  // Protect still-valid prior display from a transient authoritative "no VIP"
  // during claim confirm — but never protect expired/stale cache, and never
  // treat that protection as proof that *this claim* activated VIP.
  if (!opts.allowDowngrade && userStore.isVip && !resolved.isVip) {
    if (isLocalVipCurrentlyValid()) {
      // Local music VIP display remains valid; keep musicPermission active.
      userStore.musicPermission = 'active';
      return 'pending';
    }
    // Expired local cache: fall through and apply authoritative no-entitlement.
  }
  userStore.vipLevel = resolved.vipLevel;
  userStore.vipType = resolved.vipType;
  userStore.isVip = resolved.isVip;
  // Stage 5a 三态透传：applied 时权威结论已落地，expired/unknown 由 UI
  // 按各自表述约束展示（expired 才能说"已过期"，unknown 只能中性表述）。
  userStore.vipStatus = resolved.vipStatus;
  userStore.vipEndDate = resolved.vipEndDate;
  userStore.musicPermission = resolved.musicPermission;
  entitlementGeneration += 1;
  scheduleVipExpiryTick();
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
  // 131001：实机仅证明发生在首次受理后的第二次请求；含义未有权威定义。
  // 不得硬编码为“已领取成功”，也不得把测试夹具 ad token invalid 当服务端说明。
  if (Number(code) === 131001) {
    if (detail) {
      const suffix = detail.includes('131001') ? '' : '（错误码 131001）';
      return `领取失败：${detail}${suffix}`;
    }
    return '领取失败：上游拒绝（错误码 131001）；原因未明确（重复领取限制仅为时序假设，非错误码定义）';
  }
  // 51002：上游拒绝码是事实。有真实上游说明时原样展示；空说明才用兜底。
  if (Number(code) === 51002) {
    const upstreamDetail = [result?.upstream_message, result?.error_msg, result?.msg, result?.message]
      .find((value) => typeof value === 'string' && value.trim())?.trim() ?? '';
    if (upstreamDetail) {
      const suffix = upstreamDetail.includes('51002') ? '' : '（错误码 51002）';
      return `领取失败：${upstreamDetail}${suffix}`;
    }
    return '领取失败：上游拒绝，原因未明确（错误码 51002）';
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

function onVipClockVisibility() {
  if (typeof document !== 'undefined' && !document.hidden) {
    void refreshLiveVip();
  }
}

function clearVipExpiryTimer() {
  if (vipExpiryTimer != null) {
    clearTimeout(vipExpiryTimer);
    vipExpiryTimer = null;
  }
}

function scheduleVipExpiryTick(nowMs: number = Date.now()) {
  clearVipExpiryTimer();
  if (!userStore.isLoggedIn || !userStore.isVip) return;
  const endMs = parseVipEndTime(userStore.vipEndDate);
  if (endMs <= 0) return;
  const delay = endMs - nowMs;
  if (delay <= 0) {
    void refreshLiveVip(nowMs);
    return;
  }
  // setTimeout is 32-bit; far-future deadlines stay on the 30s beat.
  if (delay > 2_147_483_647) return;
  vipExpiryTimer = setTimeout(() => {
    vipExpiryTimer = null;
    void refreshLiveVip();
  }, delay);
}

export function startVipClock(): void {
  if (typeof document !== 'undefined' && !vipClockListening) {
    document.addEventListener('visibilitychange', onVipClockVisibility);
    vipClockListening = true;
  }
  if (vipClockTimer == null) {
    vipClockTimer = setInterval(() => {
      void refreshLiveVip();
    }, VIP_CLOCK_MS);
  }
  scheduleVipExpiryTick();
}

export function stopVipClock(): void {
  if (vipClockTimer != null) {
    clearInterval(vipClockTimer);
    vipClockTimer = null;
  }
  clearVipExpiryTimer();
}

export function __resetVipClockForTests(): void {
  stopVipClock();
  if (vipClockListening && typeof document !== 'undefined') {
    document.removeEventListener('visibilitychange', onVipClockVisibility);
    vipClockListening = false;
  }
  expiryRecheckedKeys.clear();
  expiryRecheckFlightSeq = 0;
  expiryRecheckFlightId = 0;
  sessionGeneration = 0;
  entitlementGeneration = 0;
  loginOpGeneration = 0;
}

function expiryRecheckKey(capture: ExpiryRecheckCapture): string {
  return `${capture.sessionGeneration}:${capture.userId}:${capture.deadline}`;
}

function expiryRecheckStillCurrent(capture: ExpiryRecheckCapture): boolean {
  return userStore.isLoggedIn
    && userStore.userId === capture.userId
    && sessionGeneration === capture.sessionGeneration
    && entitlementGeneration === capture.entitlementGeneration
    && (userStore.vipEndDate || '__expired__') === capture.deadline;
}

async function recheckVipAfterLocalExpiry(): Promise<void> {
  if (!userStore.isLoggedIn) return;
  const capture: ExpiryRecheckCapture = {
    sessionGeneration,
    userId: userStore.userId,
    entitlementGeneration,
    deadline: userStore.vipEndDate || '__expired__',
  };
  const key = expiryRecheckKey(capture);
  if (expiryRecheckedKeys.has(key)) return;
  expiryRecheckedKeys.add(key);
  const flightId = ++expiryRecheckFlightSeq;
  expiryRecheckFlightId = flightId;
  try {
    const vip = await fetchVipDetail();
    if (!expiryRecheckStillCurrent(capture)) return;
    applyVipSnapshot(vip, { allowDowngrade: true });
  } catch {
    // Known-expired local rights stay expired; do not loop.
  } finally {
    if (expiryRecheckFlightId === flightId) {
      expiryRecheckFlightId = 0;
    }
  }
}

/**
 * Recompute VIP display from the stored end date as time passes.
 * Crossing music-VIP expiry writes expired locally, then runs at most one
 * entitlement recheck for that session+deadline. A failed recheck must not
 * restore the previous active snapshot. Late responses after logout,
 * account switch, re-login, or a newer snapshot are discarded.
 *
 * Also recomputes observed busi_vip activity from the wall clock (tvip-only
 * stays isVip=false). Historical rows keep productType/isVip/vipEndDate;
 * only `active` and the derived musicPermission change. Does not bump
 * entitlementGeneration — clock expiry is display derivation, not a new
 * authoritative snapshot. ClaimLedger acceptance history is left untouched.
 */
export async function refreshLiveVip(nowMs: number = Date.now()): Promise<void> {
  const live = liveVipView(userStore, nowMs);
  const prevObservations = userStore.observedEntitlements;
  const recomputed = recomputeObservedEntitlements(prevObservations, nowMs);
  const observationActiveChanged = recomputed.some(
    (e, i) => Boolean(e.active) !== Boolean(prevObservations[i]?.active),
  );
  if (observationActiveChanged) {
    userStore.observedEntitlements = recomputed;
    // Music-class rows that used to be is_vip=1 but are no longer valid now.
    const hadMusicExpiryEvidence =
      userStore.vipStatus === 'expired' ||
      prevObservations.some((e) => e.unlocksMusic && e.isVip && !e.active) ||
      (userStore.isVip && !live.isVip);
    userStore.musicPermission = deriveMusicPermission(recomputed, {
      musicVipActive: userStore.isVip && live.isVip,
      hadMusicExpiryEvidence,
    });
    scheduleVipExpiryTick(nowMs);
  }

  if (userStore.isVip && !live.isVip) {
    userStore.isVip = false;
    userStore.vipStatus = live.vipStatus;
    userStore.musicPermission = deriveMusicPermission(
      userStore.observedEntitlements,
      {
        musicVipActive: false,
        hadMusicExpiryEvidence: live.vipStatus === 'expired',
      },
    );
    entitlementGeneration += 1;
    scheduleVipExpiryTick(nowMs);
    if (live.vipStatus === 'expired') {
      await recheckVipAfterLocalExpiry();
    }
  }
}

export async function ensureVipDeviceReady(opts?: {
  /** When provided, deviceReady is written only while this returns true.
   *  Callers that own an async operation pass generation ownership so a late
   *  register response cannot paint deviceReady after logout/account switch. */
  isCurrent?: () => boolean;
}): Promise<VipDeviceResult> {
  const canWrite = () => !opts?.isCurrent || opts.isCurrent();
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
      if (canWrite()) userStore.deviceReady = true;
      return { ok: true };
    }
    if (canWrite()) userStore.deviceReady = false;
    return { ok: false, error: 'device_registration_failed' };
  } catch (error: any) {
    if (canWrite()) userStore.deviceReady = false;
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
  const generation = ++loginOpGeneration;
  userStore.loading = true;
  try {
    const detail = await fetchUserDetail();
    if (!loginStillOwned(generation)) return;
    if (detail && detail.status === 1 && detail.data) {
      const incomingUserId = String(detail.data.userid || '');
      if (!userStore.isLoggedIn || incomingUserId !== userStore.userId) {
        sessionGeneration += 1;
      }
      userStore.userId = incomingUserId;
      userStore.username = detail.data.nickname || detail.data.username || '听歌用户';
      userStore.avatar = detail.data.pic || detail.data.avatar || '';
      userStore.deviceReady = false;

      // Register before any VIP claim so the request cannot race ahead with
      // the placeholder dfid="-". The backend is idempotent once registered.
      // /song/url returns full VIP audio (instead of 60s previews) and
      // /user/playlist stops returning error_code 20017. The backend is
      // idempotent — re-calls return the persisted device once registered=true.
      const deviceResult = await ensureVipDeviceReady({
        isCurrent: () => loginStillOwned(generation),
      });
      if (!loginStillOwned(generation)) return;
      userStore.isLoggedIn = true;
      startVipClock();
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
        // Capture session + entitlement BEFORE the request. A concurrent
        // expiry recheck / claim confirm that lands first bumps
        // entitlementGeneration; the late login response must not overwrite it.
        // Normal authoritative downgrade still applies when no concurrent update
        // occurs (same capture → applyVipSnapshot(allowDowngrade:true)).
        const vipCapture = {
          sessionGeneration,
          entitlementGeneration,
          userId: userStore.userId,
        };
        const vip = await fetchVipDetail();
        if (!loginStillOwned(generation)) return;
        if (
          sessionGeneration !== vipCapture.sessionGeneration ||
          entitlementGeneration !== vipCapture.entitlementGeneration ||
          userStore.userId !== vipCapture.userId
        ) {
          // Newer same-session entitlement already landed — keep it.
          return;
        }
        const outcome = applyVipSnapshot(vip, { allowDowngrade: true });
        if (outcome === 'kept') {
          console.warn('VIP detail returned no authoritative state; keeping prior VIP state');
        }
      } catch (e) {
        if (!loginStillOwned(generation)) return;
        console.warn('VIP detail refresh failed; keeping login state', e);
      }

      // 不再静默自动领取：每日免费 VIP 应由用户在账户中心手动领取，
      // 自动续领会让 VIP 状态永不过期（用户反馈“vip一直无法过期”），
      // 且对标准 token 的播放无任何帮助。
    } else {
      resetLoginState();
    }
  } catch (e) {
    if (!loginStillOwned(generation)) return;
    console.error('Check login status error', e);
    resetLoginState();
  } finally {
    // Only the owning generation may clear loading; a newer check/logout owns it.
    if (loginStillOwned(generation)) {
      userStore.loading = false;
    }
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
// 既有 *尚未过期* 的 VIP 展示可由 applyVipSnapshot(allowDowngrade:false) 暂留，
// 但领取确认文案不得把“暂留旧展示”解释成“本次已激活”。过期缓存一律不保活。
// login refresh 仍用 allowDowngrade:true，权威结论直接落地。

/** 确认重查默认参数（可在 claimVip opts 里注入以便测试）。 */
export const VIP_CONFIRM_RECHECK_ATTEMPTS = 3;
export const VIP_CONFIRM_RECHECK_INTERVAL_MS = 10_000;

type ConfirmOutcome =
  | { kind: 'active' }
  | { kind: 'inactive' }
  | { kind: 'unavailable' }
  | { kind: 'stale' };

function claimSessionStillOwned(generation: number, ownerUserId: string): boolean {
  return userStore.isLoggedIn
    && userStore.userId === ownerUserId
    && generation === claimGeneration;
}

async function tryConfirmOnce(
  generation: number,
  ownerUserId: string,
): Promise<ConfirmOutcome> {
  try {
    const vip = await fetchVipDetail();
    // Late response after logout / account switch / newer claim: discard.
    // Cancel alone does NOT make the response stale — W2 requires a landed
    // authoritative conclusion to beat a concurrent cancel request.
    if (!claimSessionStillOwned(generation, ownerUserId)) {
      return { kind: 'stale' };
    }
    const outcome = applyVipSnapshot(vip, { allowDowngrade: false });
    if (!claimSessionStillOwned(generation, ownerUserId)) {
      return { kind: 'stale' };
    }
    if (outcome === 'applied') {
      return userStore.isVip ? { kind: 'active' } : { kind: 'inactive' };
    }
    if (outcome === 'pending') {
      // Authority reported no entitlement; prior non-expired display may remain,
      // but this claim did not activate VIP.
      return { kind: 'inactive' };
    }
    return { kind: 'unavailable' };
  } catch {
    if (!claimSessionStillOwned(generation, ownerUserId)) {
      return { kind: 'stale' };
    }
    return { kind: 'unavailable' };
  }
}

function formatActiveClaimMessage(): string {
  return userStore.vipEndDate
    ? `✓ 已激活每日 VIP，到期：${userStore.vipEndDate}`
    : '✓ 已激活每日 VIP';
}

async function confirmVipAfterClaim(opts: {
  recheckAttempts: number;
  recheckIntervalMs: number;
  generation: number;
  ownerUserId: string;
}): Promise<string | null> {
  userStore.claimStage = 'confirm';
  let outcome = await tryConfirmOnce(opts.generation, opts.ownerUserId);
  // W2：已落地的权威结论优先于取消 —— 结论写入 store 后若被"已取消领取"
  // 覆盖，会出现"角标已激活、消息已取消"的自相矛盾。只有结论未落地
  // （unavailable）时才让位给取消。stale（登出/换号）则不得恢复旧文案。
  if (outcome.kind === 'stale') {
    // 审阅 P1（2026-09-15）：失效任务（登出/换号/新领取）静默退出 —— 返回
    // null，不写 pending/message/stage；调用方只在非 null 时落文案。
    return null;
  }
  if (outcome.kind === 'active') {
    userStore.vipClaimPending = false;
    return formatActiveClaimMessage();
  }
  if (outcome.kind === 'inactive') {
    userStore.vipClaimPending = false;
    return formatInactiveAfterConfirm();
  }
  if (!claimSessionStillOwned(opts.generation, opts.ownerUserId)) return null; // 失效：静默退出（审阅 P1）
  if (userStore.claimCancelRequested) return '已取消领取';

  // 首次确认不可用（失败/非权威）→ 有上限重查，可取消（P3：与广告倒计时
  // 相同的 1s 步进粒度，取消最迟 1s 生效，而不是整个 interval）。
  const totalUpperBoundSec = Math.ceil(
    (opts.recheckIntervalMs * opts.recheckAttempts) / 1000,
  );
  for (let attempt = 1; attempt <= opts.recheckAttempts; attempt++) {
    if (!claimSessionStillOwned(opts.generation, opts.ownerUserId)) return null; // 失效：静默退出（审阅 P1）
    if (userStore.claimCancelRequested) return '已取消领取';
    // W5（5c 完成标准）：整体最长等待上界文案。
    userStore.claimMessage = `权益确认中，最长约需 ${totalUpperBoundSec} 秒（重试 ${attempt}/${opts.recheckAttempts}），可随时取消…`;
    let remainMs = opts.recheckIntervalMs;
    while (remainMs > 0) {
      if (!claimSessionStillOwned(opts.generation, opts.ownerUserId)) return null; // 失效：静默退出（审阅 P1）
      if (userStore.claimCancelRequested) return '已取消领取';
      const step = Math.min(1000, remainMs);
      await sleep(step);
      remainMs -= step;
    }
    if (!claimSessionStillOwned(opts.generation, opts.ownerUserId)) return null; // 失效：静默退出（审阅 P1）
    if (userStore.claimCancelRequested) return '已取消领取';
    outcome = await tryConfirmOnce(opts.generation, opts.ownerUserId);
    if (outcome.kind === 'stale') {
      return null; // 失效：静默退出（审阅 P1）。
    }
    if (outcome.kind === 'active') {
      userStore.vipClaimPending = false;
      return formatActiveClaimMessage();
    }
    if (outcome.kind === 'inactive') {
      userStore.vipClaimPending = false;
      return formatInactiveAfterConfirm();
    }
    if (!claimSessionStillOwned(opts.generation, opts.ownerUserId)) return null; // 失效：静默退出（审阅 P1）
    if (userStore.claimCancelRequested) return '已取消领取';
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
  generation: number,
  ownerUserId: string,
): Promise<{ ok: boolean; cancelled: boolean; last?: any }> {
  let last: any;
  let ok = false;
  // 审阅 P1（2026-09-15）：共享取消标志会被新领取复位，只看
  // claimCancelRequested 拦不住登出/换号后的旧任务 —— 每一处边界都必须
  // 同时检查 generation/ownerUserId 所有权。
  const stillOwned = (): boolean =>
    !userStore.claimCancelRequested && claimSessionStillOwned(generation, ownerUserId);
  for (let i = 1; i <= adLoopMax; i++) {
    // Stage 5c: 阶段边界检查取消 —— 不再发起下一轮广告上报。
    if (!stillOwned()) {
      return { ok, cancelled: true, last };
    }
    userStore.claimMessage = `广告上报（${i}/${adLoopMax}）…`;
    try {
      last = await claimYouthVipAd();
    } catch (e: any) {
      last = { error: describeBackendError(e, '网络异常或接口调用出错') };
      break;
    }
    // await 返回后立即复查所有权：登出/换号/新领取后，旧任务必须就此停止
    // —— 不得发起下一轮上报，也不得再写任何 claim 状态（审阅 P1）。
    if (!stillOwned()) {
      return { ok, cancelled: true, last };
    }
    if (last?.status === 1) {
      ok = true;
      if (i >= adLoopMax) break;
      // 成功不立即收尾：循环上报可能累计时长；倒计时后下一轮，被拒即停。
      // 倒计时每 1s 检查一次取消与所有权（Stage 5c + 审阅 P1）。
      let remainMs = adIntervalMs;
      while (remainMs > 0) {
        if (!stillOwned()) {
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
  const generation = ++claimGeneration;
  const ownerUserId = userStore.userId;
  userStore.claimCancelRequested = false;
  userStore.loading = true;
  userStore.claimMessage = '正在领取每日免费 VIP…';
  try {
    userStore.claimStage = 'device';
    const deviceResult = await ensureVipDeviceReady({
      isCurrent: () => claimSessionStillOwned(generation, ownerUserId),
    });
    // 失效（登出/换号/新领取）：静默退出 —— 不得写任何 claim 状态（审阅 P1）。
    if (!claimSessionStillOwned(generation, ownerUserId)) return;
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
    const ad = await runAdClaimLoop(adLoopMax, adIntervalMs, generation, ownerUserId);
    // 失效（登出/换号/新领取）：静默退出 —— 不得写任何 claim 状态（审阅 P1）。
    if (!claimSessionStillOwned(generation, ownerUserId)) return;
    if (ad.cancelled) {
      // 同会话取消（用户请求）才落取消文案；失效路径已在上面静默返回。
      userStore.claimMessage = '已取消领取';
      return;
    }
    if (ad.ok) {
      // Stage 5b: 受理 ≠ 生效 —— 进入"待确认"，由权威接口给出结论。
      recordClaimAcceptance('ad', 'status=1 via 广告上报');
      userStore.vipClaimPending = true;
      userStore.claimStage = 'confirm';
      userStore.claimMessage = '领取成功，正在确认权益…';
      const confirmMsg = await confirmVipAfterClaim({
        recheckAttempts,
        recheckIntervalMs,
        generation,
        ownerUserId,
      });
      // null = 任务已失效：不得覆盖当前领取文案（审阅 P1）。
      if (confirmMsg !== null) userStore.claimMessage = confirmMsg;
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
      // 失效（登出/换号/新领取）：静默退出 —— 不得写任何 claim 状态（审阅 P1）。
      if (!claimSessionStillOwned(generation, ownerUserId)) return;
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
      if (!claimSessionStillOwned(generation, ownerUserId)) return; // 失效：静默退出
      if (userStore.claimCancelRequested) {
        userStore.claimMessage = '已取消领取';
        return;
      }
      if (result?.status === 1) {
        // Stage 5b: 受理 ≠ 生效（与主通道一致）。
        recordClaimAcceptance(fb.label, `status=1 via ${fb.label}`);
        userStore.vipClaimPending = true;
        userStore.claimStage = 'confirm';
        userStore.claimMessage = '领取成功，正在确认权益…';
        const confirmMsg = await confirmVipAfterClaim({
          recheckAttempts,
          recheckIntervalMs,
          generation,
          ownerUserId,
        });
        // null = 任务已失效：不得覆盖当前领取文案（审阅 P1）。
        if (confirmMsg !== null) userStore.claimMessage = confirmMsg;
        return;
      }
      lastFailure = result;
    }
    const failBase = formatVipClaimFailure(lastFailure);
    recordClaimFailure(lastFailure?.error_code ?? lastFailure?.error, failBase);
    userStore.claimMessage = composeFailureWithPriorAcceptance(failBase);
  } catch (e: any) {
    console.error('Claim VIP error', e);
    const failBase = `领取失败：${describeBackendError(e, '网络异常或接口调用出错')}`;
    recordClaimFailure(null, failBase);
    userStore.claimMessage = composeFailureWithPriorAcceptance(failBase);
  } finally {
    // Only the owning generation may clear flow flags; a newer claim owns them.
    if (generation === claimGeneration) {
      userStore.loading = false;
      userStore.claimStage = '';
      userStore.claimCancelRequested = false;
      // W3（批次 C 审查）：取消路径返回时 pending 不残留 —— 两个调用方的
      // finally 统一兜底复位（"未确认"语义）。
      userStore.vipClaimPending = false;
    }
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

/** Debug UI surface for the day Concept candidate. Production default day
 *  claim remains Standard and must not share this entry. */
export const isDayConceptCandidateUiEnabled =
  typeof import.meta !== 'undefined' && Boolean((import.meta as any).env?.DEV);

const DAY_CONCEPT_CANDIDATE_LABEL = 'day Concept 候选';

function formatDayConceptCandidateFailure(result: any): string {
  if (
    result?.error === 'candidate_profile_unavailable' ||
    result?.error_code === 'candidate_profile_unavailable'
  ) {
    const detail = [result?.error_msg, result?.msg, result?.message]
      .find((v) => typeof v === 'string' && v.trim())?.trim();
    return `「${DAY_CONCEPT_CANDIDATE_LABEL}」不可用：${detail || '仅 Debug 构建可用，未发起上游领取'}`;
  }
  return `「${DAY_CONCEPT_CANDIDATE_LABEL}」${formatVipClaimFailure(result)}`;
}

/** Single-channel Debug experiment: one explicit Concept day claim.
 *  No auto-fallback, no other claim routes, same session ownership + VIP
 *  confirm as claimVipViaRoute. Not a claim-success claim by itself. */
export async function claimDayVipConceptCandidate(): Promise<void> {
  if (!userStore.isLoggedIn) {
    userStore.claimMessage = '请先登录！';
    return;
  }
  const generation = ++claimGeneration;
  const ownerUserId = userStore.userId;
  userStore.claimCancelRequested = false;
  userStore.loading = true;
  userStore.claimMessage = `尝试「${DAY_CONCEPT_CANDIDATE_LABEL}」…`;
  try {
    const deviceResult = await ensureVipDeviceReady({
      isCurrent: () => claimSessionStillOwned(generation, ownerUserId),
    });
    if (!deviceResult.ok) {
      userStore.claimMessage = formatDeviceGateFailure(deviceResult);
      return;
    }
    if (!claimSessionStillOwned(generation, ownerUserId)) return;
    const result = await claimYouthDayVipConceptCandidate();
    if (!claimSessionStillOwned(generation, ownerUserId)) return;
    if (userStore.claimCancelRequested) {
      userStore.claimMessage = '已取消领取';
      return;
    }
    if (
      result?.error === 'candidate_profile_unavailable' ||
      result?.error_code === 'candidate_profile_unavailable'
    ) {
      userStore.claimMessage = formatDayConceptCandidateFailure(result);
      return;
    }
    if (result?.status === 1) {
      recordClaimAcceptance('day-concept', `status=1 via ${DAY_CONCEPT_CANDIDATE_LABEL}`);
      userStore.vipClaimPending = true;
      userStore.claimStage = 'confirm';
      userStore.claimMessage = `「${DAY_CONCEPT_CANDIDATE_LABEL}」受理，正在确认权益…`;
      const confirmMsg = await confirmVipAfterClaim({
        recheckAttempts: VIP_CONFIRM_RECHECK_ATTEMPTS,
        recheckIntervalMs: VIP_CONFIRM_RECHECK_INTERVAL_MS,
        generation,
        ownerUserId,
      });
      if (confirmMsg !== null) userStore.claimMessage = confirmMsg;
      return;
    }
    const failBase = formatDayConceptCandidateFailure(result);
    recordClaimFailure(result?.error_code ?? result?.error, failBase);
    userStore.claimMessage = composeFailureWithPriorAcceptance(failBase);
  } catch (e: any) {
    console.error('Claim day Concept candidate error', e);
    const failBase = `「${DAY_CONCEPT_CANDIDATE_LABEL}」领取失败：${describeBackendError(e, '网络异常或接口调用出错')}`;
    recordClaimFailure(null, failBase);
    userStore.claimMessage = composeFailureWithPriorAcceptance(failBase);
  } finally {
    if (generation === claimGeneration) {
      userStore.loading = false;
      userStore.claimStage = '';
      userStore.claimCancelRequested = false;
      userStore.vipClaimPending = false;
    }
  }
}

export async function claimVipViaRoute(route: VipClaimRoute): Promise<void> {
  if (!userStore.isLoggedIn) {
    userStore.claimMessage = '请先登录！';
    return;
  }
  const label = VIP_CLAIM_ROUTES.find((r) => r.id === route)?.label ?? route;
  // 与 claimVip 的状态机对齐（批次 C 审查 P1）：开头复位取消标志，
  // finally 复位全部流程状态 —— 上一次运行的取消/阶段不得毒化本次。
  const generation = ++claimGeneration;
  const ownerUserId = userStore.userId;
  userStore.claimCancelRequested = false;
  userStore.loading = true;
  userStore.claimMessage = `尝试「${label}」…`;
  try {
    const deviceResult = await ensureVipDeviceReady({
      isCurrent: () => claimSessionStillOwned(generation, ownerUserId),
    });
    if (!deviceResult.ok) {
      userStore.claimMessage = formatDeviceGateFailure(deviceResult);
      return;
    }
    if (!claimSessionStillOwned(generation, ownerUserId)) return; // 失效：静默退出（审阅 P1）
    const calls: Record<VipClaimRoute, () => Promise<any>> = {
      day: claimYouthDayVip,
      listen: claimDailyVipSong,
      ad: claimYouthVipAd,
      'day-upgrade': claimYouthDayVipUpgrade,
    };
    const result = await calls[route]();
    if (!claimSessionStillOwned(generation, ownerUserId)) return; // 失效：静默退出（审阅 P1）
    if (userStore.claimCancelRequested) {
      userStore.claimMessage = '已取消领取';
      return;
    }
    if (result?.status === 1) {
      // Stage 5b: 受理 ≠ 生效（与主通道一致）。
      recordClaimAcceptance(route, `status=1 via ${label}`);
      userStore.vipClaimPending = true;
      userStore.claimStage = 'confirm';
      userStore.claimMessage = '领取成功，正在确认权益…';
      const confirmMsg = await confirmVipAfterClaim({
        recheckAttempts: VIP_CONFIRM_RECHECK_ATTEMPTS,
        recheckIntervalMs: VIP_CONFIRM_RECHECK_INTERVAL_MS,
        generation,
        ownerUserId,
      });
      // null = 任务已失效：不得覆盖当前领取文案（审阅 P1）。
      if (confirmMsg !== null) userStore.claimMessage = confirmMsg;
      return;
    }
    const failBase = `「${label}」${formatVipClaimFailure(result)}`;
    recordClaimFailure(result?.error_code ?? result?.error, failBase);
    userStore.claimMessage = composeFailureWithPriorAcceptance(failBase);
  } catch (e: any) {
    console.error(`Claim VIP via ${route} error`, e);
    const failBase = `「${label}」领取失败：${describeBackendError(e, '网络异常或接口调用出错')}`;
    recordClaimFailure(null, failBase);
    userStore.claimMessage = composeFailureWithPriorAcceptance(failBase);
  } finally {
    if (generation === claimGeneration) {
      userStore.loading = false;
      userStore.claimStage = '';
      userStore.claimCancelRequested = false;
      // W3：同 claimVip —— 取消/异常路径 pending 不残留。
      userStore.vipClaimPending = false;
    }
  }
}

export function logoutLocal() {
  // W1（批次 C 审查）：在途领取（确认重查 ≤30s）在下一 1s 边界自行停止——
  // 登出后不再发起 FFI、不再把 VIP 状态写回已登出的 store。
  loginOpGeneration += 1; // invalidate in-flight checkLoginStatus writes
  userStore.claimCancelRequested = true;
  claimGeneration += 1; // invalidate in-flight claim ownership
  // Local clear: resetLoginState already emits accountCleared.
  resetLoginState();
  userStore.claimMessage = '';
  userStore.loading = false;
  userStore.claimStage = '';
  notifyLocalLogout();
}
