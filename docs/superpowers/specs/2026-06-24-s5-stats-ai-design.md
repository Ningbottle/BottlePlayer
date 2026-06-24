# S5 — Statistics Dashboard + AI Analysis Design

**Date**: 2026-06-24
**Status**: Ready for implementation plan
**Depends on**: S4 (play events from playerStore)

## 1. Goal

Bring the dead `play_history` SQLite table to life with a rich statistics dashboard. Record every play event locally (song name, artist, album, cover art, duration, completion, timestamp), display detailed stats with album art, and provide AI-powered listening analysis via DeepSeek API.

## 2. Key Design Decisions

| # | Decision | Rationale |
|---|---|---|
| Dashboard location | New independent Stats view in sidebar | Don't touch existing HistoryView |
| Recommendation source | Pure local (no KuGou API) | Simpler, no network dependency |
| Stats data layer | C++ PlayStatsService + SQLite | Consistent with existing FFI architecture |
| AI analysis | DeepSeek v4 flash, user-provided API key | User requested, model is cheap and fast |
| Visual design | Match current skin (Newsprint/Aurora) | Use CSS variables, no hardcoded colors |
| Cover art | Store cover URL in play_history | Display in stats list, no binary storage |

## 3. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  UI Layer (Vue 3)                                          │
│                                                             │
│  Sidebar.vue — new "统计" nav item                         │
│       │                                                     │
│       ▼                                                     │
│  StatsView.vue                                             │
│  ├─ 时间范围切换 (7天 / 30天 / 全部)                       │
│  ├─ 概览卡片 (总播放次数, 总时长, 唯一歌曲数, 完成率)     │
│  ├─ Top 歌曲 / 歌手 / 专辑 (带封面, 滚动列表)             │
│  ├─ 播放时间线 (柱状图, 按天)                              │
│  ├─ 最近播放 (详细列表: 封面+歌名+歌手+时间)              │
│  ├─ "你可能喜欢" (基于最常听歌手)                          │
│  └─ AI 分析面板 (DeepSeek API)                             │
│       │                                                     │
│       ▼ invoke('stats_*') + invoke('ai_analyze')           │
└─────────────────────────────────────────────────────────────┘
        │ Tauri IPC
        ▼
┌─────────────────────────────────────────────────────────────┐
│  Rust FFI                                                  │
│  ├─ stats_record_play / stats_get_summary                  │
│  ├─ stats_get_top / stats_get_timeline / stats_get_recent  │
│  └─ ai_analyze (calls DeepSeek API via reqwest)            │
└─────────────────────────────────────────────────────────────┘
        │ extern "C" (stats only; AI is Rust-side)
        ▼
┌─────────────────────────────────────────────────────────────┐
│  C++ Core                                                  │
│  ├─ PlayStatsService — record + query play events          │
│  ├─ Database migration — play_history schema upgrade       │
│  └─ C API: EchoStatsRecordPlay / EchoStatsGet*             │
└─────────────────────────────────────────────────────────────┘
```

## 4. Data Layer

### 4.1 Schema Migration

Upgrade the dead `play_history` table:

```sql
-- Old schema: id, mix_song_id, played_at, progress_seconds
-- New schema:
CREATE TABLE play_history_v2 (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  song_hash TEXT NOT NULL,           -- for dedup and identity
  song_name TEXT NOT NULL,
  singer_name TEXT,
  album_id TEXT,
  album_name TEXT,
  cover_url TEXT,                    -- album art URL
  duration_seconds REAL NOT NULL DEFAULT 0,
  completed INTEGER NOT NULL DEFAULT 0,  -- 1 = played to end, 0 = skipped
  listened_seconds REAL NOT NULL DEFAULT 0,  -- actual listen time
  quality TEXT,                      -- '128', '320', 'flac', etc.
  played_at INTEGER NOT NULL         -- epoch milliseconds
);

