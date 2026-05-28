---
name: tauri-ui-boundary
description: Tauri 2 + Vue 3 前端与 C++ 后端的交互边界规范。确保 UI 线程不执行阻塞操作，业务请求统一走 backend.ts。
---

## 何时使用
- 新增或修改前端 Vue 组件的业务请求逻辑
- 新增 Tauri Command（Rust/C++ 暴露给前端的接口）
- 调整前端状态管理与后端数据流

## 核心原则

### 1. 请求归口原则
**所有**酷狗业务请求必须通过 `ui/src/api/backend.ts`，组件禁止直接：
- 调用 `fetch`/`axios` 访问酷狗域名
- 拼接酷狗 URL 参数
- 构造签名/加密逻辑

### 2. UI 线程禁止清单
前端（Vue 组件、TS 逻辑）不得执行：
- [ ] 网络请求（HTTP/WebSocket）
- [ ] SQLite/IndexedDB 的大量读写
- [ ] 图片解码（大封面、批量加载）
- [ ] 音频解码/播放控制底层调用

> 这些操作必须由 Tauri Command 转发到 C++ 后端线程或 Web Worker。

### 3. Command 设计规范
新增 Tauri Command 时检查：
- [ ] Command 名称使用 `snake_case`，前缀表明模块：`get_song_url`、`search_songs`
- [ ] 返回值是 JSON 可序列化的 DTO，不是原始字符串
- [ ] 错误返回统一结构：`{ success: false, error: string, code?: number }`
- [ ] 长时间操作返回 `Promise` + 进度回调，或支持取消信号

### 4. 状态单向流
```
Vue Component → backend.ts → Tauri Command → EchoCore Service
     ↑                                    ↓
     └────── reactive DTO ←─────────────┘
```
- 前端不直接修改后端返回的原始数据
- 播放状态通过事件推送（Tauri Event），不是轮询

### 5. 样式约束
- 视觉方向：Newsprint 报纸风
- 纸色：`#f1ead8`
- 红强调：`#a8311b`
- 不使用通用 AI 美学（避免渐变、玻璃拟态）

## 常见错误
- 在 `onMounted` 里直接 `fetch('https://gateway.kugou.com/...')` ❌
- 把 `dfid`/`mid`/`token` 泄漏到前端代码 ❌
- 在 Vue 组件里做 `JSON.parse` 大量酷狗响应 ❌

## 审查输出
- 指出直接访问酷狗接口的代码位置
- 指出 UI 线程阻塞风险点
- 给出归口到 `backend.ts` 的修改建议
