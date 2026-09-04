<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import QRCode from 'qrcode';
import { fetchQrKey, checkQrStatus, logoutAuth } from './accountGateway';
import { userStore, checkLoginStatus, claimVip, claimVipViaRoute, logoutLocal, VIP_CLAIM_ROUTES, type VipClaimRoute } from './userStore';
import { useThemeStore } from '../../app/appearance/themeStore';

const themeStore = useThemeStore();
const isAurora = computed(() => themeStore.skinId.value === 'aurora');

const emit = defineEmits<{
  (e: 'navigate', view: string): void;
}>();

const qrKey = ref('');
const qrCodeImg = ref('');
const loginStatus = ref(0); // 0: loading, 1: waiting, 2: scanned, 3: expired, 4: success, -1: error
const statusMessage = ref('正在生成登录二维码…');
let pollTimer: any = null;
let pollAbort = false;
let pollFailures = 0;
/** Login-success delayed navigate; cleared on unmount. */
let postLoginTimer: ReturnType<typeof setTimeout> | null = null;

const POLL_BASE_MS = 2_000;
const POLL_MAX_MS = 10_000;

async function generateQrCode() {
  loginStatus.value = 0;
  statusMessage.value = '正在请求安全通道…';
  qrKey.value = '';
  qrCodeImg.value = '';

  try {
    // 1. Get Key
    const keyRes = await fetchQrKey();
    if (keyRes.status === 1 && keyRes.data && keyRes.data.qrcode) {
      qrKey.value = keyRes.data.qrcode;
      // KuGou may return the QR image under different field names.
      const imgData = (keyRes.data.qrcode_img || keyRes.data.imgurl || keyRes.data.img_url || keyRes.data.img) as string | undefined;
      if (imgData) {
        qrCodeImg.value = imgData;
      } else if (typeof keyRes.data.qrcodeurl === 'string') {
        // Fall back to generating the QR code locally from the scan URL.
        qrCodeImg.value = await QRCode.toDataURL(keyRes.data.qrcodeurl, { width: 200, margin: 1 });
      }

      // 2. Start Polling
      loginStatus.value = 1;
      statusMessage.value = '请使用酷狗音乐手机 App 扫码登录';
      startPolling();
    } else {
      throw new Error('初始化登录通道失败');
    }
  } catch (err: any) {
    console.error('Failed to generate login QR', err);
    loginStatus.value = -1;
    statusMessage.value = err.message || '二维码初始化失败，请重试';
  }
}

function handleQrResponse(res: any) {
  if (!res || res.status !== 1 || !res.data) return;
  const remoteStatus = res.data.status;
  if (remoteStatus === 0 || remoteStatus === 1) {
    loginStatus.value = 1;
    statusMessage.value = '请使用酷狗音乐手机 App 扫码登录';
  } else if (remoteStatus === 2) {
    loginStatus.value = 2;
    statusMessage.value = '扫码成功！请在手机上确认登录';
  } else if (remoteStatus === 3 || remoteStatus === 5 || remoteStatus === 6) {
    loginStatus.value = 3;
    statusMessage.value = '二维码已过期，请点击刷新';
    stopPolling();
  } else if (remoteStatus === 4) {
    loginStatus.value = 4;
    statusMessage.value = '登录成功，正在同步档案…';
    stopPolling();

    if (postLoginTimer) clearTimeout(postLoginTimer);
    postLoginTimer = setTimeout(async () => {
      postLoginTimer = null;
      await checkLoginStatus();
      if (userStore.isLoggedIn) {
        emit('navigate', 'home');
      }
    }, 1000);
  }
}

async function pollLoop() {
  if (!qrKey.value || pollAbort) return;
  try {
    const res = await checkQrStatus(qrKey.value);
    pollFailures = 0;
    handleQrResponse(res);
  } catch (e) {
    pollFailures += 1;
    console.error('Polling QR status error', e);
    if (pollFailures >= 5) {
      statusMessage.value = '网络连接异常，请稍后刷新二维码重试';
      return;
    }
  }
  if (pollAbort) return;
  const delay = Math.min(POLL_BASE_MS * (1 + pollFailures), POLL_MAX_MS);
  pollTimer = setTimeout(pollLoop, delay);
}