CREATE INDEX idx_play_history_played_at ON play_history_v2(played_at DESC);
CREATE INDEX idx_play_history_song_hash ON play_history_v2(song_hash);
CREATE INDEX idx_play_history_singer ON play_history_v2(singer_name);
```

Migration: check `user_version` pragma. If old version, `ALTER TABLE play_history RENAME TO play_history_old` then `CREATE TABLE play_history_v2` then optionally migrate old data.

### 4.2 PlayStatsService (C++)

```cpp
// native/include/echo/stats/PlayStatsService.h
namespace echo::stats {

struct PlayRecord {
  std::string songHash;
  std::string songName;
  std::string singerName;
  std::string albumId;
  std::string albumName;
  std::string coverUrl;
  double durationSeconds;
  bool completed;
  double listenedSeconds;
  std::string quality;
  int64_t playedAtMs;
};

class PlayStatsService {
 public:
  explicit PlayStatsService(echo::storage::Database& db);

  // Record a play event
  bool RecordPlay(const PlayRecord& record);

  // Get summary stats for a time range
  // range: "7d", "30d", "all"
  std::string GetSummary(const std::string& range);

  // Get top items: dim = "song" | "artist" | "album"
  std::string GetTop(const std::string& dim, const std::string& range, int limit);

  // Get play timeline (daily counts for chart)
  std::string GetTimeline(const std::string& range);

  // Get recent plays with full detail
  std::string GetRecent(int limit, int offset);

  // Get "you might like" — top artists you listen to
  std::string GetRecommendations(int limit);

 private:
  echo::storage::Database& db_;
  int64_t RangeToTimestamp(const std::string& range);
};

}  // namespace echo::stats
```

### 4.3 JSON Output Format

**GetSummary**:
```json
{
  "total_plays": 342,
  "total_listened_seconds": 48230.5,
  "unique_songs": 87,
  "unique_artists": 23,
  "completion_rate": 0.73,
  "range": "30d"
}
```

**GetTop** (dim=song):
```json
{
  "dim": "song",
  "items": [
    {
      "song_hash": "abc123",
      "name": "歌曲名",
      "singer": "歌手名",
      "album": "专辑名",
      "cover_url": "http://imge.kugou.com/...",
      "play_count": 15,
      "total_listened_seconds": 3240.5
    }
  ]
}
```

**GetRecent**:
```json
{
  "items": [
    {
      "song_hash": "abc123",
      "name": "歌曲名",
      "singer": "歌手名",
      "album": "专辑名",
      "cover_url": "http://...",
      "duration_seconds": 240.5,
      "listened_seconds": 180.2,
      "completed": false,
      "quality": "320",
      "played_at": 1782289763760,
      "played_at_text": "2026-06-22 14:30"
    }
  ]
}
```

## 5. C API + Rust FFI

### 5.1 C API Exports

```cpp
ECHO_C_API void EchoStatsRecordPlay(const char* json_record);
ECHO_C_API const char* EchoStatsGetSummary(const char* range);
ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit);
ECHO_C_API const char* EchoStatsGetTimeline(const char* range);
ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset);
ECHO_C_API const char* EchoStatsGetRecommendations(int limit);
```

### 5.2 Rust FFI

6 Tauri commands:
```rust
#[tauri::command]
fn stats_record_play(record: String) -> Result<(), String>;

#[tauri::command]
fn stats_get_summary(range: String) -> Result<String, String>;

#[tauri::command]
fn stats_get_top(dim: String, range: String, limit: i32) -> Result<String, String>;

#[tauri::command]
fn stats_get_timeline(range: String) -> Result<String, String>;

#[tauri::command]
fn stats_get_recent(limit: i32, offset: i32) -> Result<String, String>;

