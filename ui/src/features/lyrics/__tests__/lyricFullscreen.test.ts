import { describe, it, expect } from 'vitest';
import { lyricFullscreen, setLyricFullscreen } from '../lyricFullscreen';
import { routeNames } from '../../../app/navigation/routes';
import { createAppRouter } from '../../../app/navigation/router';

describe('lyricFullscreen', () => {
  it('starts false', () => {
    expect(lyricFullscreen.value).toBe(false);
  });
  it('setLyricFullscreen(true) sets the ref to true', () => {
    setLyricFullscreen(true);
    expect(lyricFullscreen.value).toBe(true);
    setLyricFullscreen(false); // reset
    expect(lyricFullscreen.value).toBe(false);
  });

  it('leaving lyric for any non-lyric route immediately exits fullscreen', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.lyric });
    setLyricFullscreen(true);

    try {
      await router.push({ name: routeNames.home });
      expect(lyricFullscreen.value).toBe(false);
    } finally {
      setLyricFullscreen(false);
    }
  });

  it.each([
    routeNames.home,
    routeNames.search,
    routeNames.playlist,
    routeNames.settings,
  ] as const)('clears a leftover fullscreen flag when navigating from home to %s', async (target) => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    setLyricFullscreen(true);

    try {
      if (target === routeNames.playlist) {
        await router.push({ name: target, params: { id: '1' } });
      } else if (target === routeNames.home) {
        await router.push({ name: routeNames.search });
        setLyricFullscreen(true);
        await router.push({ name: routeNames.home });
      } else {
        await router.push({ name: target });
      }
      expect(lyricFullscreen.value).toBe(false);
    } finally {
      setLyricFullscreen(false);
    }
  });

  it('does not clear fullscreen while entering or staying on lyric', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.push({ name: routeNames.lyric });
    setLyricFullscreen(true);

    try {
      await router.push({ name: routeNames.lyric, query: { k: '1' } });
      expect(lyricFullscreen.value).toBe(true);
    } finally {
      setLyricFullscreen(false);
    }
  });

  it('home → lyric keeps a later fullscreen request, then lyric → playlist clears it', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.push({ name: routeNames.lyric });
    setLyricFullscreen(true);
    expect(lyricFullscreen.value).toBe(true);

    try {
      await router.push({ name: routeNames.playlist, params: { id: '42' } });
      expect(router.currentRoute.value.name).toBe(routeNames.playlist);
      expect(lyricFullscreen.value).toBe(false);
    } finally {
      setLyricFullscreen(false);
    }
  });
});