function startPolling() {
  stopPolling();
  pollAbort = false;
  pollFailures = 0;
  pollLoop();
}

function stopPolling() {
  pollAbort = true;
  if (pollTimer) {
    clearTimeout(pollTimer);
    pollTimer = null;
  }
}

async function handleClaimVip() {
  await claimVip();
}

async function handleClaimVia(route: VipClaimRoute) {
  await claimVipViaRoute(route);
}

async function handleLogout() {
  if (confirm('确认退出当前账号吗？')) {
    try {
      // Backend clears session + device. Next QR scan binds a fresh
      // appid=1005 device, which KuGou recognises for VIP audio.
      await logoutAuth();
    } catch (e) {
      console.warn('Logout backend call failed (continuing)', e);
    }
    logoutLocal();
    generateQrCode();
  }
}

// ── 会员剩余时间（顶部平铺展示用）──────────────────────────────────────
// vipEndDate 是上游的 "YYYY-MM-DD HH:MM:SS" 字符串；本地每 30 秒刷新一次
// 相对剩余时间，不做权威判断（权威状态仍以 /user/vip/detail 为准）。
const now = ref(Date.now());
let nowTimer: ReturnType<typeof setInterval> | null = null;

const vipEndMs = computed(() => {
  const raw = userStore.vipEndDate;
  if (!raw) return null;
  const ms = Date.parse(raw.replace(' ', 'T'));
  return Number.isFinite(ms) ? ms : null;
});

const remainingLabel = computed(() => {
  if (!userStore.isVip) return '';
  if (vipEndMs.value == null) return '无期限';
  const diff = vipEndMs.value - now.value;
  if (diff <= 0) return '即将到期';
  const minutes = Math.floor(diff / 60_000);
  if (minutes < 60) return `剩 ${Math.max(1, minutes)} 分钟`;
  const hours = Math.floor(minutes / 60);
  return `剩 ${hours} 小时 ${minutes % 60} 分`;
});

const vipUrgent = computed(
  () => userStore.isVip && vipEndMs.value != null && vipEndMs.value - now.value < 3_600_000,
);

onMounted(() => {
  nowTimer = setInterval(() => {
    now.value = Date.now();
  }, 30_000);
  if (!userStore.isLoggedIn) {
    generateQrCode();
  }
});

onUnmounted(() => {
  stopPolling();
  if (nowTimer) {
    clearInterval(nowTimer);
    nowTimer = null;
  }
  if (postLoginTimer) {
    clearTimeout(postLoginTimer);
    postLoginTimer = null;
  }
});
</script>

