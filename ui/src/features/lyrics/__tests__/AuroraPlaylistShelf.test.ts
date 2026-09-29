import { describe, it, expect, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import AuroraPlaylistShelf from '../AuroraPlaylistShelf.vue';

vi.mock('gsap', () => ({ gsap: { set: vi.fn(), fromTo: vi.fn(), killTweensOf: vi.fn() } }));
vi.mock('../../../shared/motion/motion', () => ({ isReducedMotion: () => true }));
const tracks = [{ FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 120 }];

describe('playlist shelf input ownership', () => {
  it('leaves an ordinary card click targeted at the card', async () => {
    const wrapper = mount(AuroraPlaylistShelf, { props: { open: true, tracks, activeHash: 'a' }, global: { stubs: { teleport: true } } });
    await nextTick();
    const stage = wrapper.get('[data-test="shelf-stage"]');
    const capture = vi.fn();
    Object.defineProperty(stage.element, 'setPointerCapture', { value: capture });
    const card = wrapper.get('[data-test="shelf-card-0"]');
    card.element.dispatchEvent(new MouseEvent('pointerdown', { button: 0, clientX: 100, bubbles: true }));
    expect(capture).not.toHaveBeenCalled();
    await card.trigger('click');
    expect(wrapper.emitted('select')).toEqual([[tracks[0], 0]]);
    wrapper.unmount();
  });

  it('follows the queue position of the playing track, not its hash', async () => {
    // The same song may legitimately sit at two queue positions. Hash identity
    // would anchor both cards (and jump playback back to the first copy).
    const doubled = [
      { FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 120 },
      { FileHash: 'b', SongName: 'B', SingerName: 'Artist', Duration: 120 },
      { FileHash: 'a', SongName: 'A (second copy)', SingerName: 'Artist', Duration: 120 },
    ];
    const wrapper = mount(AuroraPlaylistShelf, {
      props: { open: true, tracks: doubled, activeHash: 'a', activeIndex: 2 },
      global: { stubs: { teleport: true } },
    });
    await nextTick();

    const active = wrapper
      .findAll('[data-test^="shelf-card-"]')
      .filter((card) => card.classes().includes('is-active'));
    expect(active).toHaveLength(1);
    expect(active[0]!.attributes('aria-label')).toBe('A (second copy)');

    await active[0]!.trigger('click');
    expect(wrapper.emitted('select')).toEqual([[doubled[2], 2]]);
    wrapper.unmount();
  });

  it('keeps the highlight on the real position after the queue grows', async () => {
    const base = [
      { FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 120 },
      { FileHash: 'b', SongName: 'B', SingerName: 'Artist', Duration: 120 },
      { FileHash: 'c', SongName: 'C', SingerName: 'Artist', Duration: 120 },
    ];
    const wrapper = mount(AuroraPlaylistShelf, {
      props: { open: true, tracks: base, activeHash: 'c', activeIndex: 2 },
      global: { stubs: { teleport: true } },
    });
    await nextTick();

    // A duplicate of the playing track is inserted at the front: the first card
    // now has the same hash, but position 3 is still what is playing.
    await wrapper.setProps({
      tracks: [base[2]!, ...base],
      activeIndex: 3,
    });
    await nextTick();

    const cards = wrapper.findAll('[data-test^="shelf-card-"]');
    expect(cards.filter((card) => card.classes().includes('is-active'))).toHaveLength(1);
    expect(cards[3]!.classes()).toContain('is-active');
    expect(cards[0]!.classes()).not.toContain('is-active');
    wrapper.unmount();
  });

  it('consumes Escape before background fullscreen handlers, including initially open shelves', async () => {
    const background = vi.fn();
    window.addEventListener('keydown', background);
    const wrapper = mount(AuroraPlaylistShelf, { props: { open: true, tracks, activeHash: 'a' }, global: { stubs: { teleport: true } } });
    await nextTick();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', cancelable: true }));
    expect(wrapper.emitted('close')).toHaveLength(1);
    expect(background).not.toHaveBeenCalled();
    wrapper.unmount();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(background).toHaveBeenCalledTimes(1);
    window.removeEventListener('keydown', background);
  });
});


/** Deterministic queue factory - unique SongName per position, hash given per test. */
function makeQueue(count: number, prefix: string) {
  return Array.from({ length: count }, (_, index) => ({
    FileHash: prefix + '-' + index,
    SongName: 'Song ' + index,
    SingerName: 'Artist',
    Duration: 120,
  }));
}

function mountShelf(tracks: ReturnType<typeof makeQueue>, activeIndex = 0) {
  return mount(AuroraPlaylistShelf, {
    props: {
      open: true,
      tracks,
      activeHash: tracks[activeIndex]?.FileHash ?? null,
      activeIndex,
    },
    global: { stubs: { teleport: true } },
  });
}

function focusedLabel(wrapper: ReturnType<typeof mount>): string | undefined {
  return wrapper.find('.shelf-card.is-focus').attributes('aria-label');
}

function pressKey(key: string, times = 1): void {
  for (let i = 0; i < times; i += 1) {
    window.dispatchEvent(new KeyboardEvent('keydown', { key, cancelable: true }));
  }
}

describe('playlist shelf browses the entire logical queue', () => {
  it('renders the empty state and never clamps focus on an empty queue', async () => {
    const wrapper = mountShelf([]);
    try {
      await nextTick();
      expect(wrapper.find('[data-test="shelf-empty"]').exists()).toBe(true);
      expect(wrapper.findAll('[data-test^="shelf-card-"]')).toHaveLength(0);
      pressKey('ArrowRight');
      await nextTick();
      expect(wrapper.emitted('select')).toBeUndefined();
      pressKey('Enter');
      await nextTick();
      expect(wrapper.emitted('select')).toBeUndefined();
    } finally {
      wrapper.unmount();
    }
  });

  it('a one-track queue keeps its single card focused and selectable', async () => {
    const tracks = makeQueue(1, 'one');
    const wrapper = mountShelf(tracks);
    try {
      await nextTick();
      expect(wrapper.findAll('[data-test^="shelf-card-"]')).toHaveLength(1);
      expect(focusedLabel(wrapper)).toBe('Song 0');
      pressKey('ArrowRight');
      pressKey('ArrowLeft');
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 0');
      pressKey('Enter');
      await nextTick();
      expect(wrapper.emitted('select')).toEqual([[tracks[0], 0]]);
    } finally {
      wrapper.unmount();
    }
  });

  it.each([31, 32])('a %i-track queue fits one window and clamps at the real last item', async (size) => {
    const tracks = makeQueue(size, 'q' + size);
    const wrapper = mountShelf(tracks);
    try {
      await nextTick();
      expect(wrapper.findAll('[data-test^="shelf-card-"]')).toHaveLength(size);
      pressKey('ArrowRight', size);
      await nextTick();
      // Whole queue is visible: focus rests on the last item, window unmoved.
      expect(focusedLabel(wrapper)).toBe('Song ' + (size - 1));
      expect(wrapper.find('.shelf-card.is-active').attributes('aria-label')).toBe('Song 0');
      pressKey('ArrowRight');
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song ' + (size - 1));
      pressKey('Enter');
      await nextTick();
      expect(wrapper.emitted('select')).toEqual([[tracks[size - 1], size - 1]]);
    } finally {
      wrapper.unmount();
    }
  });

  it('a 33-track queue slides the window forward past the 32nd card', async () => {
    const tracks = makeQueue(33, 'over');
    const wrapper = mountShelf(tracks);
    try {
      await nextTick();
      expect(wrapper.findAll('[data-test^="shelf-card-"]')).toHaveLength(32);
      pressKey('ArrowRight', 33);
      await nextTick();
      // 32nd press slides the window by one; the focused card is queue item 32.
      expect(wrapper.findAll('[data-test^="shelf-card-"]')).toHaveLength(32);
      expect(focusedLabel(wrapper)).toBe('Song 32');
      // Clicking the focused card selects the exact queue index behind it.
      await wrapper.get('.shelf-card.is-focus').trigger('click');
      expect(wrapper.emitted('select')).toEqual([[tracks[32], 32]]);
    } finally {
      wrapper.unmount();
    }
  });

  it('a 40-track queue is fully reachable by keyboard from item 0, selecting exact queue indexes', async () => {
    const tracks = makeQueue(40, 'kbd');
    const wrapper = mountShelf(tracks);
    try {
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 0');
      pressKey('ArrowRight', 39);
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 39');
      // Window slid to its last valid start: first visible card is queue item 8.
      expect(wrapper.get('[data-test="shelf-card-0"]').attributes('aria-label')).toBe('Song 8');
      pressKey('Enter');
      await nextTick();
      expect(wrapper.emitted('select')).toEqual([[tracks[39], 39]]);
    } finally {
      wrapper.unmount();
    }
  });

  it('wheel slides the window to the last item and back to the first', async () => {
    const tracks = makeQueue(40, 'wheel');
    const wrapper = mountShelf(tracks);
    try {
      await nextTick();
      const stage = wrapper.get('[data-test="shelf-stage"]');
      for (let i = 0; i < 39; i += 1) {
        await stage.trigger('wheel', { deltaY: 120 });
      }
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 39');
      for (let i = 0; i < 39; i += 1) {
        await stage.trigger('wheel', { deltaY: -120 });
      }
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 0');
    } finally {
      wrapper.unmount();
    }
  });

  it('drag browses to the last item while held and restores the active item on release', async () => {
    const tracks = makeQueue(40, 'drag');
    const wrapper = mountShelf(tracks);
    try {
      await nextTick();
      const stage = wrapper.get('[data-test="shelf-stage"]').element as HTMLElement;
      const pointerId = 3;
      stage.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, clientX: 100, pointerId }));
      // One long leftward move -> 39 drag steps, past the window end to item 39.
      stage.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, clientX: 100 - 90 * 39, pointerId }));
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 39');
      expect(wrapper.get('[data-test="shelf-card-0"]').attributes('aria-label')).toBe('Song 8');
      stage.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, button: 0, clientX: 100 - 90 * 39, pointerId }));
      await new Promise((resolve) => setTimeout(resolve, 0));
      await nextTick();
      // Follow-hover contract preserved: release re-anchors on the playing track.
      expect(focusedLabel(wrapper)).toBe('Song 0');
    } finally {
      wrapper.unmount();
    }
  });

  it('slides the window back below the window start after following an end-of-queue item', async () => {
    const tracks = makeQueue(40, 'back');
    const wrapper = mountShelf(tracks, 39);
    try {
      await nextTick();
      // Following item 39 centers the window at its last valid start (8).
      expect(focusedLabel(wrapper)).toBe('Song 39');
      pressKey('ArrowLeft', 32);
      await nextTick();
      // 32nd left press slides the window back: focused card is queue item 7.
      expect(focusedLabel(wrapper)).toBe('Song 7');
      pressKey('ArrowLeft', 7);
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 0');
    } finally {
      wrapper.unmount();
    }
  });

  it('selects the exact queue index of a duplicate hash browsed to the end', async () => {
    const tracks = makeQueue(40, 'dup');
    // Same hash as item 0 at the end of the queue: browsing must land on the
    // second copy and select queue index 39, not re-anchor on the first.
    tracks[39] = { ...tracks[39], FileHash: 'dup-0' };
    const wrapper = mountShelf(tracks, 0);
    try {
      await nextTick();
      pressKey('ArrowRight', 39);
      await nextTick();
      expect(focusedLabel(wrapper)).toBe('Song 39');
      pressKey('Enter');
      await nextTick();
      const select = wrapper.emitted('select')!;
      expect(select).toHaveLength(1);
      expect(select[0]![1]).toBe(39);
      expect((select[0]![0] as { FileHash: string }).FileHash).toBe('dup-0');
      expect((select[0]![0] as { SongName: string }).SongName).toBe('Song 39');
    } finally {
      wrapper.unmount();
    }
  });
});
