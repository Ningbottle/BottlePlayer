import { describe, it, expect } from 'vitest';
import { lyricFullscreen, setLyricFullscreen } from '../lyricFullscreen';
import { routeNames } from '../../navigation/routes';
import { createAppRouter } from '../../navigation/router';

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
});
