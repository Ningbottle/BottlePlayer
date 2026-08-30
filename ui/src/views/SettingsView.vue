<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { apiGet, apiPost } from '../platform/tauri/nativeClient';
import { checkLoginStatus, ensureVipDeviceReady, formatVipClaimFailure } from '../api/userStore';
import { check } from '@tauri-apps/plugin-updater';
import { relaunch } from '@tauri-apps/plugin-process';
import { openUrl } from '@tauri-apps/plugin-opener';
import { useAppearanceStore, type AppearanceSettings } from '../app/appearance/appearanceStore';
import { setSkippedVersion } from '../api/skippedVersion';
import { playbackDiagnostics, type DiagEvent } from '../playback/playbackDiagnostics';
import { crossfadeTheme } from '../shared/motion/motion';
import { transitionEnter, transitionLeave } from '../app/navigation/pageTransitions';
import SkinPageHeader from '../components/primitives/SkinPageHeader.vue';
import SkinButton from '../components/primitives/SkinButton.vue';
import SkinEmptyState from '../components/primitives/SkinEmptyState.vue';

const appearanceStore = useAppearanceStore();
appearanceStore.init();

type SectionId = 'appearance' | 'device' | 'vip' | 'update' | 'storage' | 'diagnostics';
const activeSection = ref<SectionId>('appearance');

async function selectSkin(id: AppearanceSettings['skin']) {
  await crossfadeTheme(() => appearanceStore.setSkin(id));
}
async function selectMode(m: AppearanceSettings['mode']) {
  await crossfadeTheme(() => appearanceStore.setMode(m));
}
function selectAccent(event: Event) {
  appearanceStore.setAccent((event.target as HTMLInputElement).value);
}
function selectCompactList(event: Event) {
  appearanceStore.setCompactList((event.target as HTMLInputElement).checked);
}
function selectLyricAlign(value: AppearanceSettings['lyricAlign']) {
  appearanceStore.setLyricAlign(value);
}

interface MemoryData {
  working_set_bytes: number;
  private_bytes: number;
  image_cache_bytes: number;
  pending_task_count: number;
  playback_state: string;
  text: string;
}

interface DeviceInfo {
  dfid: string;
  mid: string;
  uuid: string;
  appid: string;
  clientver: string;
  registered: boolean;
}

const loading = ref(false);
const memoryInfo = ref<MemoryData | null>(null);

// VIP claiming state
const listenVipLoading = ref(false);
const listenVipMsg = ref('');
const adVipLoading = ref(false);
const adVipMsg = ref('');

// Auto-update state
const updateStatus = ref('');
const updateLoading = ref(false);
const updateVersion = ref('');
const updateBody = ref('');
const updateDownloading = ref(false);
const updateProgress = ref(0);
// 缓存 check() 结果，避免下载时重复请求
let cachedUpdate: any = null;

async function checkForUpdate() {
  updateLoading.value = true;
  updateStatus.value = '正在检查更新…';
  updateVersion.value = '';
  updateBody.value = '';
  cachedUpdate = null;
  try {
    const update = await check();
    cachedUpdate = update || null;
    if (update) {
      updateVersion.value = update.version;
      updateBody.value = update.body || '';
      updateStatus.value = `发现新版本 v${update.version}`;
    } else {
      updateStatus.value = '✓ 已是最新版本';
    }
  } catch (e: any) {
    updateStatus.value = '检查更新失败：' + (e?.message || String(e));
  } finally {
    updateLoading.value = false;
  }
}

async function downloadAndInstall() {
  updateDownloading.value = true;
  updateProgress.value = 0;
  updateStatus.value = '正在下载更新…';
  let downloadedBytes = 0;
  let totalBytes = 0;
  try {
    const update = cachedUpdate || await check();
    if (!update) {
      updateStatus.value = '没有可用更新';
      updateDownloading.value = false;
      return;
    }
    await update.downloadAndInstall((event: any) => {
      switch (event.event) {
        case 'Started':
          totalBytes = event.data?.contentLength || 0;
          downloadedBytes = 0;
          updateProgress.value = 0;
          break;
        case 'Progress':
          downloadedBytes += event.data.chunkLength;
          if (totalBytes > 0) {
            updateProgress.value = Math.round((downloadedBytes / totalBytes) * 100);
          }
          break;
        case 'Finished':
          updateProgress.value = 100;
          break;
      }
    });
    updateStatus.value = '✓ 更新已安装，正在重启…';
    await relaunch();
  } catch (e: any) {
    updateStatus.value = '下载失败：' + (e?.message || String(e));
  } finally {
    updateDownloading.value = false;
  }
}

