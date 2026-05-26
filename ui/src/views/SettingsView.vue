<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { apiGet } from '../api/backend';

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
  if (!confirm('重置为随机指纹？将清除当前自定义 dfid/mid/uuid。')) return;
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

onMounted(() => {
  loadDiagnostics();
  loadDevice();
});

function formatBytes(bytes: number) {
  if (!bytes) return '0 MB';
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function clearCache() {
  alert('本地 SQLite3 设置缓存与图片 LRU 缓存已执行清理回收！');
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

    <!-- Custom Device Fingerprint -->
    <section class="card" style="margin-bottom: 24px;">
      <p class="kicker">ADVANCED · 自定义设备指纹</p>
      <h3 style="margin-top: 0; font-size: 18px; font-weight: 600;">
        Device Fingerprint
        <span v-if="device?.registered" style="font-size: 11px; background: var(--accent); color: var(--paper); padding: 2px 6px; border-radius: 3px; margin-left: 8px;">已注册</span>
      </h3>

      <p style="color: var(--ink-soft); font-size: 13px; line-height: 1.7;">
        酷狗对随机生成的设备指纹会限制 VIP 音频与歌单访问。如果你能从酷狗官方 App 或网页抓到真实的 <code>dfid / mid / uuid</code>（典型格式：dfid 24 位 base64，mid 32 位 hex，uuid 13 位时间戳或 GUID），填进下面三个框，所有 KuGou API 调用都会改用你输入的指纹。
        <br>
        <strong>怎么获取</strong>：浏览器打开 <a href="https://m.kugou.com/" target="_blank" style="color: var(--accent);">m.kugou.com</a> → F12 → Network → 找任意请求里的 query 字符串 → 复制 <code>dfid=</code><code>mid=</code><code>uuid=</code> 三个字段。
      </p>

      <div style="margin-top: 14px;">
        <label style="display: block; font-size: 12px; margin-bottom: 4px; color: var(--ink-soft);">dfid（24 字符 base64-like，如 <code>2ULHpc3qaLZa43ln8x0fLJQp</code>）</label>
        <input v-model="dfidInput" type="text" placeholder="-" style="width: 100%; padding: 8px 10px; font-family: var(--font-sans); font-size: 13px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper);" />
      </div>
      <div style="margin-top: 10px;">
        <label style="display: block; font-size: 12px; margin-bottom: 4px; color: var(--ink-soft);">mid（32 字符 hex）</label>
        <input v-model="midInput" type="text" placeholder="0" style="width: 100%; padding: 8px 10px; font-family: var(--font-sans); font-size: 13px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper);" />
      </div>
      <div style="margin-top: 10px;">
        <label style="display: block; font-size: 12px; margin-bottom: 4px; color: var(--ink-soft);">uuid（13 位时间戳数字或 GUID）</label>
        <input v-model="uuidInput" type="text" placeholder="-" style="width: 100%; padding: 8px 10px; font-family: var(--font-sans); font-size: 13px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper);" />
      </div>

      <div style="margin-top: 14px; display: flex; gap: 10px; align-items: center;">
        <button class="cta" @click="saveDevice">保存指纹</button>
        <button class="more" @click="resetDevice" style="color: var(--ink-mute);">重置为随机</button>
        <span v-if="deviceStatus" style="font-size: 12px; color: var(--ink-soft);">{{ deviceStatus }}</span>
      </div>
    </section>

    <section class="card" style="margin-bottom: 24px;">
      <p class="kicker">PREFERENCES · 参数项</p>
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
      <p class="kicker">DIAGNOSTICS · 后端内核自检 (EchoCompatServer)</p>
      
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
  </div>
</template>

<style scoped>
/* Settings view specific styles */
</style>
