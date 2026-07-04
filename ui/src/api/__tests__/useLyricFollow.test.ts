import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { ref, nextTick, effectScope } from 'vue';
import { useLyricFollow, type UseLyricFollowReturn } from '../useLyricFollow';

function setup(opts: { activeIndex?: number; now?: () => number } = {}) {
  const activeIndex = ref(opts.activeIndex ?? 0);
  const scrolledTo: number[] = [];
  const follow = useLyricFollow({
    activeIndex,
    scrollToLine: (idx: number) => scrolledTo.push(idx),
    now: opts.now ?? (() => 0),
  });
  return { activeIndex, scrolledTo, follow };
}

describe('useLyricFollow', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-07-04T00:00:00Z'));
  });
  afterEach(() => vi.useRealTimers());

  it('starts auto-following active', () => {
    const { follow } = setup();
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('auto-scrolls to the active line when activeIndex changes while following', async () => {
    const { activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    activeIndex.value = 2;
    await nextTick();
    expect(scrolledTo).toEqual([2]);
  });

  it('onUserScroll suspends auto-follow and resumes after 3s idle', () => {
    const { follow } = setup({ now: () => 1000 });
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    expect(follow.manualScrollUntil.value).toBe(1000 + 3000);

    vi.advanceTimersByTime(2999);
    expect(follow.autoFollowing.value).toBe(false);

    vi.advanceTimersByTime(1);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('resumeFollow immediately re-follows and scrolls to the active line', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 3 });
    follow.onUserScroll(); // suspend
    expect(follow.autoFollowing.value).toBe(false);
    scrolledTo.length = 0;

    follow.resumeFollow();
    expect(follow.autoFollowing.value).toBe(true);
    expect(scrolledTo).toContain(activeIndex.value);
  });

  it('resetForTrack resets follow state and only fires when the track key changes', () => {
    const { follow } = setup();
    follow.onUserScroll(); // suspend
    expect(follow.autoFollowing.value).toBe(false);

    follow.resetForTrack('track-B');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
    expect(follow.trackKey.value).toBe('track-B');

    // Same key again — must NOT reset (so a metadata refresh on the same
    // track doesn't wipe an in-progress manual-scroll suspension).
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    follow.resetForTrack('track-B');
    expect(follow.autoFollowing.value).toBe(false);

    // A different key DOES reset.
    follow.resetForTrack('track-C');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.trackKey.value).toBe('track-C');
  });

  it('does not auto-scroll while auto-follow is suspended by user scroll', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    follow.onUserScroll(); // suspend
    scrolledTo.length = 0;

    activeIndex.value = 4;
    await nextTick();
    expect(scrolledTo).toEqual([]); // no scroll — user is browsing freely
  });

  it('resetForTrack clears a pending idle resume so it cannot fire after the reset', () => {
    const { follow } = setup({ activeIndex: 5 });
    follow.onUserScroll(); // schedules a 3s resume
    follow.resetForTrack('new-track');

    // Advance well past the idle window — the stale timer must not have fired
    // (it was cleared; autoFollowing stays true from the reset, not from a late timer).
    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('does not auto-scroll when activeIndex becomes negative (guard: idx >= 0)', async () => {
    const { activeIndex, scrolledTo } = setup({ activeIndex: 2 });
    activeIndex.value = -1;
    await nextTick();
    expect(scrolledTo).toEqual([]);
  });

  it('repeated onUserScroll pushes out the idle timer; resumes 3s after the LAST scroll', () => {
    const { follow } = setup({ now: () => 1000 });
    follow.onUserScroll(); // schedules resume in 3000ms
    vi.advanceTimersByTime(2000); // 1000ms before resume

    follow.onUserScroll(); // cancels old timer, schedules a NEW 3000ms resume
    vi.advanceTimersByTime(2000); // past the FIRST schedule, but only 2000ms past the second
    expect(follow.autoFollowing.value).toBe(false); // still suspended

    vi.advanceTimersByTime(1000); // 3000ms past the second scroll
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('resetForTrack("") is a no-op when trackKey is already empty (initial state)', () => {
    const { follow } = setup();
    expect(follow.trackKey.value).toBe('');
    expect(follow.autoFollowing.value).toBe(true);

    follow.resetForTrack(''); // same key — no-op

    expect(follow.trackKey.value).toBe('');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
    // No timer should have been scheduled.
    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('resetForTrack transitions to an empty key ("") from a non-empty key and resets', () => {
    const { follow } = setup();
    follow.resetForTrack('track-A');
    expect(follow.trackKey.value).toBe('track-A');
    follow.onUserScroll(); // suspend
    expect(follow.autoFollowing.value).toBe(false);

    follow.resetForTrack(''); // different key — resets
    expect(follow.trackKey.value).toBe('');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
  });

  it('resumeFollow clears the pending idle timer so it cannot fire later', () => {
    const { follow } = setup({ activeIndex: 3 });
    follow.onUserScroll(); // schedules 3s resume
    follow.resumeFollow(); // should cancel the pending timer
    expect(follow.autoFollowing.value).toBe(true);

    vi.advanceTimersByTime(5000); // well past the idle window
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
  });

  it('resumeFollow does not scroll when activeIndex is negative', () => {
    const { follow, scrolledTo } = setup({ activeIndex: -1 });
    follow.onUserScroll(); // suspend
    scrolledTo.length = 0;

    follow.resumeFollow();
    expect(follow.autoFollowing.value).toBe(true);
    expect(scrolledTo).toEqual([]); // idx is -1, guard prevents scroll
  });

  it('after resumeFollow, subsequent activeIndex changes auto-scroll again', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    follow.onUserScroll(); // suspend
    follow.resumeFollow(); // re-enable (scrolls to current idx=0 by design)
    scrolledTo.length = 0; // clear AFTER resume so we only capture the subsequent change

    activeIndex.value = 7;
    await nextTick();
    expect(scrolledTo).toEqual([7]);
  });

  it('does not scroll on initial setup (watch is not immediate)', async () => {
    const { scrolledTo } = setup({ activeIndex: 5 });
    await nextTick();
    expect(scrolledTo).toEqual([]);
  });

  it('clears the idle timer when the composable scope is disposed (no late mutation)', () => {
    const scope = effectScope();
    let follow!: UseLyricFollowReturn;
    scope.run(() => {
      follow = useLyricFollow({
        activeIndex: ref(0),
        scrollToLine: () => {},
        now: () => 1000,
      });
    });
    follow.onUserScroll(); // schedules a 3s resume
    expect(follow.autoFollowing.value).toBe(false);

    scope.stop(); // dispose — should clear the pending idle timer

    // Advance well past the idle window. If the timer was cleared, autoFollowing
    // stays false (no late mutation on a disposed scope).
    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(false);
  });
});
