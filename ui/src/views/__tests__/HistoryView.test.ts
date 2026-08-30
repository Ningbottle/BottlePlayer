import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../platform/tauri/nativeClient', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));

vi.mock('../../playback/playerStore', async () => {
  const actual = await vi.importActual<typeof import('../../playback/playerStore')>('../../playback/playerStore');
  return { ...actual, playAll: vi.fn() };
});

const { mockUserStore } = vi.hoisted(() => ({ mockUserStore: { isLoggedIn: true } }));
vi.mock('../../api/userStore', () => ({
  userStore: mockUserStore,
  checkLoginStatus: vi.fn().mockResolvedValue(undefined),
}));

import HistoryView from '../HistoryView.vue';
import { recentPlayedStore } from '../../playback/data/recentPlayedStore';
import type { Track } from '../../api/normalizer';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'Artist ' + hash, Duration: 200, Image: 'http://img/' + hash } as Track;
}

describe('HistoryView local-first recent-played', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    recentPlayedStore.reset();
    mockApiGet.mockReset();
    mockUserStore.isLoggedIn = true;
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
});
