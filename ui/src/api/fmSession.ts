import { apiGet } from './backend';
import { normalizeTrack, type Track } from './normalizer';
import { playbackDiagnostics } from './playbackDiagnostics';

export type FmState = {
  currentTrack: Track | null;
  queue: Track[];
  currentIndex: number;
  currentTime: number;
  queueMode: 'normal' | 'personalFm';
};

export type FmSessionDeps = {
  getState: () => FmState;
  saveQueue: () => void;
};

/**
 * Personal-FM session status (observability).
 *
 * The FM session is independent from the Daily Picks snapshot: only an explicit
 * FM entry (`playPersonalFm`) sets `queueMode = 'personalFm'`. Recommendations
 * are appended only when the player nears the queue tail, deduped by FileHash,
 * and an in-flight append is discarded if its session was superseded.
 */
export interface FmSessionStatus {
  /** Monotonic session counter; bumps when a new FM session is detected. */
  generation: number;
  /** True while a recommendation fetch is in flight. */
  pending: boolean;
  /** True once a successful fetch yielded no fresh tracks (until session reset). */
  exhausted: boolean;
}

let fmGeneration = 0;
let lastQueueRef: Track[] | null = null;
/** Soft cooldown after an empty/deduped response — not a permanent lock. */
let exhaustedUntil = 0;
/** Consecutive empty successful responses before we apply the soft cooldown. */
let emptyStreak = 0;

/** After this many consecutive empty responses, pause refetch for COOLDOWN_MS. */
const EMPTY_STREAK_BEFORE_COOLDOWN = 2;
const EMPTY_COOLDOWN_MS = 30_000;

interface Inflight {
  generation: number;
  queueRef: Track[];
  promise: Promise<boolean>;
}
let inflight: Inflight | null = null;

function isSoftExhausted(): boolean {
  return exhaustedUntil > Date.now();
}

export function getFmSessionState(): FmSessionStatus {
  return {
    generation: fmGeneration,
    pending: inflight !== null,
    exhausted: isSoftExhausted(),
  };
}

/** Test-only: reset module-level session state between cases. */
export function __resetFmSessionForTests(): void {
  fmGeneration = 0;
  lastQueueRef = null;
  exhaustedUntil = 0;
  emptyStreak = 0;
  inflight = null;
}

/** Test-only: advance soft-exhausted clock for cooldown tests. */
export function __setFmExhaustedUntilForTests(ts: number): void {
  exhaustedUntil = ts;
}

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

/**
 * Append fresh personal-FM recommendations to the current queue tail.
 *
 * Session safety:
 * - Returns false without fetching when `queueMode !== 'personalFm'` (Daily
 *   Picks and other normal queues must never drive the FM session).
 * - Detects a new session by the queue array identity changing
 *   (`playPersonalFm`/`playAll`/`clearQueue` all install a fresh array). A new
 *   session bumps `generation`, clears `exhausted`, and drops any stale
 *   in-flight fetch.
 * - Coalesces concurrent tail-advance calls within one session into a single
 *   fetch (`pending`).
 * - After the fetch resolves, the append is applied only if the queue array
 *   and `queueMode` are still the same; otherwise the response is discarded so
 *   a superseded session can never contaminate the current one.
 * - Soft-cools after consecutive empty/deduped responses (not a permanent lock).
 *   Transport failures stay immediately retryable.
 */
export async function appendPersonalFmRecommendations(
  deps: FmSessionDeps,
): Promise<boolean> {
  const state = deps.getState();
  if (state.queueMode !== 'personalFm') return false;

  // A new queue array identity => a new FM session.
  if (lastQueueRef !== state.queue) {
    fmGeneration += 1;
    exhaustedUntil = 0;
    emptyStreak = 0;
    inflight = null;
    lastQueueRef = state.queue;
  }

  // Coalesce concurrent calls within the same session into one fetch.
  if (inflight && inflight.queueRef === state.queue) {
    return inflight.promise;
  }

  if (isSoftExhausted()) {
    return false;
  }

  const queueRef = state.queue;
  const current = state.currentTrack;
  const remain = Math.max(0, state.queue.length - state.currentIndex - 1);
  const trackKey = current?.FileHash || '';

  const promise = (async (): Promise<boolean> => {
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

    // Session superseded (queue replaced, or left personalFm) while fetching.
    const latest = deps.getState();
    if (latest.queue !== queueRef || latest.queueMode !== 'personalFm') {
      playbackDiagnostics.recordEvent({
        kind: 'fm_fetch',
        phase: 'noop',
        detail: 'discarded stale session response',
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

    const existing = new Set(latest.queue.map((track) => track.FileHash).filter(Boolean));
    // Dedup against the existing queue AND within this response (a single
    // /personal/fm payload can list the same FileHash more than once).
    const fresh: Track[] = [];
    const seenInResponse = new Set<string>();
    for (const raw of extractSongList(response)) {
      const track = normalizeTrack(raw);
      if (!track.FileHash) continue;
      if (existing.has(track.FileHash)) continue;
      if (seenInResponse.has(track.FileHash)) continue;
      seenInResponse.add(track.FileHash);
      fresh.push(track);
    }

    if (fresh.length === 0) {
      emptyStreak += 1;
      if (emptyStreak >= EMPTY_STREAK_BEFORE_COOLDOWN) {
        exhaustedUntil = Date.now() + EMPTY_COOLDOWN_MS;
        emptyStreak = 0;
      }
      playbackDiagnostics.recordEvent({
        kind: 'fm_fetch',
        phase: 'noop',
        detail: `no fresh songs after dedupe; streak cooldown=${isSoftExhausted()}`,
        trackKey,
      });
      return false;
    }

    emptyStreak = 0;
    exhaustedUntil = 0;
    latest.queue.push(...fresh);
    deps.saveQueue();
    playbackDiagnostics.recordEvent({
      kind: 'fm_fetch',
      phase: 'ok',
      detail: `appended ${fresh.length} songs`,
      trackKey,
    });
    return true;
  })();

  inflight = { generation: fmGeneration, queueRef, promise };
  void promise.finally(() => {
    if (inflight?.promise === promise) inflight = null;
  });
  return promise;
}