<template>
  <div class="list-view login-view" :class="{ 'login-view--aurora': isAurora }">
    <div class="page-head">
      <div>
        <div class="kicker">{{ isAurora ? 'ACCOUNT · 账户' : 'SECURE LOG IN · 账户鉴权' }}</div>
        <h1>{{ userStore.isLoggedIn ? '我的账户' : '扫码登录' }}</h1>
      </div>
      <div class="date">
        <b>BOTTLE PLAYER</b> · {{ isAurora ? '极光账户中心' : '经典新闻纸质感' }}
      </div>
    </div>

    <!-- 已登录：无卡片，内容直接平铺，细线分区 -->
    <div v-if="userStore.isLoggedIn" class="account-flat">
      <!-- 第一行：资料在左，会员状态+领取按钮在右 -->
      <div class="flat-row profile-row">
        <div class="profile-side">
          <div class="avatar-wrap">
            <div class="avatar-large" :class="{ 'is-vip': userStore.isVip }">
              <img v-if="userStore.avatar" :src="userStore.avatar" alt="avatar" />
              <div v-else class="avatar-placeholder">听</div>
            </div>
            <span v-if="userStore.isVip" class="avatar-badge">VIP</span>
          </div>
          <div class="profile-meta">
            <h2 class="profile-name">{{ userStore.username }}</h2>
            <p class="user-id">ID {{ userStore.userId }}</p>
          </div>
        </div>

        <div class="membership-side">
          <div class="vip-info" :class="{ 'is-vip': userStore.isVip, 'is-urgent': vipUrgent }">
            <span class="vip-remaining">
              {{ userStore.isVip ? remainingLabel : '未开通' }}
            </span>
            <span
              class="vip-sub"
              :title="userStore.vipEndDate ? `到期时间 ${userStore.vipEndDate}` : undefined"
            >
              <template v-if="userStore.isVip">
                VIP · Lv.{{ userStore.vipLevel }}<template v-if="userStore.vipEndDate"> · 至 {{ userStore.vipEndDate.replace(/:\d\d$/, '') }}</template>
              </template>
              <template v-else>
                领取后解锁更高音质
              </template>
            </span>
          </div>

          <button
            class="play-cta claim-cta"
            type="button"
            data-test="claim-vip"
            @click="handleClaimVip"
            :disabled="userStore.loading"
          >
            {{ userStore.loading ? '领取中…' : (userStore.isVip ? '续领今日 VIP' : '领取每日免费 VIP') }}
          </button>
        </div>
      </div>

      <!-- 领取状态 -->
      <p v-if="userStore.claimMessage" class="claim-msg" role="status">
        {{ userStore.claimMessage }}
      </p>

      <!-- 其他领取通道 -->
      <div class="flat-row channels-row">
        <span class="flat-caption">其他领取通道</span>
        <div class="channel-links">
          <button
            v-for="route in VIP_CLAIM_ROUTES"
            :key="route.id"
            class="channel-link"
            type="button"
            :disabled="userStore.loading"
            @click="handleClaimVia(route.id)"
          >{{ route.label }}</button>
        </div>
      </div>

      <!-- 退出 -->
      <div class="flat-row logout-row">
        <button class="logout-link" type="button" @click="handleLogout">
          退出登录
        </button>
      </div>
    </div>

    <!-- 未登录：左二维码右指引，平铺无卡片 -->
    <div v-else class="qr-flat">
      <div class="qr-side">
        <div class="qr-container">
          <img v-if="qrCodeImg" :src="qrCodeImg" alt="QR Code" class="qr-code" />
          <div v-else class="qr-loading-placeholder">
            <span class="spinner-icon"></span>
          </div>

          <!-- Overlays for expired or success -->
          <div v-if="loginStatus === 3" class="qr-overlay" @click="generateQrCode">
            <span>点击刷新</span>
          </div>
          <div v-if="loginStatus === 4" class="qr-overlay success">
            <span>✓ 成功</span>
          </div>
        </div>
        <p class="status-text" :class="{ error: loginStatus === -1, success: loginStatus === 4 }">
          {{ statusMessage }}
        </p>
      </div>

      <div class="qr-guide">
        <h2 class="qr-guide-title">用酷狗音乐 App 扫码</h2>
        <ol class="qr-guide-steps">
          <li>打开手机上的 <b>酷狗音乐 App</b></li>
          <li>点右上角或侧边栏的 <b>“扫一扫”</b></li>
          <li>扫描左侧二维码并在手机上确认</li>
        </ol>
        <p class="qr-guide-note">登录后可领取每日免费 VIP，解锁更高音质。</p>
        <button
          v-if="loginStatus === 3 || loginStatus === -1"
          class="icon-btn refresh-btn"
          @click="generateQrCode"
        >
          刷新二维码 ↻
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* 铺满内容区，无卡片：细线 + 留白分区 */
.login-view {
  padding: 22px 34px 48px;
}

.account-flat {
  margin-top: 26px;
  display: flex;
  flex-direction: column;
  gap: 36px;
}

.flat-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 18px 40px;
  flex-wrap: wrap;
}

