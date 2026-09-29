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

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: Error) => void;
  const promise = new Promise<T>((res, rej) => { resolve = res; reject = rej; });
  return { promise, resolve, reject };
}

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

  it('exposes a labeled modal, supports Escape and Tab containment, and restores focus', async () => {
    const opener = document.createElement('button');
    opener.textContent = '收藏';
    document.body.appendChild(opener);
    opener.focus();
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track },
      attachTo: document.body,
    });

    try {
      await wrapper.setProps({ show: true });
      await flushPromises();

      const dialog = document.body.querySelector<HTMLElement>('[role="dialog"]');
      expect(dialog?.getAttribute('aria-modal')).toBe('true');
      expect(document.getElementById(dialog?.getAttribute('aria-labelledby') ?? '')?.textContent)
        .toBe('收藏到歌单');
      const closeButton = document.body.querySelector<HTMLButtonElement>('[data-modal-initial-focus]');
      const playlistButtons = Array.from(document.body.querySelectorAll<HTMLButtonElement>('.playlist-item'));
      expect(closeButton?.getAttribute('aria-label')).toBe('关闭收藏弹窗');
      expect(document.activeElement).toBe(closeButton);
      expect(playlistButtons.length).toBeGreaterThan(0);

      const shiftTab = new KeyboardEvent('keydown', { key: 'Tab', shiftKey: true, bubbles: true, cancelable: true });
      closeButton!.dispatchEvent(shiftTab);
      expect(shiftTab.defaultPrevented).toBe(true);
      expect(document.activeElement).toBe(playlistButtons[playlistButtons.length - 1]);

      const escape = new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true });
      playlistButtons[playlistButtons.length - 1].dispatchEvent(escape);
      expect(wrapper.emitted('close')).toHaveLength(1);

      await wrapper.setProps({ show: false });
      await flushPromises();
      expect(document.activeElement).toBe(opener);
    } finally {
      wrapper.unmount();
      opener.remove();
    }
  });

  it('uses disabled button semantics while a playlist add is in flight', async () => {
    await favoriteStore.onLogin('u1');
    const add = deferred<{ status: number }>();
    mockApiPost.mockReturnValueOnce(add.promise);
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track },
      attachTo: document.body,
    });

    try {
      await wrapper.setProps({ show: true });
      await flushPromises();
      const buttons = Array.from(document.body.querySelectorAll<HTMLButtonElement>('.playlist-item'));
      expect(buttons.length).toBeGreaterThan(0);
      expect(buttons[0].tagName).toBe('BUTTON');
      buttons[0].click();
      await flushPromises();

      expect(buttons.every((button) => button.disabled)).toBe(true);
      expect(document.body.querySelector('[aria-busy="true"]')).toBe(buttons[0]);
      expect(document.body.textContent).toContain('添加中…');

      add.resolve({ status: 1 });
      await flushPromises();
      expect(buttons.every((button) => !button.disabled)).toBe(true);
    } finally {
      wrapper.unmount();
    }
  });

  it('never closes the B modal when an A add succeeds after the track changes', async () => {
    await favoriteStore.onLogin('u1');
    const add = deferred<{ status: number }>();
    mockApiPost.mockReturnValueOnce(add.promise);
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track }, attachTo: document.body,
    });
    try {
      await wrapper.setProps({ show: true });
      await flushPromises();
      const liked = Array.from(document.body.querySelectorAll<HTMLElement>('.playlist-item'))
        .find((el) => el.textContent?.includes('我喜欢的音乐'))!;
      liked.click();
      await flushPromises();
      await wrapper.setProps({ track: { ...track, FileHash: 'modal-B', SongName: 'B' } });
      add.resolve({ status: 1 });
      await flushPromises();
      expect(favoriteStore.isFavorite('modal-B')).toBe(false);
      expect(wrapper.emitted('close')).toBeUndefined();
      expect(wrapper.emitted('success')).toBeUndefined();
      expect(document.body.querySelector('.playlist-modal')).not.toBeNull();
    } finally {
      wrapper.unmount();
    }
  });

  it('ignores a successful A add after the parent closes and reopens for B', async () => {
    await favoriteStore.onLogin('u1');
    const add = deferred<{ status: number }>();
    mockApiPost.mockReturnValueOnce(add.promise);
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track }, attachTo: document.body,
    });
    try {
      await wrapper.setProps({ show: true });
      await flushPromises();
      const liked = Array.from(document.body.querySelectorAll<HTMLElement>('.playlist-item'))
        .find((el) => el.textContent?.includes('我喜欢的音乐'))!;
      liked.click();
      await flushPromises();
      await wrapper.setProps({ show: false });
      await wrapper.setProps({ show: true, track: { ...track, FileHash: 'modal-B', SongName: 'B' } });
      await flushPromises();
      add.resolve({ status: 1 });
      await flushPromises();
      expect(wrapper.emitted('close')).toBeUndefined();
      expect(wrapper.emitted('success')).toBeUndefined();
      expect(favoriteStore.isFavorite('modal-B')).toBe(false);
    } finally {
      wrapper.unmount();
    }
  });

  it('drops a successful add from A after the active account switches to B', async () => {
    await favoriteStore.onLogin('u1');
    const add = deferred<{ status: number }>();
    mockApiPost.mockReturnValueOnce(add.promise);
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track }, attachTo: document.body,
    });
    try {
      await wrapper.setProps({ show: true });
      await flushPromises();
      const liked = Array.from(document.body.querySelectorAll<HTMLElement>('.playlist-item'))
        .find((el) => el.textContent?.includes('我喜欢的音乐'))!;
      liked.click();
      await flushPromises();
      userStore.userId = 'u2';
      await favoriteStore.onLogin('u2');
      add.resolve({ status: 1 });
      await flushPromises();
      expect(wrapper.emitted('success')).toBeUndefined();
      expect(wrapper.emitted('close')).toBeUndefined();
      expect(favoriteStore.isFavorite('modal-1')).toBe(false);
    } finally {
      wrapper.unmount();
    }
  });

  it('drops a late failure after unmount without emitting or mutating state', async () => {
    await favoriteStore.onLogin('u1');
    const add = deferred<{ status: number }>();
    mockApiPost.mockReturnValueOnce(add.promise);
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track }, attachTo: document.body,
    });
    await wrapper.setProps({ show: true });
    await flushPromises();
    const liked = Array.from(document.body.querySelectorAll<HTMLElement>('.playlist-item'))
      .find((el) => el.textContent?.includes('我喜欢的音乐'))!;
    liked.click();
    await flushPromises();
    wrapper.unmount();
    add.reject(new Error('offline'));
    await flushPromises();
    expect(wrapper.emitted('close')).toBeUndefined();
    expect(wrapper.emitted('success')).toBeUndefined();
    expect(wrapper.emitted('error')).toBeUndefined();
    expect(favoriteStore.isFavorite('modal-1')).toBe(false);
  });

  it('discards the old-account playlist response after an account switch', async () => {
    await favoriteStore.onLogin('u1');
    const oldList = deferred<ReturnType<typeof likedUserPlaylists>>();
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/user/playlist' && userStore.userId === 'u1') return oldList.promise;
      if (path === '/user/playlist') return Promise.resolve({ status: 1, data: { info: [{
        global_collection_id: 'collection_3_u2_333_0', listid: '333', listname: 'B 的歌单',
      }] } });
      return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
    });
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track }, attachTo: document.body,
    });
    try {
      await wrapper.setProps({ show: true });
      userStore.userId = 'u2';
      await flushPromises();
      expect(document.body.textContent).toContain('B 的歌单');
      oldList.resolve(likedUserPlaylists());
      await flushPromises();
      expect(document.body.textContent).toContain('B 的歌单');
      expect(document.body.textContent).not.toContain('通勤精选');
    } finally {
      wrapper.unmount();
    }
  });

  it('reports a playlist load failure rather than pretending there are no lists', async () => {
    mockApiGet.mockRejectedValueOnce(new Error('offline'));
    const wrapper = mount(AddToPlaylistModal, {
      props: { show: false, track }, attachTo: document.body,
    });
    try {
      await wrapper.setProps({ show: true });
      await flushPromises();
      expect(wrapper.emitted('error')?.[0]).toEqual(['歌单列表加载失败，请稍后重试']);
    } finally {
      wrapper.unmount();
    }
  });
});
