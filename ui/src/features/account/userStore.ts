import { reactive } from 'vue';
import { describeBackendError } from '../../platform/tauri/nativeClient';
import {
  registerDevice,
  fetchUserDetail,
  fetchVipDetail,
  claimDailyVipSong,
} from './accountGateway';
import { resolveVip } from './vipResolver';
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
  loading: boolean;
  claimMessage: string;
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
  vipEndDate: '',
  loading: false,
  claimMessage: '',
});

function resetVipState() {
  userStore.vipLevel = 0;
  userStore.vipType = 0;
  userStore.isVip = false;
  userStore.vipEndDate = '';
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

export async function claimVip() {
  if (!userStore.isLoggedIn) {
    userStore.claimMessage = '请先登录！';
    return;
  }
  userStore.loading = true;
  userStore.claimMessage = '正在领取每日免费 VIP…';
  try {
    const deviceResult = await ensureVipDeviceReady();
    if (!deviceResult.ok) {
      userStore.claimMessage = formatDeviceGateFailure(deviceResult);
      return;
    }
    void notifyAccountReady(userStore.userId);
    const listen = await claimDailyVipSong();

    // 关键：listen_song 成功响应是 {status:1, data:"", error_msg:""} —— 不携带到期时间。
    // 成功与否只看 status===1；到期时间的权威来源是 /user/vip/detail (get_union_vip)。
    if (listen?.status === 1) {
      userStore.isVip = true;
      userStore.claimMessage = '领取成功，正在同步权益';
      try {
        const vip = await fetchVipDetail();
        const outcome = applyVipSnapshot(vip, { allowDowngrade: false });
        if (outcome === 'applied' && userStore.isVip) {
          userStore.claimMessage = userStore.vipEndDate
            ? `✓ 已激活每日 VIP，到期：${userStore.vipEndDate}`
            : '✓ 已激活每日 VIP';
        } else if (outcome === 'pending') {
          userStore.claimMessage = '领取成功，权益状态同步中';
        } else {
          userStore.claimMessage = '领取上报成功，会员状态查询失败';
        }
      } catch {
        userStore.claimMessage = '领取上报成功，会员状态查询失败';
      }
      return;
    }

    if (Number(listen?.error_code) === 130012) {
      userStore.claimMessage = '今天已经领过了';
      try {
        const vip = await fetchVipDetail();
        applyVipSnapshot(vip, { allowDowngrade: false });
      } catch {
        /* keep "already claimed"; only authoritative VIP may update state */
      }
      return;
    }

    userStore.claimMessage = formatVipClaimFailure(listen);
  } catch (e: any) {
    console.error('Claim VIP error', e);
    userStore.claimMessage = `领取失败：${describeBackendError(e, '网络异常或接口调用出错')}`;
  } finally {
    userStore.loading = false;
  }
}

export function logoutLocal() {
  // Local clear: resetLoginState already emits accountCleared.
  resetLoginState();
  userStore.claimMessage = '';
  notifyLocalLogout();
}