#[tauri::command]
fn stats_get_recommendations(limit: i32) -> Result<String, String>;
```

### 5.3 AI Analysis (Rust-side, DeepSeek API)

AI analysis is handled entirely in Rust — no C++ involvement. The Rust code calls the DeepSeek API directly via `reqwest`.

```rust
#[tauri::command]
async fn ai_analyze(
    api_key: String,
    stats_summary: String,
    top_songs: String,
    top_artists: String,
    timeline: String,
) -> Result<String, String> {
    // Call DeepSeek API
    // Model: deepseek-v4-flash
    // Prompt: "Based on the following listening statistics, provide a brief analysis..."
    // Return: AI-generated text (listening habits, recommendations, patterns)
}
```

**DeepSeek API details**:
- Endpoint: `https://api.deepseek.com/v1/chat/completions`
- Model: `deepseek-v4-flash` (fast, cheap)
- API key: stored in localStorage on frontend, passed to Rust per call
- Prompt: structured with stats data, asking for listening pattern analysis

**Privacy**: API key is user-provided, stored in localStorage, never committed. Stats data sent to DeepSeek for analysis — user explicitly clicks "AI 分析" to trigger.

## 6. Frontend

### 6.1 Sidebar

Add new nav item:
```typescript
const sidebarNav = [
  { id: 'home', name: '首页', icon: '...' },
  { id: 'stats', name: '统计', icon: 'M3 3h18v18H3z M9 17V9 M15 17v-4' }, // chart icon
  { id: 'history', name: '最近播放', icon: '...' },
];
```

### 6.2 StatsView.vue

Layout (top to bottom):

1. **Header**: "我的统计" title + time range tabs (7天 / 30天 / 全部)
2. **Overview cards** (4 cards in a row):
   - 总播放次数
   - 总听歌时长 (格式化为 "Xh Ym")
   - 唯一歌曲数
   - 完成率 (百分比)
3. **Top section** (3 columns or tabs):
   - Top 歌曲 (封面 + 歌名 + 歌手 + 播放次数)
   - Top 歌手 (歌手名 + 播放次数)
   - Top 专辑 (封面 + 专辑名 + 歌手 + 播放次数)
4. **Timeline chart** (柱状图, 按天):
   - 使用 CSS 柱状图 (无 D3 依赖)
   - X 轴: 日期, Y 轴: 播放次数
5. **Recent plays** (详细列表):
   - 封面缩略图 (40x40)
   - 歌名 + 歌手
   - 播放时间 ("2小时前", "昨天", "2026-06-20")
   - 听了多久 / 是否听完
6. **AI 分析面板**:
   - "AI 分析" 按钮
   - API key 输入框 (密码类型, 存 localStorage)
   - 分析结果文本区域
   - 加载状态

### 6.3 Skin Integration

All styles use CSS variables:
- 背景: `var(--paper)`, `var(--paper-2)`
- 文字: `var(--ink)`, `var(--ink-soft)`, `var(--ink-mute)`
- 强调: `var(--accent)`
- 边框: `var(--rule)`, `var(--rule-soft)`
- 字体: `var(--font-serif)`, `var(--font-sans)`

卡片、列表、图表的样式跟 Drawer.vue / SettingsView.vue 保持一致。

### 6.4 playerStore 改动

```typescript
// 记录播放开始
function recordPlayStart(track: Track) {
  const record = {
    song_hash: track.FileHash,
    song_name: track.Name,
    singer_name: track.Singer,
    album_id: track.AlbumID || '',
    album_name: track.AlbumName || '',
    cover_url: track.Image || '',
    duration_seconds: track.Duration || 0,
    completed: false,
    listened_seconds: 0,
    quality: playerStore.quality,
    played_at: Date.now(),
  };
  currentPlayRecord_ = record;
  playStartTime_ = Date.now();
  invoke('stats_record_play', { record: JSON.stringify(record) }).catch(() => {});
}

// 记录播放结束 (completed 或 skip)
function recordPlayEnd(completed: boolean) {
  if (!currentPlayRecord_) return;
  const listened = (Date.now() - playStartTime_) / 1000;
  currentPlayRecord_.listened_seconds = Math.min(listened, currentPlayRecord_.duration_seconds);
  currentPlayRecord_.completed = completed;
  currentPlayRecord_.played_at = playStartTime_;
  invoke('stats_record_play', { record: JSON.stringify(currentPlayRecord_) }).catch(() => {});
  currentPlayRecord_ = null;
}
```

