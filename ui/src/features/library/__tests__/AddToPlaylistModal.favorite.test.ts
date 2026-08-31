import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiGet: (...a: unknown[]) => mockApiGet(...a),
  apiPost: (...a: unknown[]) => mockApiPost(...a),
}));

vi.mock('gsap', () => ({
  gsap: {
    fromTo: vi.fn((_el: unknown, _from: unknown, opts: unknown & { onComplete?: () => void }) => {
      opts?.onComplete?.();
      return { kill: () => {} };
    }),
    to: vi.fn((_el: unknown, opts: unknown & { onComplete?: () => void }) => {
      opts?.onComplete?.();
      return { kill: () => {} };
    }),
    set: vi.fn(),
    killTweensOf: vi.fn(),
  },
}));

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue('') }));

import AddToPlaylistModal from '../AddToPlaylistModal.vue';
import { userStore } from '../../account';
import { favoriteStore, __resetFavoriteStoreForTests } from '../favoriteStore';
import { __resetFavoriteRepositoryForTests } from '../favoriteRepository';
import type { Track } from '../../../shared/music/track';

const track = {
  FileHash: 'modal-1',
  SongName: 'Modal Song',
  SingerName: 'A',
  Duration: 100,
  audio_id: '7',
} as Track;

function likedUserPlaylists() {
  return {
    status: 1,
    data: {
      info: [
        { global_collection_id: 'collection_3_u1_999_0', listid: '999', listname: '我喜欢的音乐', songcount: 0 },
        { global_collection_id: 'collection_3_u1_888_0', listid: '888', listname: '通勤精选', songcount: 3 },
      ],
    },
  };
}

describe('AddToPlaylistModal shared favorite state', () => {
  beforeEach(() => {
    __resetFavoriteStoreForTests();
    __resetFavoriteRepositoryForTests();
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    userStore.isLoggedIn = true;
    userStore.userId = 'u1';
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/user/playlist') return Promise.resolve(likedUserPlaylists());
      return Promise.resolve({ status: 1, data: {} });
    });
    mockApiPost.mockResolvedValue({ status: 1 });
  });

  it('adding to the liked playlist marks the track favorite in the shared store', async () => {
    // Resolve the liked playlist (listid 999) in the shared store first.
    await favoriteStore.onLogin('u1');
    expect(favoriteStore.isFavorite('modal-1')).toBe(false);

    // The modal loads playlists when `show` transitions to true (its watch is
    // not immediate) and teleports its content to document.body.
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track },
      attachTo: document.body,
    });
    await wrapper.setProps({ show: true });
    await flushPromises(); // load playlists

    const items = Array.from(document.body.querySelectorAll<HTMLElement>('.playlist-item'));
    const likedItem = items.find((el) => el.textContent?.includes('我喜欢的音乐'));
    expect(likedItem).toBeTruthy();
    likedItem!.click();
    await flushPromises();

    // The shared favorite store (read by the player bar heart) is updated.
    expect(favoriteStore.isFavorite('modal-1')).toBe(true);
    expect(wrapper.emitted('success')).toBeTruthy();
    wrapper.unmount();
  });

  it('adding to a non-liked playlist does not mark the track favorite', async () => {
    await favoriteStore.onLogin('u1');
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track },
      attachTo: document.body,
    });
    await wrapper.setProps({ show: true });
    await flushPromises();

    const items = Array.from(document.body.querySelectorAll<HTMLElement>('.playlist-item'));
    const otherItem = items.find((el) => el.textContent?.includes('通勤精选'));
    expect(otherItem).toBeTruthy();
    otherItem!.click();
    await flushPromises();

    expect(favoriteStore.isFavorite('modal-1')).toBe(false);
    expect(wrapper.emitted('success')).toBeTruthy();
    wrapper.unmount();
  });
});
