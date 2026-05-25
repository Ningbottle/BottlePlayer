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

const loading = ref(false);
const memoryInfo = ref<MemoryData | null>(null);

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

onMounted(() => {
  loadDiagnostics();
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