**调用时机**:
- `playTrack(track)` → `recordPlayStart(track)` (如果上一首没记录结束，先 `recordPlayEnd(false)`)
- `ended` 事件 → `recordPlayEnd(true)`
- `next()` / `prev()` / `playTrack(other)` → `recordPlayEnd(false)` 再 `recordPlayStart(newTrack)`

**注意**: 同一次播放会产生两条记录（start + end），或者只在结束时产生一条完整记录。后者更简洁。**方案：只在结束时记录一条**。`playTrack` 时存当前 track + startTime，`ended`/`next`/`prev` 时写入完整记录。

## 7. Migration Phases

### Phase 5.1 — C++ Data Layer
- Database schema migration
- PlayStatsService implementation
- C API exports
- CTest: write records, query summary/top/timeline

### Phase 5.2 — Rust FFI + Commands
- CApiHandle extension for 6 stats symbols
- 6 Tauri commands
- DeepSeek AI analysis command (reqwest)
- Cargo test: command integration

### Phase 5.3 — Frontend
- playerStore play event recording
- StatsView.vue with all sections
- Sidebar nav item
- Skin-consistent styling
- Vitest: mock invoke, verify rendering

### Phase 5.4 — Polish
- AI analysis prompt tuning
- Timeline chart visual polish
- Empty states (no data yet)
- Loading states

## 8. Test Strategy

### C++ (CTest)
- `PlayStatsService` contract test:
  - Write 10 play records
  - Verify summary (total_plays, unique_songs)
  - Verify top songs ordering
  - Verify timeline daily counts

### Rust (cargo test)
- Stats command integration test

### Frontend (Vitest)
- Mock `invoke('stats_*')` → verify StatsView renders correct cards
- Mock `invoke('ai_analyze')` → verify AI panel shows result
- Verify play event recording on playTrack/next/ended

## 9. Scope Discipline (YAGNI)

### In scope
- Local play event recording (SQLite)
- Stats dashboard (summary, top, timeline, recent)
- Album art display in stats
- DeepSeek AI analysis (user-provided API key)
- Skin-consistent styling
- "You might like" (based on top artists)

### Out of scope
- KuGou API fusion for recommendations
- Aggregation/precomputed tables
- Cross-device sync
- Export/import stats data
- Real-time spectrum visualizer
- D3.js dependency (use CSS charts)

## 10. Dependencies

- **S4**: Play events come from playerStore (HTML5 backend)
- **reqwest** (Rust): For DeepSeek API calls (already a transitive dep via Tauri)
- **No new C++ dependencies**
- **No new frontend dependencies** (CSS charts, no D3)

## 11. AI Analysis Prompt

```
你是一个音乐分析助手。基于以下用户的听歌统计数据，请用中文给出简短的分析：

1. 听歌习惯总结（2-3句话）
2. 音乐品味画像（2-3句话）
3. 一个有趣的发现或建议

统计数据：
- 总播放次数: {total_plays}
- 总听歌时长: {total_listened_formatted}
- 完成率: {completion_rate}%
- Top 歌曲: {top_songs}
- Top 歌手: {top_artists}
- 播放时间线: {timeline}

请控制在200字以内，语气友好轻松。
```

DeepSeek API key stored in `localStorage` under key `deepseek_api_key`. Model: `deepseek-v4-flash`.

## 12. Risks

| Risk | Mitigation |
|---|---|
| SQLite migration breaks existing data | Test migration on old schema, backup before ALTER |
| DeepSeek API latency | Show loading spinner, 10s timeout |
| API key security | localStorage only, never logged, password-type input |
| Large play_history table | Index on played_at, LIMIT on all queries |
| CORS for cover art | Cover URLs already work in existing UI |
