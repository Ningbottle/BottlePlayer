import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('../invoke', () => ({
  invokeTauri: vi.fn(),
}));

import { invokeTauri as invoke } from '../invoke';
import { prepareAudioSourceUrl } from '../audioProxy';

describe('audioProxy', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('turns http audio URLs into CORS-safe local proxy URLs', async () => {
    vi.mocked(invoke).mockResolvedValue('http://127.0.0.1:17631/audio/7');

    const prepared = await prepareAudioSourceUrl('https://cdn.example/song.mp3');

    expect(invoke).toHaveBeenCalledWith('audio_proxy_url', {
      url: 'https://cdn.example/song.mp3',
    });
    expect(prepared).toEqual({
      url: 'http://127.0.0.1:17631/audio/7',
      crossOriginSafe: true,
    });
  });

  it('leaves non-http URLs direct and marks them unsafe for WebAudio EQ', async () => {
    const prepared = await prepareAudioSourceUrl('file:///tmp/song.mp3');

    expect(invoke).not.toHaveBeenCalled();
    expect(prepared).toEqual({
      url: 'file:///tmp/song.mp3',
      crossOriginSafe: false,
    });
  });
});
