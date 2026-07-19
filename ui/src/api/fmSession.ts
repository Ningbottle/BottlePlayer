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

export type PersonalFmAppendSuccess = {
  generation: number;
  queueRef: Track[];
  appendedCount: number;
};

export type PersonalFmAppendOptions = {
  /** Called only when a timer-backed retry appends fresh tracks. */
  onRetrySuccess?: (result: PersonalFmAppendSuccess) => void;
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
  /** True after the bounded retry budget is exhausted, until a new attempt/session. */
  exhausted: boolean;
}

let fmGeneration = 0;
let lastQueueRef: Track[] | null = null;
let retryExhausted = false;
/**
 * Bumped by disposeFmSession (exit / HMR). In-flight fetches capture it at
 * start and discard their result if it changed, so an append cannot land on a
 * queue that is being torn down or has moved to a new module.
 */
let disposeEpoch = 0;

const AUTO_RETRY_DELAYS_MS = [1_000, 3_000, 10_000] as const;

interface Inflight {
  generation: number;
  queueRef: Track[];
  promise: Promise<boolean>;
}
let inflight: Inflight | null = null;

interface RetryState {
  generation: number;
  queueRef: Track[];
  currentTrack: Track | null;
  currentTrackKey: string;
  currentIndex: number;
  deps: FmSessionDeps;
  options: PersonalFmAppendOptions;
  retriesStarted: number;
  timer: ReturnType<typeof setTimeout> | null;
}

let retryState: RetryState | null = null;

export function getFmSessionState(): FmSessionStatus {
  return {
    generation: fmGeneration,
    pending: inflight !== null,
    exhausted: retryExhausted,
  };
}

/** Test-only: reset module-level session state between cases. */
export function __resetFmSessionForTests(): void {
  if (retryState?.timer) clearTimeout(retryState.timer);
  fmGeneration = 0;
  lastQueueRef = null;
  retryExhausted = false;
  inflight = null;
  retryState = null;
  disposeEpoch = 0;
}

/**
 * Tear down the FM session on app exit / HMR: cancel the bounded retry timer
 * and invalidate pending retry callbacks (fmGeneration) and any in-flight fetch
 * (disposeEpoch) so neither can append to a queue being torn down. A later
 * explicit append call may start a fresh round.
 */
export function disposeFmSession(): void {
  cancelScheduledRetry();
  fmGeneration += 1;
  disposeEpoch += 1;
  inflight = null;
}

function extractSongList(payload: any): any[] {
  const data = payload?.data?.data || payload?.data || payload || {};
  const list = data.song_list || data.info || data.list || data.songs || [];
  return Array.isArray(list) ? list : [];
}

function isPersonalFmFailure(payload: any): boolean {
  return payload?.status === 0;
}

function cancelScheduledRetry(): void {
  if (retryState?.timer) clearTimeout(retryState.timer);
  retryState = null;
}

function scheduleRetry(
  deps: FmSessionDeps,
  queueRef: Track[],
  generation: number,
  currentTrack: Track | null,
  currentIndex: number,
  retriesStarted: number,
  options: PersonalFmAppendOptions,
): void {
  if (retriesStarted >= AUTO_RETRY_DELAYS_MS.length) {
    retryExhausted = true;
    retryState = null;
    return;
  }

  if (retryState?.timer) clearTimeout(retryState.timer);
  const next: RetryState = {
    generation,
    queueRef,
    currentTrack,
    currentTrackKey: currentTrack?.FileHash || '',
    currentIndex,
    deps,
    options,
    retriesStarted: retriesStarted + 1,
    timer: null,
  };
  next.timer = setTimeout(() => {
    if (retryState !== next) return;
    next.timer = null;
    const latest = deps.getState();
    if (
      latest.queue !== next.queueRef
      || latest.queueMode !== 'personalFm'
      || fmGeneration !== next.generation
      || (next.currentTrackKey
        ? latest.currentTrack?.FileHash !== next.currentTrackKey
        : latest.currentTrack !== next.currentTrack)
      || latest.currentIndex !== next.currentIndex
    ) {
      retryState = null;
      return;
    }
    void appendPersonalFmRecommendationsInternal(
      deps,
      next.retriesStarted,
      true,
      next.options,
    ).catch(() => {
      // The internal request records and bounds its own failure path.
    });
  }, AUTO_RETRY_DELAYS_MS[retriesStarted]);
  retryState = next;
}