function skipVersion() {
  if (updateVersion.value) {
    setSkippedVersion(updateVersion.value);
    updateStatus.value = `已跳过 v${updateVersion.value}（下次有新版本再提醒）`;
    updateVersion.value = '';
    updateBody.value = '';
    cachedUpdate = null;
  }
}

// Custom device fingerprint (used to unlock VIP audio when KuGou's risk
// service rejects randomly-generated dfids).
const device = ref<DeviceInfo | null>(null);
const dfidInput = ref('');
const midInput = ref('');
const uuidInput = ref('');
const deviceStatus = ref('');

async function loadDiagnostics() {
  loading.value = true;
  try {
    const res = await apiGet<{ status: number; data?: MemoryData }>('/diagnostics/memory');
    if (res.status === 1 && res.data) {
      memoryInfo.value = res.data;
    }
  } catch (e) {
    console.error('Failed to get diagnostics memory', e);
  } finally {
    loading.value = false;
  }
}

async function loadDevice() {
  try {
    const res = await apiGet<{ status: number; data: DeviceInfo }>('/settings/device');
    if (res.status === 1 && res.data) {
      device.value = res.data;
      dfidInput.value = res.data.dfid || '';
      midInput.value = res.data.mid || '';
      uuidInput.value = res.data.uuid || '';
    }
  } catch (e) {
    console.error('Failed to load device', e);
  }
}

async function saveDevice() {
  deviceStatus.value = '保存中…';
  try {
    const query: Record<string, string> = {};
    if (dfidInput.value.trim()) query.dfid = dfidInput.value.trim();
    if (midInput.value.trim()) query.mid = midInput.value.trim();
    if (uuidInput.value.trim()) query.uuid = uuidInput.value.trim();
    const res = await apiPost<{ status: number; data: DeviceInfo; updated: boolean }>('/settings/device', undefined, query);
    if (res.status === 1) {
      device.value = res.data;
      deviceStatus.value = res.updated
        ? '✓ 已保存，下一次播放/拉歌单将使用新指纹'
        : '（参数未变化）';
    } else {
      deviceStatus.value = '保存失败';
    }
  } catch (e: any) {
    deviceStatus.value = '出错：' + (e?.message || String(e));
  }
}

async function resetDevice() {
  if (!confirm('清除设备指纹？将删除当前自定义 dfid/mid/uuid，退化为未注册占位。')) return;
  deviceStatus.value = '重置中…';
  try {
    const res = await apiPost<{ status: number; data: DeviceInfo }>('/settings/device', undefined, { clear: '1' });
    if (res.status === 1) {
      device.value = res.data;
      dfidInput.value = res.data.dfid || '';
      midInput.value = res.data.mid || '';
      uuidInput.value = res.data.uuid || '';
      deviceStatus.value = '✓ 已重置';
    }
  } catch (e: any) {
    deviceStatus.value = '出错：' + (e?.message || String(e));
  }
}

async function openDeviceHelp() {
  try {
    await openUrl('https://m.kugou.com/');
  } catch (e: any) {
    deviceStatus.value = '无法打开系统浏览器：' + (e?.message || String(e));
  }
}

