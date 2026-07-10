import { reactive } from 'vue';
import { apiGet } from './backend';
import { resolveVip } from './vipResolver';
import { recentPlayedStore } from './recentPlayedStore';

interface UserState {
  isLoggedIn: boolean;
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
  userStore.userId = '';
  userStore.username = '未登录';
  userStore.avatar = '';
  resetVipState();
}

export async function checkLoginStatus() {
  userStore.loading = true;
  try {
    const detail = await apiGet<any>('/user/detail');
    if (detail && detail.status === 1 && detail.data) {
      userStore.userId = String(detail.data.userid || '');
      userStore.username = detail.data.nickname || detail.data.username || '听歌用户';
      userStore.avatar = detail.data.pic || detail.data.avatar || '';
      userStore.isLoggedIn = true;

      // Trigger lazy device registration with KuGou's risk service so that
      // /song/url returns full VIP audio (instead of 60s previews) and
      // /user/playlist stops returning error_code 20017. The backend is
      // idempotent — re-calls are cheap once registered=true.
      apiGet('/register/dev').catch(e => console.warn('Device upgrade failed', e));

      // VIP 解析抽到 vipResolver.resolveVip（纯函数，有单元测试覆盖）。
      // 规则摘要：顶层 is_vip/vip_type → 付费；busi_vip[svip] 未过期 → 临时 SVIP；
      // 到期时间取所有来源里"最晚且未过期"的。旧"顶层短路"bug 已由测试锁定。
      try {
        const vip = await apiGet<any>('/user/vip/detail');
        if (vip && vip.status === 1 && vip.data) {
          const r = resolveVip(vip.data, Date.now());
          userStore.vipLevel = r.vipLevel;
          userStore.vipType = r.vipType;
          userStore.isVip = r.isVip;
          userStore.vipEndDate = r.vipEndDate;

          // Backfill avatar/nickname if /user/vip/detail surfaced them.
          if (r.nickname && !userStore.username.startsWith(r.nickname)) {
            userStore.username = r.nickname;
          }
          if (r.pic && !userStore.avatar) {
            userStore.avatar = r.pic;
          }
        } else {
          resetVipState();
        }
      } catch (e) {
        console.warn('VIP detail refresh failed; keeping login state', e);
        resetVipState();
      }
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
    const listen = await apiGet<any>('/youth/listen/song');

    // 关键：listen_song 成功响应是 {status:1, data:"", error_msg:""} —— 不携带到期时间。
    // 成功与否只看 status===1；到期时间的权威来源是 /user/vip/detail (get_union_vip)。
    // 旧逻辑把成功挂在不存在的 data.ad_vip_end_time 上，导致领取成功却永远显示“领取失败”。
    if (listen?.status === 1) {
      await checkLoginStatus(); // 从 get_union_vip 刷新真实到期时间到 userStore.vipEndDate
      userStore.isVip = true;
      userStore.claimMessage = userStore.vipEndDate
        ? `✓ 已激活每日 VIP，到期：${userStore.vipEndDate}`
        : '✓ 已激活每日 VIP';
      return;
    }

    const errMsg = listen?.error_msg || listen?.error || '';
    if (errMsg.includes('已领') || errMsg.includes('已经领')) {
      userStore.claimMessage = '今天已经领过了';
    } else if (errMsg) {
      userStore.claimMessage = `领取失败：${errMsg}`;
    } else {
      userStore.claimMessage = '领取失败：需要在酷狗官方 App 内领取';
    }
  } catch (e: any) {
    console.error('Claim VIP error', e);
    userStore.claimMessage = '领取失败：网络异常或接口调用出错';
  } finally {
    userStore.loading = false;
  }
}

export function logoutLocal() {
  // Local clear
  resetLoginState();
  userStore.claimMessage = '';
  recentPlayedStore.reset();
}