async function fetchPersonalFmRecommendations(
  query: Record<string, string | number>,
): Promise<any> {
  return apiGet<any>('/personal/fm', query);
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
 * - Empty/deduped and transport failures schedule a bounded, non-blocking
 *   retry round. A later explicit request may start a fresh round.
 */
export async function appendPersonalFmRecommendations(
  deps: FmSessionDeps,
  options: PersonalFmAppendOptions = {},
): Promise<boolean> {
  return appendPersonalFmRecommendationsInternal(deps, 0, false, options);
}

async function appendPersonalFmRecommendationsInternal(
  deps: FmSessionDeps,
  retriesStarted: number,
  automaticRetry: boolean,
  options: PersonalFmAppendOptions,
): Promise<boolean> {
  const state = deps.getState();
  if (state.queueMode !== 'personalFm') return false;

  // A new queue array identity => a new FM session.
  if (lastQueueRef !== state.queue) {
    cancelScheduledRetry();
    fmGeneration += 1;
    retryExhausted = false;
    inflight = null;
    lastQueueRef = state.queue;
  }

  // Coalesce concurrent calls within the same session into one fetch.
  if (inflight && inflight.queueRef === state.queue) {
    return inflight.promise;
  }

  // A new explicit operation may replace a waiting retry, but automatic
  // retries must be allowed to finish their bounded budget.
  if (!automaticRetry && retryState?.timer) cancelScheduledRetry();

  if (!automaticRetry) {
    retryExhausted = false;
  }

  const queueRef = state.queue;
  const current = state.currentTrack;
  const remain = Math.max(0, state.queue.length - state.currentIndex - 1);
  const trackKey = current?.FileHash || '';
  const startDisposeEpoch = disposeEpoch;

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
      scheduleRetry(
        deps,
        queueRef,
        fmGeneration,
        current,
        state.currentIndex,
        retriesStarted,
        options,
      );
      return false;
    }

    // Session superseded (queue replaced, or left personalFm) while fetching,
    // or the FM session was disposed (exit / HMR) mid-fetch.
    const latest = deps.getState();
    if (disposeEpoch !== startDisposeEpoch || latest.queue !== queueRef || latest.queueMode !== 'personalFm') {
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
      scheduleRetry(
        deps,
        queueRef,
        fmGeneration,
        current,
        state.currentIndex,
        retriesStarted,
        options,
      );
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
      playbackDiagnostics.recordEvent({
        kind: 'fm_fetch',
        phase: 'noop',
        detail: 'no fresh songs after dedupe',
        trackKey,
      });
      scheduleRetry(
        deps,
        queueRef,
        fmGeneration,
        current,
        state.currentIndex,
        retriesStarted,
        options,
      );
      return false;
    }

    retryExhausted = false;
    cancelScheduledRetry();
    latest.queue.push(...fresh);
    deps.saveQueue();
    playbackDiagnostics.recordEvent({
      kind: 'fm_fetch',
      phase: 'ok',
      detail: `appended ${fresh.length} songs`,
      trackKey,
    });
    if (automaticRetry) {
      try {
        options.onRetrySuccess?.({
          generation: fmGeneration,
          queueRef,
          appendedCount: fresh.length,
        });
      } catch {
        // Recovery notification must not turn a successful append into a fail.
      }
    }
    return true;
  })();

  inflight = { generation: fmGeneration, queueRef, promise };
  void promise.then(
    () => {
      if (inflight?.promise === promise) inflight = null;
    },
    () => {
      if (inflight?.promise === promise) inflight = null;
    },
  );
  return promise;
}
