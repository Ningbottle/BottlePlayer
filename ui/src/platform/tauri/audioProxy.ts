import { invokeTauri } from './invoke';
import type { PreparedAudioSource } from '../../shared/media/audioSource';

function isHttpUrl(url: string): boolean {
  return /^https?:\/\//i.test(url);
}

export async function prepareAudioSourceUrl(url: string): Promise<PreparedAudioSource> {
  if (!isHttpUrl(url)) {
    return { url, crossOriginSafe: false };
  }

  try {
    const proxyUrl = await invokeTauri<string>('audio_proxy_url', { url });
    if (typeof proxyUrl !== 'string' || !proxyUrl) {
      return { url, crossOriginSafe: false };
    }
    return { url: proxyUrl, crossOriginSafe: true };
  } catch (e) {
    console.warn('Audio proxy unavailable; falling back to direct playback:', e);
    return { url, crossOriginSafe: false };
  }
}
