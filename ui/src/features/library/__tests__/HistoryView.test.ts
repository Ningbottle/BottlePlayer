import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));

vi.mock('../../../playback/playerStore', async () => {
  const actual = await vi.importActual<typeof import('../../../playback/playerStore')>('../../../playback/playerStore');
  return { ...actual, playAll: vi.fn() };
});

vi.mock('../../account', async () => ({
  userStore: (await import('vue')).reactive({ isLoggedIn: true, userId: 'a', deviceReady: true }),
  checkLoginStatus: vi.fn().mockResolvedValue(undefined),
}));

import HistoryView from '../HistoryView.vue';
import { userStore as mockUserStore } from '../../account';
import { playAll } from '../../../playback/index';
import { recentPlayedStore } from '../../../playback/data/recentPlayedStore';
import type { Track } from '../../../shared/music/track';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'Artist ' + hash, Duration: 200, Image: 'http://img/' + hash } as Track;
}

describe('HistoryView local-first recent-played', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    recentPlayedStore.reset();
    mockApiGet.mockReset();
    mockUserStore.isLoggedIn = true;
    mockUserStore.userId = 'a';
    mockUserStore.deviceReady = true;
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    recentPlayedStore.reset();
  });

  function mountHistory() {
    wrapper = mount(HistoryView, { attachTo: document.body });
    return wrapper;
  }

  it('renders local recent-played entries immediately on mount, before remote resolves', async () => {
    recentPlayedStore.recordRecentPlayed(mkTrack('local-1', 'Local One'));
    recentPlayedStore.recordRecentPlayed(mkTrack('local-2', 'Local Two'));

    // Remote never resolves — local-first render must still show the entries.
    mockApiGet.mockImplementation(() => new Promise(() => {}));

    const w = mountHistory();
    await flushPromises();

    expect(w.text()).toContain('Local One');
    expect(w.text()).toContain('Local Two');
  });

  it('keeps local entries visible when remote fetch fails (non-blocking sync status)', async () => {
    recentPlayedStore.recordRecentPlayed(mkTrack('local-1', 'Local One'));
    mockApiGet.mockRejectedValue(new Error('network down'));

    const w = mountHistory();
    await flushPromises();

    expect(w.text()).toContain('Local One');
    expect(w.text()).toContain('远端同步失败');
  });

  it('merges remote entries with local, deduped by FileHash (remote newer wins)', async () => {
    recentPlayedStore.recordRecentPlayed(mkTrack('shared', 'Local Shared'));
    recentPlayedStore.recordRecentPlayed(mkTrack('local-only', 'Local Only'));

    // Remote: 'shared' with a far-future timestamp (wins over local) + a new 'remote-only'.
    mockApiGet.mockResolvedValue({
      status: 1,
      data: {
        info: [
          { info: { hash: 'shared', name: 'Remote Shared Newer', singername: 'R', duration: 200 }, time: 9999999999 },
          { info: { hash: 'remote-only', name: 'Remote Only', singername: 'R', duration: 200 }, time: 9999999998 },
        ],
      },
    });

    const w = mountHistory();
    await flushPromises();

    const text = w.text();
    // 'shared' appears once — remote entry wins (newer), so the remote name shows.
    expect(text).toContain('Remote Shared Newer');
    expect(text).not.toContain('Local Shared');
    // Both local-only and remote-only appear.
    expect(text).toContain('Local Only');
    expect(text).toContain('Remote Only');
  });

  it('shows local entries when not logged in and does not fetch remote', async () => {
    mockUserStore.isLoggedIn = false;
    recentPlayedStore.recordRecentPlayed(mkTrack('local-1', 'Local One'));

    const w = mountHistory();
    await flushPromises();

    expect(w.text()).toContain('Local One');
    expect(mockApiGet).not.toHaveBeenCalled();
  });

  it('does not let undated remote entries displace actual local listens and accepts numeric string dates', async () => {
    recentPlayedStore.recordRecentPlayed(mkTrack('shared', 'Latest local'));
    mockApiGet.mockResolvedValue({ status: 1, data: { info: [
      null,
      { info: { hash: 'SHARED', name: 'Undated duplicate', duration: '200' } },
      { info: { hash: 'old', name: 'Old remote', duration: 'bad' }, time: '1600000000' },
      { info: { hash: 'unknown', name: 'Undated remote' } },
    ] } });
    const w = mountHistory();
    await flushPromises();
    const rows = w.findAll('.song-row[role="button"]');
    expect(rows.map(row => row.find('.title').text())).toEqual(['Latest local', 'Old remote', 'Undated remote']);
    expect(w.text()).not.toContain('NaN');
  });

  it('normalizes timestamp from both the shared songs fixture and wrapped song metadata', async () => {
    mockApiGet.mockResolvedValue({
      status: 1,
      data: {
        songs: [
          { info: { hash: 'song-timestamp', name: '歌曲行时间', timestamp: 1700000000000 } },
          { hash: 'fixture-timestamp', name: '夹具时间', timestamp: 1800000000000 },
          { hash: 'undated', name: '未知时间' },
        ],
        bp: 'synthetic-next-cursor',
        bp_finished: 'opaque-upstream-string',
        has_more: 1,
      },
    });

    const w = mountHistory();
    await flushPromises();

    const rows = w.findAll('.song-row[role="button"]');
    expect(rows.map(row => row.find('.title').text())).toEqual(['夹具时间', '歌曲行时间', '未知时间']);
  });

  it('falls through invalid time aliases and keeps 1990s epoch milliseconds as milliseconds', async () => {
    mockApiGet.mockResolvedValue({
      status: 1,
      data: {
        songs: [
          { hash: 'undated', name: '未知时间' },
          { hash: 'legacy-ms', name: '1990年代毫秒', time: 0, addtime: 'invalid', timestamp: 752460000000 },
          { info: { hash: 'valid-nested', name: '嵌套秒', time: 'invalid', timestamp: '1700000000' } },
          { hash: 'valid-fallback', name: '外层秒', time: 0, timestamp: '1735689600' },
        ],
      },
    });

    const w = mountHistory();
    await flushPromises();

    const rows = w.findAll('.song-row[role="button"]');
    expect(rows.map(row => row.find('.title').text())).toEqual([
      '外层秒',
      '嵌套秒',
      '1990年代毫秒',
      '未知时间',
    ]);
  });

  it('keeps history rows actionable by mouse and keyboard', async () => {
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { info: [
        { hash: 'first', name: '第一首', time: 1800000000 },
        { hash: 'second', name: '第二首', time: 1700000000 },
      ] },
    });
    const w = mountHistory();
    await flushPromises();

    await w.get('[aria-label="播放 第二首"]').trigger('keydown.enter');

    expect(playAll).toHaveBeenCalledWith(
      [expect.objectContaining({ FileHash: 'first', SongName: '第一首' }), expect.objectContaining({ FileHash: 'second', SongName: '第二首' })],
      1,
    );
  });

  it('discards a stale account response and clears remote history on logout', async () => {
    let resolveOld!: (value: unknown) => void;
    mockApiGet.mockImplementationOnce(() => new Promise(resolve => { resolveOld = resolve; }));
    const w = mountHistory();
    mockApiGet.mockResolvedValue({ status: 1, data: { info: [{ hash: 'b-song', name: 'Account B' }] } });
    mockUserStore.userId = 'b';
    await flushPromises();
    resolveOld({ status: 1, data: { info: [{ hash: 'a-song', name: 'Account A' }] } });
    await flushPromises();
    expect(w.text()).toContain('Account B');
    expect(w.text()).not.toContain('Account A');
    mockUserStore.isLoggedIn = false;
    await flushPromises();
    expect(w.text()).not.toContain('Account B');
  });

  it('loads when the device becomes ready and retries a failed sync without hiding local rows', async () => {
    mockUserStore.deviceReady = false;
    recentPlayedStore.recordRecentPlayed(mkTrack('local', 'Local'));
    const w = mountHistory();
    expect(mockApiGet).not.toHaveBeenCalled();
    mockApiGet.mockResolvedValueOnce({ status: 0 });
    mockUserStore.deviceReady = true;
    await flushPromises();
    expect(w.text()).toContain('Local');
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { info: [] } });
    await w.get('.history-retry').trigger('click');
    await flushPromises();
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    expect(w.find('.history-retry').exists()).toBe(false);
  });
});
