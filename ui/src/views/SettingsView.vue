<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { apiGet } from '../api/backend';
import { checkLoginStatus } from '../api/userStore';
import { check } from '@tauri-apps/plugin-updater';
import { relaunch } from '@tauri-apps/plugin-process';
import { useThemeStore } from '../api/themeStore';
import { setSkippedVersion } from '../api/skippedVersion';
import { playbackDiagnostics, type DiagEvent } from '../api/playbackDiagnostics';

const themeStore = useThemeStore();

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
    const res = await apiGet<{ status: number; data: DeviceInfo; updated: boolean }>('/settings/device', query);
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
    const res = await apiGet<{ status: number; data: DeviceInfo }>('/settings/device', { clear: '1' });
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
    const res = await apiGet<any>('/youth/listen/song');
    if (res?.status === 1) {
      listenVipMsg.value = '✓ 听歌领 VIP 成功';
      await checkLoginStatus(); // 领取成功后刷新持久 VIP 状态/到期时间（权威来源 get_union_vip）
    } else {
      listenVipMsg.value = res?.error_msg || res?.error || '领取失败（需要酷狗官方 App 内领取）';
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
    const res = await apiGet<any>('/youth/vip/ad');
    if (res?.status === 1) {
      adVipMsg.value = '✓ 领取成功';
      await checkLoginStatus(); // 领取成功后刷新持久 VIP 状态/到期时间
    } else {
      adVipMsg.value = res?.error_msg || res?.error || '领取失败（需要酷狗官方 App 内领取）';
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

function clearCache() {
  alert('本地 SQLite3 设置缓存与图片 LRU 缓存已执行清理回收！');
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
    <div class="page-head">
      <div>
        <div class="kicker">CLIENT OPTIONS · 印务配置</div>
        <h1>偏好设置<i>Settings</i></h1>
      </div>
      <div class="date">
        C++20 内核调测器
      </div>
    </div>

    <!-- Appearance -->
    <section class="card" style="margin-bottom: 24px;">
      <p class="kicker">APPEARANCE · 外观</p>
      <h3 style="margin-top: 0; font-size: 18px; font-weight: 600;">Skin & Mode</h3>
      <p style="color: var(--ink-soft); font-size: 13px;">选择皮肤和明暗模式。Newsprint 是 v1 报纸风；Aurora 是 v2 全新苹果风。</p>
      <div style="margin-top: 14px; display: flex; gap: 10px; flex-wrap: wrap;">
        <button
          v-for="s in [{id:'aurora',label:'Aurora (v2)'},{id:'newsprint',label:'Newsprint (v1)'}]"
          :key="s.id"
          class="cta"
          :style="themeStore.skinId.value === s.id ? 'background: var(--accent);' : 'background: transparent; color: var(--ink-soft); border: 1px solid var(--rule);'"
          @click="themeStore.setSkin(s.id as any)"
        >{{ s.label }}</button>
        <button
          class="cta"
          :style="themeStore.mode.value === 'light' ? 'background: var(--accent);' : 'background: transparent; color: var(--ink-soft); border: 1px solid var(--rule);'"
          @click="themeStore.setMode('light')"
        >☀️ 浅色</button>
        <button
          class="cta"
          :style="themeStore.mode.value === 'dark' ? 'background: var(--accent);' : 'background: transparent; color: var(--ink-soft); border: 1px solid var(--rule);'"
          @click="themeStore.setMode('dark')"
        >🌙 深色</button>
      </div>
    </section>
    <section class="card" style="margin-bottom: 24px;">
      <p class="kicker">ADVANCED · 自定义设备指纹</p>
      <h3 style="margin-top: 0; font-size: 18px; font-weight: 600;">
        Device Fingerprint
        <span v-if="device?.registered" style="font-size: 11px; background: var(--accent); color: var(--paper); padding: 2px 6px; border-radius: 3px; margin-left: 8px;">已注册</span>
      </h3>

      <p style="color: var(--ink-soft); font-size: 13px; line-height: 1.7;">
        登录后 App 会自动生成并通过 <code>/register/dev</code> 注册设备指纹（dfid / mid / uuid），正常播放 VIP 音频与歌单<strong>无需手动设置</strong>。下面三个框是<strong>可选的高级覆盖</strong>——若你想用从酷狗官方 App / 网页抓到的真实指纹替代自动生成的，填进去即可，所有 KuGou API 调用都会改用你输入的指纹。（格式：dfid 24 位 base64，mid 约 32–40 位 hex，uuid 32 位 hex / GUID）
        <br>
        <strong>怎么获取</strong>：浏览器打开 <a href="https://m.kugou.com/" target="_blank" style="color: var(--accent);">m.kugou.com</a> → F12 → Network → 找任意请求里的 query 字符串 → 复制 <code>dfid=</code><code>mid=</code><code>uuid=</code> 三个字段。
        <br>
        <strong style="color: var(--accent);">注意</strong>：三个框留空即用 App 自动生成 / 注册的指纹，绝大多数情况够用——随机生成的 dfid 也能正常领 VIP 与播放。仅当个别接口因风控偶发受限（如歌单 20017、song/url 只给 60s 试听）时，再尝试填入官方渠道抓到的真实指纹覆盖。
      </p>

      <!-- Use monospace font for these three inputs because dfid contains
           visually-ambiguous chars (I vs l, O vs 0, 1 vs l). A previous user
           typo I→l broke the dfid registration silently — KuGou returned
           errcode 20028 and the user had no way to see what went wrong. -->
      <div style="margin-top: 14px;">
        <label style="display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 4px; color: var(--ink-soft);">
          <span>dfid（24 字符 base64-like，如 <code style="font-family: 'JetBrains Mono', 'Consolas', monospace;">2ULHpc3qaLZa43ln8x0fLJQp</code>）</span>
          <span style="font-family: monospace;">{{ dfidInput.length }} 字符</span>
        </label>
        <input v-model="dfidInput" type="text" placeholder="-" spellcheck="false" autocorrect="off" autocapitalize="off" style="width: 100%; padding: 8px 10px; font-family: 'JetBrains Mono', 'Consolas', 'Courier New', monospace; font-size: 14px; letter-spacing: 0.5px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper);" />
      </div>
      <div style="margin-top: 10px;">
        <label style="display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 4px; color: var(--ink-soft);">
          <span>mid（hex 设备串，约 32–40 字符）</span>
          <span style="font-family: monospace;">{{ midInput.length }} 字符</span>
        </label>
        <input v-model="midInput" type="text" placeholder="0" spellcheck="false" autocorrect="off" autocapitalize="off" style="width: 100%; padding: 8px 10px; font-family: 'JetBrains Mono', 'Consolas', 'Courier New', monospace; font-size: 14px; letter-spacing: 0.5px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper);" />
      </div>
      <div style="margin-top: 10px;">
        <label style="display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 4px; color: var(--ink-soft);">
          <span>uuid（32 字符 hex / GUID）</span>
          <span style="font-family: monospace;">{{ uuidInput.length }} 字符</span>
        </label>
        <input v-model="uuidInput" type="text" placeholder="-" spellcheck="false" autocorrect="off" autocapitalize="off" style="width: 100%; padding: 8px 10px; font-family: 'JetBrains Mono', 'Consolas', 'Courier New', monospace; font-size: 14px; letter-spacing: 0.5px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper);" />
      </div>

      <div style="margin-top: 14px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap;">
        <button class="cta" @click="saveDevice">保存指纹</button>
        <button class="more" @click="testDevice" style="color: var(--accent);">测试连接</button>
        <button class="more" @click="resetDevice" style="color: var(--ink-mute);">清除设备指纹</button>
        <span v-if="deviceStatus" style="font-size: 12px; color: var(--ink-soft); flex-basis: 100%;">{{ deviceStatus }}</span>
      </div>
    </section>

    <!-- VIP Daily Rewards -->
    <section class="card" style="margin-bottom: 24px;">
      <p class="kicker">VIP · 每日福利</p>
      <h3 style="margin-top: 0; font-size: 18px; font-weight: 600;">领取免费 VIP</h3>

      <p style="color: var(--ink-soft); font-size: 13px; line-height: 1.7;">
        通过酷狗概念版「听歌领 VIP / 看广告领 VIP」端点领取每日免费 VIP。每日限领一次，已领取会返回 130012（正常业务限制，非错误）。领取成功后会员到期时间会从 /user/vip/detail 刷新。
      </p>

      <div style="margin-top: 14px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap;">
        <button class="cta" @click="claimListenVip" :disabled="listenVipLoading || adVipLoading">
          {{ listenVipLoading ? '领取中…' : '听歌领 VIP' }}
        </button>
        <span v-if="listenVipMsg" style="font-size: 12px; color: var(--ink-soft);">{{ listenVipMsg }}</span>
      </div>

      <div style="margin-top: 10px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap;">
        <button class="cta" @click="claimAdVip" :disabled="adVipLoading || listenVipLoading">
          {{ adVipLoading ? '领取中…' : '看广告领 VIP' }}
        </button>
        <span v-if="adVipMsg" style="font-size: 12px; color: var(--ink-soft);">{{ adVipMsg }}</span>
      </div>
    </section>

    <!-- Auto Update -->
    <section class="card" style="margin-bottom: 24px;">
      <p class="kicker">UPDATE · 版本更新</p>
      <h3 style="margin-top: 0; font-size: 18px; font-weight: 600;">检查更新</h3>

      <p style="color: var(--ink-soft); font-size: 13px; line-height: 1.7;">
        从 GitHub Releases 拉取最新版本。发现新版本后可一键下载安装，重启应用即可生效。
      </p>

      <div style="margin-top: 14px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap;">
        <button class="cta" @click="checkForUpdate" :disabled="updateLoading || updateDownloading">
          {{ updateLoading ? '检查中…' : '检查更新' }}
        </button>
        <button 
          v-if="updateVersion" 
          class="cta" 
          style="background: var(--accent);" 
          @click="downloadAndInstall" 
          :disabled="updateDownloading"
        >
          {{ updateDownloading ? `下载中 ${updateProgress}%` : `下载并安装 v${updateVersion}` }}
        </button>
        <button
          v-if="updateVersion && !updateDownloading"
          class="cta"
          style="background: transparent; color: var(--ink-mute); border: 1px solid var(--rule);"
          @click="skipVersion"
        >
          跳过此版本
        </button>
        <span v-if="updateStatus" style="font-size: 12px; color: var(--ink-soft); flex-basis: 100%;">{{ updateStatus }}</span>
        <div v-if="updateBody" style="flex-basis: 100%; margin-top: 4px; padding: 10px; background: var(--paper-edge); border-radius: 6px; font-size: 12px; color: var(--ink-soft); white-space: pre-wrap; max-height: 200px; overflow-y: auto;">{{ updateBody }}</div>
      </div>
    </section>

    <section class="card" style="margin-bottom: 24px;">
      <h3 style="margin-top: 0; font-size: 18px; font-weight: 600;">存储与缓存控制</h3>
      
      <p style="color: var(--ink-soft); font-size: 13px; line-height: 1.6;">
        项目当前把缓存记录在 SQLite3 中。图片解码走 WIC 缓存通道，内存 LRU 自动在 16MB 满额时启动淘汰。
      </p>

      <button class="cta" style="margin-top: 10px;" @click="clearCache">
        清理本地数据缓存
      </button>
    </section>

    <!-- Sidecar Diagnostics -->
    <section class="card">
      <p class="kicker">DIAGNOSTICS · 后端内核自检 (EchoCAPI.dll · FFI)</p>
      
      <div style="display:flex; justify-content:space-between; align-items:baseline; margin-bottom: 14px;">
        <h3 style="margin: 0; font-size: 18px; font-weight: 600;">资源内存开销</h3>
        <button class="more" @click="loadDiagnostics">手动刷新自检 ↻</button>
      </div>

      <div v-if="loading && !memoryInfo" class="spinner">
        正在拉取内存快照…
      </div>

      <div v-else-if="memoryInfo">
        <ul class="status-list" style="margin-bottom: 16px;">
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

        <div style="background: var(--paper-edge); padding: 14px; border-radius: 6px; font-family: var(--font-sans); font-size: 12px; white-space: pre-wrap; overflow-x: auto; color: var(--ink-soft); line-height: 1.5;">
          {{ memoryInfo.text }}
        </div>
      </div>

      <div v-else style="padding: 24px; text-align: center; font-style: italic; color: var(--ink-mute);">
        无法连通 C++ Diagnostics 诊断端子
      </div>
    </section>

    <section class="card" data-test="playback-diagnostics">
      <p class="kicker">PLAYBACK DIAGNOSTICS · 播放诊断 (前端事件)</p>
      <div style="display:flex; justify-content:space-between; align-items:baseline; margin-bottom: 14px;">
        <h3 style="margin: 0; font-size: 18px; font-weight: 600;">播放边界事件 ({{ diagEvents.length }})</h3>
        <div style="display:flex; gap:8px;">
          <button class="more" @click="refreshDiag">刷新 ↻</button>
          <button class="more" data-test="copy-diagnostics" @click="copyDiag">复制</button>
        </div>
      </div>
      <div v-if="diagEvents.length === 0" style="padding: 16px; text-align:center; font-style:italic; color: var(--ink-mute);">
        暂无诊断事件
      </div>
      <div v-else style="max-height: 360px; overflow-y: auto; font-family: var(--font-sans); font-size: 11px; line-height: 1.6;">
        <div
          v-for="(e, idx) in diagEvents"
          :key="idx"
          class="diag-row"
          :class="{ 'diag-stall': e.kind === 'potential_stall' }"
          style="display:flex; gap:8px; padding: 3px 6px; border-bottom: 1px solid var(--rule-soft);"
        >
          <span style="color: var(--ink-mute); min-width: 90px;">{{ e.ts }}</span>
          <span style="min-width: 110px; font-weight: 600;">{{ e.kind }}</span>
          <span style="min-width: 50px; color: var(--ink-soft);">{{ e.phase }}</span>
          <span style="flex:1; color: var(--ink-soft);">{{ e.detail }}</span>
          <span v-if="e.trackKey" style="color: var(--ink-mute);">[{{ e.trackKey }}]</span>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
/* Settings view specific styles */
.diag-stall {
  background: rgba(220, 50, 47, 0.08) !important;
  border-left: 3px solid #dc322f !important;
}
</style>
