import { reactive } from 'vue';
import { apiGet } from './backend';

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

      // Load VIP details. KuGou's get_union_vip returns:
      //   {
      //     data: {
      //       is_vip: 0/1,                     ← top-level paid-VIP flag
      //       vip_type, vip_end_time,
      //       svip_level, svip_score,          // historical/decorative
      //       busi_vip: [
      //         { product_type: "svip", is_vip: 0/1, vip_end_time, ... },  ← real song-unlock VIP
      //         { product_type: "tvip", is_vip: 0/1, vip_end_time, ... },  ← trial marker, doesn't unlock songs
      //       ],
      //     }
      //   }
      // VIP detection rules (in order of trust):
      //   1) top-level is_vip=1 OR vip_type>0  → paid VIP
      //   2) busi_vip[].product_type=="svip" && is_vip=1 && vip_end_time in future  → ad-reward / temp SVIP (unlocks songs)
      //   3) Otherwise the tvip-only state means free user
      const vip = await apiGet<any>('/user/vip/detail');
      if (vip && vip.status === 1 && vip.data) {
        const d = vip.data;
        userStore.vipLevel = Number(d.svip_level || d.vip_level || 0);
        userStore.vipType = Number(d.vip_type || 0);

        let isVip = d.is_vip === 1 || d.is_vip === '1' || Number(d.vip_type) > 0;
        let endTime = d.vip_end_time || d.end_time || '';
        if (!isVip && Array.isArray(d.busi_vip)) {
          const nowSec = Math.floor(Date.now() / 1000);
          for (const b of d.busi_vip) {
            if (!b) continue;
            const productType = String(b.product_type || '');
            const bIsVip = b.is_vip === 1 || b.is_vip === '1';
            if (productType === 'svip' && bIsVip) {
              // Verify the end time is still in the future
              const endStr = String(b.vip_end_time || '');
              const endMs = endStr ? new Date(endStr.replace(' ', 'T')).getTime() : 0;
              if (endMs === 0 || endMs / 1000 > nowSec) {
                isVip = true;
                endTime = endStr || endTime;
                break;
              }
            }
          }
        }
        userStore.isVip = isVip;
        userStore.vipEndDate = endTime;

        // Backfill avatar/nickname if /user/vip/detail surfaced them.
        if (d.nickname && !userStore.username.startsWith(d.nickname)) {
          userStore.username = d.nickname;
        }
        if (d.pic && !userStore.avatar) {
          userStore.avatar = d.pic;
        }
      }
    } else {
      userStore.isLoggedIn = false;
      userStore.userId = '';
      userStore.username = '未登录';
      userStore.avatar = '';
      userStore.vipLevel = 0;
      userStore.vipType = 0;
      userStore.isVip = false;
      userStore.vipEndDate = '';
    }
  } catch (e) {
    console.error('Check login status error', e);
    userStore.isLoggedIn = false;
    userStore.userId = '';
    userStore.username = '未登录';
    userStore.avatar = '';
    userStore.vipLevel = 0;
    userStore.vipType = 0;
    userStore.isVip = false;
    userStore.vipEndDate = '';
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

    if (listen?.status === 1 && listen?.data) {
      const endTime = Number(listen.data.ad_vip_end_time || 0);
      const serverTime = Number(listen.data.server_time || Math.floor(Date.now() / 1000));
      if (endTime > serverTime) {
        const endDate = new Date(endTime * 1000).toLocaleString('zh-CN');
        userStore.isVip = true;
        userStore.vipType = 5;
        userStore.vipEndDate = endDate;
        userStore.claimMessage = `✓ 已激活每日 VIP，到期：${endDate}`;
        await checkLoginStatus();
        return;
      }
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
  userStore.isLoggedIn = false;
  userStore.userId = '';
  userStore.username = '未登录';
  userStore.avatar = '';
  userStore.vipLevel = 0;
  userStore.vipType = 0;
  userStore.isVip = false;
  userStore.vipEndDate = '';
  userStore.claimMessage = '';
}