.flat-caption {
  font-size: 12px;
  letter-spacing: 0.06em;
  color: var(--ink-mute, #8a7e6a);
}

/* ── 资料 + 会员 ─────────────────────────────────────────────── */
.profile-row {
  border-top: 2px solid var(--ink);
  padding-top: 28px;
}

.profile-side {
  display: flex;
  align-items: center;
  gap: 20px;
  min-width: 0;
}

.avatar-wrap {
  position: relative;
  flex: none;
}

.avatar-badge {
  position: absolute;
  bottom: -8px;
  left: 50%;
  transform: translateX(-50%);
  background: var(--accent, #a8311b);
  color: var(--paper);
  border: 1.5px solid var(--ink);
  box-shadow: 2px 2px 0 var(--ink-soft);
  border-radius: 999px;
  padding: 2px 10px;
  font-size: 11px;
  font-weight: 800;
  letter-spacing: 0.1em;
  white-space: nowrap;
}

.avatar-large {
  width: 96px;
  height: 96px;
  flex: none;
  border-radius: 50%;
  border: 2px solid var(--ink);
  overflow: hidden;
  background: var(--paper);
}

.avatar-large.is-vip {
  border-color: var(--accent, #a8311b);
}

.avatar-large img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.avatar-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 34px;
  font-family: 'Noto Serif SC', serif;
  color: var(--ink-soft);
  font-weight: 700;
}

.profile-meta {
  min-width: 0;
}

.profile-name {
  margin: 0;
  font-size: 1.7rem;
  font-weight: 700;
  line-height: 1.2;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.user-id {
  margin: 8px 0 0;
  font-family: ui-monospace, monospace;
  font-size: 12px;
  color: var(--ink-mute, #8a7e6a);
  letter-spacing: 0.02em;
}

.membership-side {
  display: flex;
  align-items: center;
  gap: 34px;
  flex-wrap: wrap;
}

/* 会员信息 = 纯文字，剩余时长是视觉焦点 */
.vip-info {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  min-width: 170px;
}

.vip-remaining {
  font-size: 34px;
  font-weight: 800;
  line-height: 1.15;
  letter-spacing: 0.01em;
  color: var(--ink-soft);
}

.vip-sub {
  margin-top: 6px;
  font-size: 12px;
  color: var(--ink-mute, #8a7e6a);
}

.vip-info.is-vip .vip-remaining {
  color: var(--accent, #a8311b);
}

.vip-info.is-vip .vip-sub {
  color: color-mix(in srgb, var(--accent, #a8311b) 72%, var(--ink-soft));
}

.claim-cta {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 220px;
  padding: 10px 26px;
  border-radius: 999px;
  background: var(--ink);
  color: var(--paper);
  font-size: 14px;
  font-weight: 600;
  letter-spacing: 0.06em;
  cursor: pointer;
  transition: background 0.15s ease;
}

.claim-cta:hover:not(:disabled) {
  background: var(--accent, #a8311b);
}

.claim-cta:disabled {
  opacity: 0.55;
  cursor: default;
}

.claim-msg {
  margin: 0;
  font-size: 13px;
  line-height: 1.5;
  color: var(--accent, #a8311b);
}

/* ── 其他领取通道 ────────────────────────────────────────────── */
.channel-links {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 26px;
}

.channel-link {
  background: none;
  border: none;
  padding: 2px 0;
  font-size: 13px;
  color: var(--ink-soft);
  text-decoration: underline;
  text-underline-offset: 4px;
  cursor: pointer;
}

.channel-link:hover:not(:disabled) {
  color: var(--accent);
}

.channel-link:disabled {
  opacity: 0.5;
  cursor: default;
}

/* ── 退出 ────────────────────────────────────────────────────── */
.logout-row {
  justify-content: flex-end;
}

.logout-link {
  background: none;
  border: none;
  color: var(--ink-mute, #8a7e6a);
  text-decoration: underline;
  text-underline-offset: 4px;
  cursor: pointer;
  font-size: 13px;
}

.logout-link:hover {
  color: var(--accent);
}

/* ── 未登录：二维码 + 指引，平铺 ─────────────────────────────── */
.qr-flat {
  margin-top: 30px;
  padding-top: 28px;
  border-top: 2px solid var(--ink);
  display: flex;
  gap: 56px;
  align-items: flex-start;
  flex-wrap: wrap;
}

.qr-side {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 14px;
}

.qr-container {
  position: relative;
  width: 210px;
  height: 210px;
  border: 1px solid var(--ink);
  background: #fff;
  padding: 6px;
}

.qr-code {
  width: 100%;
  height: 100%;
}

.qr-loading-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--paper);
}

.spinner-icon {
  width: 30px;
  height: 30px;
  border: 2px solid var(--rule);
  border-top-color: var(--accent);
  border-radius: 50%;
  animation: spin 1s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.qr-overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(241, 234, 216, 0.9);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  font-weight: 700;
  font-size: 16px;
  color: var(--accent);
}

.qr-overlay.success {
  background: rgba(241, 234, 216, 0.95);
  color: green;
}

.status-text {
  margin: 0;
  font-weight: 700;
  font-size: 13px;
  text-align: center;
}

.status-text.error {
  color: var(--accent);
}

.status-text.success {
  color: green;
}

.qr-guide-title {
  margin: 0 0 14px;
  font-size: 1.35rem;
  font-weight: 700;
}

.qr-guide-steps {
  margin: 0;
  padding-left: 1.4em;
  display: flex;
  flex-direction: column;
  gap: 8px;
  font-size: 14px;
  line-height: 1.6;
  color: var(--ink-soft);
}

.qr-guide-note {
  margin: 16px 0 0;
  font-size: 12px;
  color: var(--ink-mute, #8a7e6a);
}

.refresh-btn {
  margin-top: 16px;
  font-size: 12px;
  width: auto;
  padding: 4px 14px;
}

/* ── 极光皮肤：同样平铺，只换颜色 ───────────────────────────── */
.login-view--aurora .page-head h1 {
  letter-spacing: -0.02em;
}

.login-view--aurora .profile-row,
.login-view--aurora .qr-flat {
  border-top-color: color-mix(in srgb, var(--text-primary, #e8e6f2) 22%, transparent);
}

.login-view--aurora .avatar-large {
  border-color: color-mix(in srgb, var(--text-primary, #e8e6f2) 18%, transparent);
  background: color-mix(in srgb, var(--text-primary, #e8e6f2) 6%, transparent);
}

.login-view--aurora .avatar-large.is-vip {
  border-color: color-mix(in srgb, var(--accent, #8b7cf6) 65%, transparent);
}

.login-view--aurora .avatar-badge {
  border: none;
  box-shadow: 0 6px 16px color-mix(in srgb, var(--accent, #8b7cf6) 40%, transparent);
  background: linear-gradient(
    135deg,
    var(--accent, #8b7cf6),
    color-mix(in srgb, var(--accent, #8b7cf6) 55%, #5ad1ff)
  );
  color: #0b0c12;
}

.login-view--aurora .user-id,
.login-view--aurora .flat-caption,
.login-view--aurora .vip-sub,
.login-view--aurora .qr-guide-note,
.login-view--aurora .logout-link {
  color: var(--text-muted, #9a97ad);
}

.login-view--aurora .vip-remaining {
  color: var(--text-primary, #e8e6f2);
}

.login-view--aurora .vip-info.is-vip .vip-remaining {
  color: color-mix(in srgb, var(--accent, #8b7cf6) 85%, #fff);
}

.login-view--aurora .vip-info.is-vip .vip-sub {
  color: color-mix(in srgb, var(--accent, #8b7cf6) 70%, var(--text-muted, #9a97ad));
}

.login-view--aurora .claim-cta {
  border: 0;
  border-radius: 999px;
  min-height: 44px;
  background: linear-gradient(
    135deg,
    var(--accent, #8b7cf6),
    color-mix(in srgb, var(--accent, #8b7cf6) 55%, #5ad1ff)
  );
  color: #0b0c12;
  box-shadow: 0 10px 24px color-mix(in srgb, var(--accent, #8b7cf6) 30%, transparent);
}

.login-view--aurora .claim-msg {
  color: color-mix(in srgb, var(--accent, #8b7cf6) 80%, #fff);
}

.login-view--aurora .channel-link {
  color: var(--text-muted, #9a97ad);
}

.login-view--aurora .channel-link:hover:not(:disabled) {
  color: color-mix(in srgb, var(--accent, #8b7cf6) 85%, #fff);
}

.login-view--aurora .qr-container {
  border-radius: 12px;
  border-color: color-mix(in srgb, var(--text-primary, #e8e6f2) 16%, transparent);
}

/* Dark mode overrides */
:global(:root[data-mode="dark"]) .qr-container {
  background: var(--paper-2);
}
:global(:root[data-mode="dark"]) .qr-overlay {
  background: rgba(30, 30, 30, 0.95);
}
:global(:root[data-mode="dark"]) .qr-overlay.success {
  background: rgba(30, 30, 30, 0.97);
}
</style>