// Probe whether KuGou trusts the current saved device. Calls /song/url with a
// well-known free song hash; analyzes the upstream response to tell the user
// instantly if the dfid is registered (status:1 + /full/), risk-controlled
// (errcode:20028), or VIP-locked (fail_process). Avoids the round-trip of
// "save → close settings → click a track → check banner".
async function testDevice() {
  deviceStatus.value = '测试中…';
  try {
    // hash f0a6ba24... (风中芭蕾) is a known concept-edition track that
    // KuGou serves as /full/ to trusted devices.
    const res = await apiGet<any>('/song/url', {
      hash: 'F0A6BA24635A8560F96C2C2D603E8CA8',
      album_id: '1776319',
      album_audio_id: '39905465',
    });
    if (res?.status === 1 && res?.url) {
      const url: string = res.url;
      if (url.includes('/yp/full/') || url.includes('/full/')) {
        deviceStatus.value = '✓ KuGou 信任该指纹（拿到 /full/ 完整 URL）';
      } else if (url.match(/\/p_0_\d+\//)) {
        deviceStatus.value = '⚠ 拿到的是 60s 试听（设备可能未被信任或账号需要 VIP）';
      } else {
        deviceStatus.value = '⚠ URL 形态未知：' + url.slice(0, 80) + '...';
      }
    } else {
      deviceStatus.value = '✗ 失败：' + (res?.error || '未拿到 URL');
    }
  } catch (e: any) {
    deviceStatus.value = '出错：' + (e?.message || String(e));
  }
}

async function claimListenVip() {
  listenVipLoading.value = true;
  listenVipMsg.value = '';
  try {
    const deviceResult = await ensureVipDeviceReady();
    if (!deviceResult.ok) {
      listenVipMsg.value = `领取失败：设备注册失败${deviceResult.error ? `（${deviceResult.error}）` : ''}`;
      return;
    }
    const res = await apiGet<any>('/youth/listen/song');
    if (res?.status === 1) {
      listenVipMsg.value = '✓ 听歌领 VIP 成功';
      await checkLoginStatus(); // 领取成功后刷新持久 VIP 状态/到期时间（权威来源 get_union_vip）
    } else {
      listenVipMsg.value = formatVipClaimFailure(res);
    }
  } catch (e: any) {
    listenVipMsg.value = '出错：' + (e?.message || String(e));
  } finally {
    listenVipLoading.value = false;
  }
}

async function claimAdVip() {
  adVipLoading.value = true;
  adVipMsg.value = '';
  try {
    const deviceResult = await ensureVipDeviceReady();
    if (!deviceResult.ok) {
      adVipMsg.value = `领取失败：设备注册失败${deviceResult.error ? `（${deviceResult.error}）` : ''}`;
      return;
    }
    const res = await apiGet<any>('/youth/vip/ad');
    if (res?.status === 1) {
      adVipMsg.value = '✓ 领取成功';
      await checkLoginStatus(); // 领取成功后刷新持久 VIP 状态/到期时间
    } else {
      adVipMsg.value = formatVipClaimFailure(res);
    }
  } catch (e: any) {
    adVipMsg.value = '出错：' + (e?.message || String(e));
  } finally {
    adVipLoading.value = false;
  }
}

onMounted(() => {
  loadDiagnostics();
  loadDevice();
  refreshDiag();
});

function formatBytes(bytes: number) {
  if (!bytes) return '0 MB';
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

// ── Playback diagnostics (frontend event ring buffer) ──
const diagEvents = ref<DiagEvent[]>([]);
function refreshDiag() {
  diagEvents.value = playbackDiagnostics.getEvents();
}
async function copyDiag() {
  try {
    await navigator.clipboard.writeText(playbackDiagnostics.copyAsText());
  } catch (e) {
    console.warn('clipboard write failed', e);
  }
}
</script>

<template>
  <div class="list-view">
    <SkinPageHeader title="偏好设置" kicker="CLIENT OPTIONS · 印务配置" subtitle="Settings">
      <template #actions>
        <span class="settings-header-meta">C++20 内核调测器</span>
      </template>
    </SkinPageHeader>

    <div class="settings-shell">
      <nav class="settings-nav">
        <button class="settings-nav-item" :class="{ active: activeSection === 'appearance' }" data-test="settings-nav-item" @click="activeSection = 'appearance'">外观</button>
        <button class="settings-nav-item" :class="{ active: activeSection === 'device' }" data-test="settings-nav-item" @click="activeSection = 'device'">设备</button>
        <button class="settings-nav-item" :class="{ active: activeSection === 'vip' }" data-test="settings-nav-item" @click="activeSection = 'vip'">VIP</button>
        <button class="settings-nav-item" :class="{ active: activeSection === 'update' }" data-test="settings-nav-item" @click="activeSection = 'update'">更新</button>
        <button class="settings-nav-item" :class="{ active: activeSection === 'storage' }" data-test="settings-nav-item" @click="activeSection = 'storage'">存储</button>
        <button class="settings-nav-item" :class="{ active: activeSection === 'diagnostics' }" data-test="settings-nav-item" @click="activeSection = 'diagnostics'">诊断</button>
      </nav>

      <div class="settings-content">
        <Transition :css="false" @enter="transitionEnter" @leave="transitionLeave">
          <!-- ── Appearance ── -->
          <section v-if="activeSection === 'appearance'" key="appearance" data-test="settings-section-appearance">
            <h3 class="settings-section-title">外观设置 <span class="settings-control-secondary">Appearance</span></h3>
            <p class="settings-hint">选择皮肤、光感、强调色、列表密度和歌词对齐方式。</p>

            <div class="settings-control-group" role="group" aria-labelledby="settings-skin-label" data-test="settings-skin-group">
              <span id="settings-skin-label" class="settings-field-label">
                皮肤 <span class="settings-control-secondary">Skin</span>
              </span>
              <div class="settings-row">
                <SkinButton
                  data-test="select-skin-aurora"
                  :aria-pressed="appearanceStore.skin.value === 'aurora'"
                  :variant="appearanceStore.skin.value === 'aurora' ? 'primary' : 'secondary'"
                  size="md"
                  @click="selectSkin('aurora')"
                ><span>极光</span><span class="settings-control-secondary">Aurora</span></SkinButton>
                <SkinButton
                  data-test="select-skin-newsprint"
                  :aria-pressed="appearanceStore.skin.value === 'newsprint'"
                  :variant="appearanceStore.skin.value === 'newsprint' ? 'primary' : 'secondary'"
                  size="md"
                  @click="selectSkin('newsprint')"
                ><span>报刊</span><span class="settings-control-secondary">Newsprint</span></SkinButton>
              </div>
            </div>

            <div class="settings-control-group" role="group" aria-labelledby="settings-mode-label" data-test="settings-mode-group">
              <span id="settings-mode-label" class="settings-field-label">
                光感 <span class="settings-control-secondary">Mode</span>
              </span>
              <div class="settings-row">
                <SkinButton
                  data-test="select-mode-light"
                  :aria-pressed="appearanceStore.mode.value === 'light'"
                  :variant="appearanceStore.mode.value === 'light' ? 'primary' : 'secondary'"
                  size="md"
                  @click="selectMode('light')"
                ><span>浅色</span><span class="settings-control-secondary">Light</span></SkinButton>
                <SkinButton
                  data-test="select-mode-dark"
                  :aria-pressed="appearanceStore.mode.value === 'dark'"
                  :variant="appearanceStore.mode.value === 'dark' ? 'primary' : 'secondary'"
                  size="md"
                  @click="selectMode('dark')"
                ><span>深色</span><span class="settings-control-secondary">Dark</span></SkinButton>
              </div>
            </div>

            <div class="settings-field" data-test="settings-appearance-accent">
              <label class="settings-field-label" for="settings-accent">强调色 <span class="settings-control-secondary">Accent</span></label>
              <input
                id="settings-accent"
                class="settings-input settings-color-input"
                data-test="settings-accent-input"
                type="color"
                :value="appearanceStore.accent.value"
                @input="selectAccent"
              />
            </div>

            <label class="settings-field settings-toggle" data-test="settings-appearance-compact-list">
              <span class="settings-field-label">紧凑列表 <span class="settings-control-secondary">Compact List</span></span>
              <input
                data-test="settings-compact-list"
                type="checkbox"
                :checked="appearanceStore.compactList.value"
                @change="selectCompactList"
              />
            </label>

            <section class="settings-row" data-test="settings-appearance-lyric-align">
              <div
                role="group"
                class="settings-segment"
                aria-labelledby="settings-lyric-align-label"
              >
                <span id="settings-lyric-align-label" class="settings-field-label">歌词对齐 <span class="settings-control-secondary">Lyric Alignment</span></span>
                <div class="settings-segment-buttons">
                  <button
                    type="button"
                    data-test="settings-lyric-align-left"
                    :aria-pressed="appearanceStore.lyricAlign.value === 'left'"
                    :data-active="appearanceStore.lyricAlign.value === 'left'"
                    @click="selectLyricAlign('left')"
                  >
                    <span>左对齐</span><span class="settings-control-secondary">Left</span>
                  </button>
                  <button
                    type="button"
                    data-test="settings-lyric-align-center"
                    :aria-pressed="appearanceStore.lyricAlign.value === 'center'"
                    :data-active="appearanceStore.lyricAlign.value === 'center'"
                    @click="selectLyricAlign('center')"
                  >
                    <span>居中</span><span class="settings-control-secondary">Center</span>
                  </button>
                </div>
              </div>
            </section>
          </section>

          <!-- ── Device Fingerprint ── -->
          <section v-else-if="activeSection === 'device'" key="device" data-test="settings-section-device">
            <h3 class="settings-section-title">
              设备指纹 · Device Fingerprint
              <span v-if="device?.registered" class="settings-badge">已注册</span>
            </h3>

            <p class="settings-hint">
              本地初始 dfid 为 <code>-</code>。只有 <code>/risk/v2/r_register_dev</code> 返回并持久化的 dfid 才视为 registered。登录后 App 会通过 <code>/register/dev</code> 完成这次注册（dfid / mid / uuid），正常播放与领取<strong>无需手动设置</strong>。
              <br>
              下面三个框是<strong>诊断用手工覆盖</strong>：填入从酷狗官方 App / 网页抓到的指纹后，后续请求会改用这些值，但<strong>不保证上游接受</strong>，也不会把手工 dfid 自动标记为 registered。（格式：dfid 24 位 base64，mid 约 32–40 位 hex，uuid 32 位 hex / GUID）
              <br>
              <strong>怎么获取</strong>：浏览器打开
              <button
                type="button"
                class="settings-inline-link"
                data-test="open-device-help"
                @click="openDeviceHelp"
              >m.kugou.com</button>
              → F12 → Network → 找任意请求里的 query 字符串 → 复制 <code>dfid=</code><code>mid=</code><code>uuid=</code> 三个字段。
            </p>

            <!-- Use monospace font for these three inputs because dfid contains
                 visually-ambiguous chars (I vs l, O vs 0, 1 vs l). A previous user
                 typo I→l broke the dfid registration silently — KuGou returned
                 errcode 20028 and the user had no way to see what went wrong. -->
            <div class="settings-field">
              <label class="settings-field-label">
                <span>dfid（24 字符 base64-like，如 <code class="settings-mono">2ULHpc3qaLZa43ln8x0fLJQp</code>）</span>
                <span class="settings-mono">{{ dfidInput.length }} 字符</span>
              </label>
              <input v-model="dfidInput" type="text" placeholder="-" spellcheck="false" autocorrect="off" autocapitalize="off" class="settings-input" />
            </div>
            <div class="settings-field">
              <label class="settings-field-label">
                <span>mid（hex 设备串，约 32–40 字符）</span>
                <span class="settings-mono">{{ midInput.length }} 字符</span>
              </label>
              <input v-model="midInput" type="text" placeholder="0" spellcheck="false" autocorrect="off" autocapitalize="off" class="settings-input" />
            </div>
            <div class="settings-field">
              <label class="settings-field-label">
                <span>uuid（32 字符 hex / GUID）</span>
                <span class="settings-mono">{{ uuidInput.length }} 字符</span>
              </label>
              <input v-model="uuidInput" type="text" placeholder="-" spellcheck="false" autocorrect="off" autocapitalize="off" class="settings-input" />
            </div>

            <div class="settings-row">
              <SkinButton variant="primary" size="md" @click="saveDevice">保存指纹</SkinButton>
              <SkinButton variant="secondary" size="md" @click="testDevice">测试连接</SkinButton>
              <SkinButton variant="ghost" size="md" @click="resetDevice">清除设备指纹</SkinButton>
              <span v-if="deviceStatus" class="settings-status">{{ deviceStatus }}</span>
            </div>
          </section>

          <!-- ── VIP Daily Rewards ── -->
          <section v-else-if="activeSection === 'vip'" key="vip" data-test="settings-section-vip">
            <h3 class="settings-section-title">每日福利 · VIP Rewards</h3>
            <p class="settings-hint">
              通过酷狗概念版「听歌领 VIP / 看广告领 VIP」端点领取每日免费 VIP。每日限领一次，已领取会返回 130012（正常业务限制，非错误）。领取成功后会员到期时间会从 /user/vip/detail 刷新。
            </p>
            <div class="settings-row">
              <SkinButton variant="primary" size="md" :disabled="listenVipLoading || adVipLoading" @click="claimListenVip">
                {{ listenVipLoading ? '领取中…' : '听歌领 VIP' }}
              </SkinButton>
              <span v-if="listenVipMsg" class="settings-status">{{ listenVipMsg }}</span>
            </div>
            <div class="settings-row">
              <SkinButton variant="primary" size="md" :disabled="adVipLoading || listenVipLoading" @click="claimAdVip">
                {{ adVipLoading ? '领取中…' : '看广告领 VIP' }}
              </SkinButton>
              <span v-if="adVipMsg" class="settings-status">{{ adVipMsg }}</span>
            </div>
          </section>

          <!-- ── Auto Update ── -->
          <section v-else-if="activeSection === 'update'" key="update" data-test="settings-section-update">
            <h3 class="settings-section-title">版本更新 · Update</h3>
            <p class="settings-hint">
              从 GitHub Releases 拉取最新版本。发现新版本后可一键下载安装，重启应用即可生效。
            </p>
            <div class="settings-row">
              <SkinButton variant="primary" size="md" :disabled="updateLoading || updateDownloading" @click="checkForUpdate">
                {{ updateLoading ? '检查中…' : '检查更新' }}
              </SkinButton>
              <SkinButton
                v-if="updateVersion"
                variant="primary"
                size="md"
                :disabled="updateDownloading"
                @click="downloadAndInstall"
              >
                {{ updateDownloading ? `下载中 ${updateProgress}%` : `下载并安装 v${updateVersion}` }}
              </SkinButton>
              <SkinButton
                v-if="updateVersion && !updateDownloading"
                variant="ghost"
                size="md"
                @click="skipVersion"
              >
                跳过此版本
              </SkinButton>
              <span v-if="updateStatus" class="settings-status">{{ updateStatus }}</span>
              <div v-if="updateBody" class="settings-changelog">{{ updateBody }}</div>
            </div>
          </section>

          <!-- ── Storage & Cache ── -->
          <section v-else-if="activeSection === 'storage'" key="storage" data-test="settings-section-storage">
            <h3 class="settings-section-title">存储与缓存 · Storage</h3>
            <p class="settings-hint">
              项目当前把缓存记录在 SQLite3 中。图片解码走 WIC 缓存通道，内存 LRU 自动在 16MB 满额时启动淘汰。
            </p>
          </section>

          <!-- ── Diagnostics (native memory + frontend playback merged) ── -->
          <section v-else key="diagnostics" data-test="settings-section-diagnostics">
            <h3 class="settings-section-title">诊断 · Diagnostics</h3>

            <!-- Sub-panel 1: native C++ memory -->
            <div class="settings-subpanel">
              <div class="settings-subpanel-head">
                <h4 class="settings-subpanel-title">后端内核自检 (EchoCAPI.dll · FFI)</h4>
                <SkinButton variant="ghost" size="sm" @click="loadDiagnostics">手动刷新 ↻</SkinButton>
              </div>
              <div v-if="loading && !memoryInfo" class="spinner">
                正在拉取内存快照…
              </div>
              <div v-else-if="memoryInfo">
                <ul class="status-list">
                  <li>
                    <span class="label">Working Set (工作物理集)</span>
                    <span class="value">{{ formatBytes(memoryInfo.working_set_bytes) }}</span>
                  </li>
                  <li>
                    <span class="label">Private committed (专用虚拟集)</span>
                    <span class="value">{{ formatBytes(memoryInfo.private_bytes) }}</span>
                  </li>
                  <li>
                    <span class="label">Image Cache LRU (图片解码缓存)</span>
                    <span class="value">{{ formatBytes(memoryInfo.image_cache_bytes) }}</span>
                  </li>
                  <li>
                    <span class="label">C++ Async Threads (就绪异步任务)</span>
                    <span class="value">{{ memoryInfo.pending_task_count }} 项</span>
                  </li>
                  <li>
                    <span class="label">Native Playback State</span>
                    <span class="value">{{ memoryInfo.playback_state }}</span>
                  </li>
                </ul>
                <div class="settings-preformatted">{{ memoryInfo.text }}</div>
              </div>
              <SkinEmptyState v-else message="无法连通 C++ Diagnostics 诊断端子" />
            </div>

            <!-- Sub-panel 2: frontend playback diagnostics -->
            <div class="settings-subpanel" data-test="playback-diagnostics">
              <div class="settings-subpanel-head">
                <h4 class="settings-subpanel-title">播放边界事件 ({{ diagEvents.length }})</h4>
                <div class="settings-row">
                  <SkinButton variant="ghost" size="sm" @click="refreshDiag">刷新 ↻</SkinButton>
                  <SkinButton variant="ghost" size="sm" data-test="copy-diagnostics" @click="copyDiag">复制</SkinButton>
                </div>
              </div>
              <SkinEmptyState v-if="diagEvents.length === 0" message="暂无诊断事件" />
              <div v-else class="settings-diag-list">
                <div
                  v-for="(e, idx) in diagEvents"
                  :key="idx"
                  class="diag-row"
                  :class="{ 'diag-stall': e.kind === 'potential_stall' }"
                >
                  <span class="diag-ts">{{ e.ts }}</span>
                  <span class="diag-kind">{{ e.kind }}</span>
                  <span class="diag-phase">{{ e.phase }}</span>
                  <span class="diag-detail">{{ e.detail }}</span>
                  <span v-if="e.trackKey" class="diag-track">[{{ e.trackKey }}]</span>
                </div>
              </div>
            </div>
          </section>
        </Transition>
      </div>
    </div>
  </div>
</template>

<style scoped>
.settings-header-meta {
  font-size: 13px;
  color: var(--text-muted);
}
.settings-control-secondary {
  display: block;
  font-size: 0.72em;
  font-weight: 500;
  letter-spacing: 0.03em;
  opacity: 0.72;
}
.settings-inline-link {
  border: 0;
  border-bottom: 1px solid currentColor;
  background: transparent;
  color: var(--accent);
  cursor: pointer;
  font: inherit;
  padding: 0;
}
.settings-inline-link:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}
.settings-section-title > .settings-control-secondary {
  display: inline;
  margin-left: 4px;
}
.settings-control-group {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.diag-stall {
  background: rgba(220, 50, 47, 0.08) !important;
  border-left: 3px solid #dc322f !important;
}
.settings-segment {
  display: flex;
  flex-direction: column;
  gap: 8px;
  width: 100%;
}
.settings-segment .settings-field-label {
  display: block;
  font-weight: 600;
  color: var(--text-primary, var(--ink));
}
.settings-segment .settings-hint {
  margin: 0;
}
.settings-segment-buttons {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.settings-segment-buttons button {
  padding: 8px 14px;
  border-radius: 999px;
  border: 1px solid var(--rule, var(--border, #ccc));
  background: transparent;
  color: var(--text-secondary, var(--ink-soft));
  font-size: 13px;
  font-family: inherit;
  cursor: pointer;
  transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
}
.settings-segment-buttons button:hover {
  border-color: var(--accent);
  color: var(--text-primary, var(--ink));
}
.settings-segment-buttons button[aria-pressed='true'],
.settings-segment-buttons button[data-active='true'] {
  background: color-mix(in srgb, var(--accent) 16%, transparent);
  border-color: var(--accent);
  color: var(--accent);
  font-weight: 600;
}
.sr-only {
  position: absolute;
  width: 1px;
  height: 1px;
  padding: 0;
  margin: -1px;
  overflow: hidden;
  clip: rect(0, 0, 0, 0);
  white-space: nowrap;
  border: 0;
}
</style>
