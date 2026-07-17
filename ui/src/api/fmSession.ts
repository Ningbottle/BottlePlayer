import { apiGet } from './backend';
import { normalizeTrack, type Track } from './normalizer';
import { playbackDiagnostics } from './playbackDiagnostics';

export type FmState = {
  currentTrack: Track | null;
  queue: Track[];
  currentIndex: number;
  currentTime: number;
};

export type FmSessionDeps = {
  getState: () => FmState;
  saveQueue: () => void;
};

function extractSongList(payload: any): any[] {
  const data = payload?.data?.data || payload?.data || payload || {};
  const list = data.song_list || data.info || data.list || data.songs || [];
  return Array.isArray(list) ? list : [];
}

function isPersonalFmFailure(payload: any): boolean {
  return payload?.status === 0;
}

async function fetchPersonalFmRecommendations(
  query: Record<string, string | number>,
): Promise<any> {
  let lastResponse: any;
  for (let attempt = 0; attempt < 2; attempt++) {
    lastResponse = await apiGet<any>('/personal/fm', query);
    if (!isPersonalFmFailure(lastResponse)) return lastResponse;
  }
  return lastResponse;
}

export async function appendPersonalFmRecommendations(
  deps: FmSessionDeps,
): Promise<boolean> {
  const state = deps.getState();
  const current = state.currentTrack;
  const remain = Math.max(0, state.queue.length - state.currentIndex - 1);
  const trackKey = current?.FileHash || '';
  playbackDiagnostics.recordEvent({
    kind: 'fm_fetch',
    phase: 'start',
    detail: `remain=${remain}; is_overplay=${remain === 0 ? 1 : 0}`,
    trackKey,
  });
  let response: any;
  try {
    response = await fetchPersonalFmRecommendations({
      hash: current?.FileHash || '',
      songid: current?.AlbumAudioID || current?.MixSongID || '',
      playtime: Math.floor(state.currentTime || 0),
      remain_songcnt: remain,
      is_overplay: remain === 0 ? 1 : 0,
    });
  } catch (e) {
    playbackDiagnostics.recordEvent({
      kind: 'fm_fetch',
      phase: 'fail',
      detail: `fetch threw: ${e instanceof Error ? e.message : String(e)}`,
      trackKey,
    });
    return false;
  }
  if (isPersonalFmFailure(response)) {
    playbackDiagnostics.recordEvent({
      kind: 'fm_fetch',
      phase: 'fail',
      detail: `status=0: ${response?.error || ''}`,
      trackKey,
    });
    console.warn('Personal FM recommendation returned an error:', response?.error || response);
    return false;
  }
  const existing = new Set(state.queue.map((track) => track.FileHash).filter(Boolean));
  const fresh = extractSongList(response)
    .map(normalizeTrack)
    .filter((track) => track.FileHash && !existing.has(track.FileHash));

  if (fresh.length === 0) {
    playbackDiagnostics.recordEvent({
      kind: 'fm_fetch',
      phase: 'noop',
      detail: 'no fresh songs after dedupe',
      trackKey,
    });
    return false;
  }
  state.queue.push(...fresh);
  deps.saveQueue();
  playbackDiagnostics.recordEvent({
    kind: 'fm_fetch',
    phase: 'ok',
    detail: `appended ${fresh.length} songs`,
    trackKey,
  });
  return true;
}
