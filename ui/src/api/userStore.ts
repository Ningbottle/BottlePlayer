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

        // VIP 到期时间的权威来源是 busi_vip 里 product_type=svip 且未过期的项
        // （今天领到的免费/广告 SVIP 在这里）。顶层 vip_end_time 可能是过期的历史付费 VIP。
        // 旧逻辑：顶层 is_vip=1 就短路，导致显示过期的顶层时间、无视 busi_vip 的有效时间
        // （症状：会员有效但“截止日期”是过去的日期）。
        // 新逻辑：扫描所有来源，取“最晚且未过期”的到期时间展示。
        let isVip = d.is_vip === 1 || d.is_vip === '1' || Number(d.vip_type) > 0;
        const nowMs = Date.now();
        // 酷狗返回 "YYYY-MM-DD HH:MM:SS"（北京时间，按本地解析做比较）；0 表示无法解析/永久。
        const toMs = (s: any): number => {
          const str = String(s || '');
          if (!str) return 0;
          const t = new Date(str.replace(' ', 'T')).getTime();
          return isNaN(t) ? 0 : t;
        };
        let bestStr = '';
        let bestRank = -1;
        const consider = (str: string) => {
          if (!str) return;
          const ms = toMs(str);
          const rank = ms === 0 ? Number.MAX_SAFE_INTEGER : ms; // 永久/无法解析 → 最高优先
          if ((ms === 0 || ms > nowMs) && rank > bestRank) {
            bestRank = rank;
            bestStr = str;
          }
        };
        if (Array.isArray(d.busi_vip)) {
          for (const b of d.busi_vip) {
            if (!b) continue;
            const isSvip = String(b.product_type || '') === 'svip';
            const bIsVip = b.is_vip === 1 || b.is_vip === '1';
            if (isSvip && bIsVip) {
              const endStr = String(b.vip_end_time || '');
              if (!endStr || toMs(endStr) === 0 || toMs(endStr) > nowMs) {
                isVip = true;
                consider(endStr);
              }
            }
          }
        }
        consider(String(d.vip_end_time || d.end_time || '')); // 顶层作为候选（过期则不入选）
        userStore.isVip = isVip;
        userStore.vipEndDate = bestStr || String(d.vip_end_time || d.end_time || '');

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
