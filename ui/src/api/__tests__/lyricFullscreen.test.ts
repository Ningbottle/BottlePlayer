import { describe, it, expect } from 'vitest';
import { lyricFullscreen, setLyricFullscreen } from '../lyricFullscreen';

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
});
