<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import QRCode from 'qrcode';
import { apiGet, apiPost } from '../api/backend';
import { userStore, checkLoginStatus, claimVip, logoutLocal } from '../api/userStore';
import { useThemeStore } from '../api/themeStore';

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
    const keyRes = await apiGet<any>('/login/qr/key');
    if (keyRes.status === 1 && keyRes.data && keyRes.data.qrcode) {
      qrKey.value = keyRes.data.qrcode;
      // KuGou may return the QR image under different field names.
      const imgData = keyRes.data.qrcode_img || keyRes.data.imgurl || keyRes.data.img_url || keyRes.data.img;
      if (imgData) {
        qrCodeImg.value = imgData;
      } else if (keyRes.data.qrcodeurl) {
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
    const res = await apiGet<any>('/login/qr/check', { key: qrKey.value });
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

async function handleLogout() {
  if (confirm('确认退出当前账号吗？')) {
    try {
      // Backend clears session + device. Next QR scan binds a fresh
      // appid=1005 device, which KuGou recognises for VIP audio.
      await apiPost<{ status: number }>('/auth/logout');
    } catch (e) {
      console.warn('Logout backend call failed (continuing)', e);
    }
    logoutLocal();
    generateQrCode();
  }
}

onMounted(() => {
  if (!userStore.isLoggedIn) {
    generateQrCode();
  }
});

onUnmounted(() => {
  stopPolling();
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

    <div class="login-card">
      <div class="retro-box" :class="{ 'retro-box--aurora': isAurora }">
        <!-- Logged in state -->
        <div v-if="userStore.isLoggedIn" class="logged-in-profile">
          <div class="avatar-large">
            <img v-if="userStore.avatar" :src="userStore.avatar" alt="avatar" />
            <div v-else class="avatar-placeholder">听</div>
          </div>
          <h2>{{ userStore.username }}</h2>
          <div class="user-id">ID: {{ userStore.userId }}</div>

          <div class="vip-panel" :class="{ 'is-vip': userStore.isVip, 'vip-panel--aurora': isAurora }">
            <div class="vip-panel-badge">{{ userStore.isVip ? 'VIP' : 'FREE' }}</div>
            <div class="vip-panel-body">
              <div class="vip-label">{{ userStore.isVip ? 'VIP 会员' : '普通用户' }}</div>
              <div v-if="userStore.isVip" class="vip-details">
                等级 Lv.{{ userStore.vipLevel }} · 至 {{ userStore.vipEndDate || '无期限' }}
              </div>
              <div v-else class="vip-details">领取后可解锁更高音质与完整曲库权益</div>
            </div>
            <button
              class="vip-claim-btn"
              type="button"
              @click="handleClaimVip"
              :disabled="userStore.loading"
            >
              {{ userStore.loading ? '领取中…' : (userStore.isVip ? '续领今日 VIP' : '领取每日免费 VIP') }}
            </button>
            <div v-if="userStore.claimMessage" class="claim-msg">
              {{ userStore.claimMessage }}
            </div>
          </div>

          <button class="logout-btn" type="button" @click="handleLogout">
            退出登录
          </button>
        </div>

        <!-- Logged out QR state -->
        <div v-else class="qr-login-flow">
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

          <div class="help-info">
            请打开 <b>酷狗音乐 App</b>，点击右上角或侧边栏的 <b>“扫一扫”</b> 扫描上方二维码。
          </div>

          <button v-if="loginStatus === 3 || loginStatus === -1" class="icon-btn refresh-btn" @click="generateQrCode">
            刷新二维码 ↻
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.login-view {
  max-width: 600px;
  margin: 0 auto;
  padding: 20px;
}

.login-card {
  margin-top: 40px;
  display: flex;
  justify-content: center;
}

.retro-box {
  width: 100%;
  max-width: 420px;
  border: 2px solid var(--ink);
  padding: 30px;
  background-color: var(--paper-alt);
  box-shadow: 6px 6px 0 var(--ink-soft);
  text-align: center;
}

.logged-in-profile {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.avatar-large {
  width: 90px;
  height: 90px;
  border-radius: 50%;
  border: 2px solid var(--ink);
  overflow: hidden;
  margin-bottom: 16px;
  background: var(--paper);
  box-shadow: 3px 3px 0 var(--rule);
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
  font-size: 32px;
  font-family: 'Noto Serif SC', serif;
  color: var(--ink-soft);
  font-weight: 700;
}

.user-id {
  font-family: monospace;
  font-size: 13px;
  color: var(--ink-mute);
  margin-top: 4px;
}

.vip-panel {
  margin-top: 22px;
  width: 100%;
  padding: 16px 18px;
  border: 1px dashed var(--ink-mute, #8a7e6a);
  background: rgba(34, 27, 18, 0.03);
  border-radius: 4px;
  display: grid;
  gap: 10px;
  justify-items: stretch;
  text-align: left;
}

.vip-panel.is-vip {
  border-style: solid;
  border-color: color-mix(in srgb, var(--accent, #a8311b) 55%, transparent);
  background: color-mix(in srgb, var(--accent, #a8311b) 8%, transparent);
}

.vip-panel-badge {
  display: inline-flex;
  width: fit-content;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.06em;
  color: var(--paper, #fff);
  background: var(--ink-soft, #5c5346);
}

.vip-panel.is-vip .vip-panel-badge {
  background: var(--accent, #a8311b);
}

.vip-label {
  font-weight: 700;
  font-size: 16px;
}

.vip-panel.is-vip .vip-label {
  color: var(--accent, #a8311b);
}

.vip-details {
  font-size: 12px;
  color: var(--ink-mute, #8a7e6a);
  margin-top: 2px;
  line-height: 1.45;
}

.vip-claim-btn {
  margin-top: 4px;
  width: 100%;
  border: 0;
  border-radius: 999px;
  padding: 10px 16px;
  font: inherit;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  color: var(--paper, #fff);
  background: var(--accent, #a8311b);
  transition: opacity 0.15s, transform 0.15s;
}

.vip-claim-btn:hover:not(:disabled) {
  transform: translateY(-1px);
}

.vip-claim-btn:disabled {
  opacity: 0.55;
  cursor: default;
}

.claim-msg {
  font-size: 12px;
  color: var(--accent, #a8311b);
  line-height: 1.4;
}

.logout-btn {
  margin-top: 28px;
  background: none;
  border: none;
  color: var(--ink-mute, #8a7e6a);
  text-decoration: underline;
  cursor: pointer;
  font-size: 12px;
}

/* Aurora: glass card fused with shell tokens (no newspaper stamp shadow). */
.login-view--aurora {
  max-width: 520px;
}

.login-view--aurora .page-head h1 {
  letter-spacing: -0.02em;
}

.retro-box--aurora {
  border: 1px solid color-mix(in srgb, var(--text-primary, #e8e6f2) 12%, transparent);
  background: color-mix(in srgb, var(--surface-elevated, #1a1b28) 88%, transparent);
  box-shadow:
    0 18px 48px rgba(0, 0, 0, 0.28),
    inset 0 1px 0 color-mix(in srgb, #fff 8%, transparent);
  border-radius: 18px;
  color: var(--text-primary, #e8e6f2);
  backdrop-filter: blur(18px);
}

.login-view--aurora .avatar-large {
  border-color: color-mix(in srgb, var(--text-primary, #e8e6f2) 22%, transparent);
  box-shadow: none;
  background: color-mix(in srgb, var(--text-primary, #e8e6f2) 6%, transparent);
}

.login-view--aurora .user-id,
.login-view--aurora .vip-details,
.login-view--aurora .logout-btn {
  color: var(--text-muted, #9a97ad);
}

.vip-panel--aurora {
  border: 1px solid color-mix(in srgb, var(--text-primary, #e8e6f2) 10%, transparent);
  background: color-mix(in srgb, var(--text-primary, #e8e6f2) 5%, transparent);
  border-radius: 14px;
}

.vip-panel--aurora.is-vip {
  border-color: color-mix(in srgb, var(--accent, #8b7cf6) 45%, transparent);
  background: linear-gradient(
    145deg,
    color-mix(in srgb, var(--accent, #8b7cf6) 16%, transparent),
    color-mix(in srgb, var(--text-primary, #e8e6f2) 4%, transparent)
  );
}

.vip-panel--aurora .vip-claim-btn {
  background: linear-gradient(135deg, var(--accent, #8b7cf6), color-mix(in srgb, var(--accent, #8b7cf6) 70%, #5ad1ff));
  box-shadow: 0 8px 20px color-mix(in srgb, var(--accent, #8b7cf6) 28%, transparent);
}

.logout-btn:hover {
  color: var(--accent);
}

.qr-login-flow {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.qr-container {
  position: relative;
  width: 200px;
  height: 200px;
  border: 1px solid var(--ink);
  background: #fff;
  padding: 6px;
  box-shadow: 4px 4px 0 var(--rule);
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
  margin-top: 16px;
  font-weight: 700;
  font-size: 14px;
}

.status-text.error {
  color: var(--accent);
}

.status-text.success {
  color: green;
}

.help-info {
  margin-top: 16px;
  font-size: 12px;
  color: var(--ink-soft);
  line-height: 1.5;
  border-top: 1px solid var(--rule);
  padding-top: 14px;
  width: 100%;
}

.refresh-btn {
  margin-top: 14px;
  font-size: 12px;
  width: auto;
  padding: 4px 14px;
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
:global(:root[data-mode="dark"]) .vip-status-box {
  background: rgba(255,255,255,0.03);
}
:global(:root[data-mode="dark"]) .vip-status-box.is-vip {
  background: rgba(168,49,27,0.12);
}
</style>
