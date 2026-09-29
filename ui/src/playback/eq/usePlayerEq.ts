/**
 * Web Audio EQ leaf extracted from playerStore.
 * Owns eqState, graph lifecycle, and Html5 backend EQ hooks.
 * playerStore remains the stable barrel for public exports.
 *
 * EQ consumes explicit ports (PlayerEqDeps) instead of a Store slice: it has
 * no import on playerStore, and the audio element comes from MediaRuntime via
 * the getAudio port rather than from reactive state.
 */
import { reactive } from 'vue';
import { WebAudioEq } from './webAudioEq';
import { prepareAudioSourceUrl } from '../../platform/tauri/audioProxy';
import { playbackDiagnostics } from '../playbackDiagnostics';

export interface PlayerEqDeps {
  getAudio: () => HTMLAudioElement | null;
  getVolume: () => number;
  getEqEnabled: () => boolean;
  getEqBands: () => number[];
}

const EQ_UNAVAILABLE_REASON =
  '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。';
const EQ_DEGRADED_REASON = 'EQ 暂不可用，点击重试';

function getAudioSource(element: HTMLAudioElement): string {
  return element.getAttribute('src') || element.currentSrc || element.src || '';
}

function isLocalAudioProxySource(src: string): boolean {
  if (!src) return false;
  try {
    const url = new URL(src, window.location.href);
    return (
      url.protocol === 'http:'
      && url.hostname === '127.0.0.1'
      && url.pathname.startsWith('/audio/')
    );
  } catch {
    return false;
  }
}

/**
 * Factory keeps EQ free of a hard import on playerStore (avoids circular init).
 * Wire with createPlayerEq({ getAudio, getVolume, getEqEnabled, getEqBands })
 * after playerStore and the MediaRuntime binding exist.
 */
export function createPlayerEq(deps: PlayerEqDeps) {
  // Routes <audio> through a BiquadFilter chain. KuGou CDN has no CORS;
  // only proxy-backed 127.0.0.1 media is rerouted (see webAudioEq.ts).
  const webAudioEq = new WebAudioEq(() => {
    const Ctx =
      window.AudioContext
      || (window as unknown as { webkitAudioContext?: typeof AudioContext })
        .webkitAudioContext;
    return Ctx ? new Ctx() : null;
  });

  let currentEqSafeSource = '';
  /** initPlayerBackend configures EQ intent; a graph is built only for a safe enabled source. */
  let eqGraphRequested = false;
  let graphLifecycleEpoch = 0;
  let sourceIntentEpoch = 0;
  let retryInFlight: Promise<void> | null = null;

  const eqState = reactive({
    available: false,
    reason: EQ_UNAVAILABLE_REASON,
    retryFailCount: 0,
    retryDisabled: false,
  });

  function __resetWebAudioEqForTests() {
    graphLifecycleEpoch += 1;
    sourceIntentEpoch += 1;
    currentEqSafeSource = '';
    webAudioEq.close();
    eqGraphRequested = false;
  }

  function closeWebAudioEq() {
    graphLifecycleEpoch += 1;
    sourceIntentEpoch += 1;
    currentEqSafeSource = '';
    webAudioEq.close();
    eqGraphRequested = false;
  }

  function currentWebAudioEqOptions() {
    return {
      enabled: deps.getEqEnabled(),
      bands: deps.getEqBands().slice(),
      onDegraded: () => {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
      },
      onRecovered: () => {
        syncEqAvailabilityFromReroute();
      },
    };
  }

  async function ensureWebAudioEqGraph(
    expectedLifecycleEpoch = graphLifecycleEpoch,
  ): Promise<boolean> {
    if (expectedLifecycleEpoch !== graphLifecycleEpoch) return false;
    eqGraphRequested = true;
    const requestedOptions = currentWebAudioEqOptions();
    webAudioEq.init(requestedOptions);
    try {
      await webAudioEq.awaitReady();
    } catch (error) {
      if (expectedLifecycleEpoch === graphLifecycleEpoch) {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
        recordEqEvent(
          'fail',
          `graph_init_reject err=${error instanceof Error ? error.message : String(error)}`,
        );
      }
      return false;
    }
    if (expectedLifecycleEpoch !== graphLifecycleEpoch || !eqGraphRequested) return false;
    if (!webAudioEq.isGraphReady) {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      return false;
    }

    // Settings can change while the worklet module is loading. Apply the
    // latest enabled/band state before the first source is attached.
    const latestOptions = currentWebAudioEqOptions();
    if (
      requestedOptions.enabled !== latestOptions.enabled
      || requestedOptions.bands.length !== latestOptions.bands.length
      || requestedOptions.bands.some((gain, index) => gain !== latestOptions.bands[index])
    ) {
      webAudioEq.init(latestOptions);
    }
    return true;
  }

  function syncEqAvailabilityFromReroute() {
    eqState.available = webAudioEq.isRerouted;
    eqState.reason = eqState.available ? '' : EQ_UNAVAILABLE_REASON;
  }

  async function preparePlaybackAudioSourceUrl(url: string) {
    const lifecycleEpoch = graphLifecycleEpoch;
    const intentEpoch = sourceIntentEpoch;
    if (!deps.getEqEnabled()) {
      return { url, crossOriginSafe: false };
    }
    const prepared = await prepareAudioSourceUrl(url);
    if (
      prepared.crossOriginSafe
      && lifecycleEpoch === graphLifecycleEpoch
      && intentEpoch === sourceIntentEpoch
      && deps.getEqEnabled()
    ) {
      // Build before audio.play() so the first safe track still starts with
      // EQ ready; avoid constructing AudioContext/worklet on idle startup.
      await ensureWebAudioEqGraph(lifecycleEpoch);
    }
    return prepared;
  }

  /** Preserve EQ intent during app startup; defer the graph until playback needs it. */
  function initWebAudioEQ() {
    if (eqGraphRequested) webAudioEq.init(currentWebAudioEqOptions());
  }

  function restoreElementVolume(element: HTMLAudioElement) {
    element.volume = deps.getVolume();
  }

  function isCurrentAudioIntent(
    epoch: number,
    audio: HTMLAudioElement,
    source: string,
  ): boolean {
    return (
      epoch === sourceIntentEpoch
      && deps.getEqEnabled()
      && deps.getAudio() === audio
      && getAudioSource(audio) === source
    );
  }

  function recordEqEvent(
    phase: 'start' | 'ok' | 'fail' | 'noop',
    detail: string,
  ) {
    playbackDiagnostics.recordEvent({
      kind: 'eq',
      phase,
      detail,
    });
  }

  /** Post-play attach: captureStream → worklet (spec §5.2). Skips when not CORS-safe. */
  async function attachWebAudioEqSource(
    element: HTMLAudioElement,
    crossOriginSafe = false,
    isCurrent: () => boolean = () => true,
  ) {
    if (!isCurrent()) return;
    const operationEpoch = ++sourceIntentEpoch;
    const lifecycleEpoch = graphLifecycleEpoch;
    const source = getAudioSource(element);
    if (!crossOriginSafe) {
      currentEqSafeSource = '';
      eqState.available = false;
      eqState.reason = EQ_UNAVAILABLE_REASON;
      restoreElementVolume(element);
      recordEqEvent('noop', `cors_unsafe volume=${deps.getVolume()}`);
      return;
    }

    const graphReady = await ensureWebAudioEqGraph(lifecycleEpoch);
    const ownsOperation = () => (
      operationEpoch === sourceIntentEpoch
      && lifecycleEpoch === graphLifecycleEpoch
      && deps.getEqEnabled()
      && getAudioSource(element) === source
      && isCurrent()
    );
    if (!ownsOperation()) return;
    if (!graphReady) {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      restoreElementVolume(element);
      recordEqEvent('fail', `graph_unavailable volume=${element.volume}`);
      return;
    }
    currentEqSafeSource = source;

    const volumeBefore = element.volume;
    try {
      await webAudioEq.resume();
    } catch (e) {
      if (!ownsOperation()) return;
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      restoreElementVolume(element);
      recordEqEvent(
        'fail',
        `resume_reject ctx=${webAudioEq.contextState} volume_before=${volumeBefore} volume_after=${element.volume} err=${e instanceof Error ? e.message : String(e)}`,
      );
      return;
    }
    if (!ownsOperation()) return;
    if (webAudioEq.contextState !== 'running') {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      restoreElementVolume(element);
      recordEqEvent('fail', `ctx_not_running ctx=${webAudioEq.contextState} volume=${element.volume}`);
      return;
    }

    const attached = webAudioEq.attachSource(element, deps.getVolume());
    const leaseId = webAudioEq.currentLeaseId;
    if (!attached) {
      restoreElementVolume(element);
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      recordEqEvent('fail', `attach_false lease=${leaseId} volume=${element.volume}`);
      return;
    }
    if (!ownsOperation()) {
      webAudioEq.releaseLease(leaseId);
      recordEqEvent('noop', `stale_after_attach released_lease=${leaseId}`);
      return;
    }
    syncEqAvailabilityFromReroute();
    if (!ownsOperation()) {
      webAudioEq.releaseLease(leaseId);
      recordEqEvent('noop', `stale_after_sync released_lease=${leaseId}`);
      return;
    }
    setWebAudioEqVolume(deps.getVolume());
    recordEqEvent(
      'ok',
      `attached lease=${leaseId} proxy=${isLocalAudioProxySource(getAudioSource(element))} ctx=${webAudioEq.contextState} volume_before=${volumeBefore} volume_after=${element.volume}`,
    );
  }

  function disconnectWebAudioEqSource() {
    sourceIntentEpoch += 1;
    const leaseId = webAudioEq.currentLeaseId;
    currentEqSafeSource = '';
    webAudioEq.disconnectSource();
    syncEqAvailabilityFromReroute();
    recordEqEvent('ok', `disconnect lease=${leaseId} ctx=${webAudioEq.contextState}`);
  }

  function setWebAudioEqVolume(vol: number) {
    webAudioEq.setVolume(vol);
  }

  function setWebAudioEqBand(index: number, gainDb: number) {
    webAudioEq.setBand(index, gainDb, deps.getEqEnabled());
  }

  function setWebAudioEqEnabled(enabled: boolean) {
    if (eqGraphRequested) webAudioEq.setEnabled(enabled, deps.getEqBands());
    if (!enabled) {
      sourceIntentEpoch += 1;
      webAudioEq.disconnectSource();
      const audio = deps.getAudio();
      if (audio) audio.volume = deps.getVolume();
      syncEqAvailabilityFromReroute();
      return;
    }
    const audio = deps.getAudio();
    const source = audio ? getAudioSource(audio) : '';
    const sourceSafe =
      !!source
      && (source === currentEqSafeSource || isLocalAudioProxySource(source));
    if (audio && !webAudioEq.isRerouted && sourceSafe) {
      void attachWebAudioEqSource(audio, true);
    } else {
      syncEqAvailabilityFromReroute();
    }
  }

  /** Resume the AudioContext after a user gesture (autoplay policy). */
  function resumeAudioContext() {
    // The HTML5 play event also fires for direct/unsafe sources. There is no
    // EQ context to resume in that case, and a missing context is not an EQ
    // degradation.
    if (!eqGraphRequested || !webAudioEq.isGraphReady) return;
    const audio = deps.getAudio();
    if (!audio) return;
    const operationEpoch = sourceIntentEpoch;
    const source = getAudioSource(audio);
    void webAudioEq.resume().then(() => {
      if (!isCurrentAudioIntent(operationEpoch, audio, source)) return;
      if (webAudioEq.contextState !== 'running') {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
        audio.volume = deps.getVolume();
        if (webAudioEq.isRerouted) {
          webAudioEq.enterDegradation(audio, deps.getVolume());
        }
      }
    }).catch(() => {
      if (!isCurrentAudioIntent(operationEpoch, audio, source)) return;
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      audio.volume = deps.getVolume();
      if (webAudioEq.isRerouted) {
        webAudioEq.enterDegradation(audio, deps.getVolume());
      }
    });
  }

  /** Retry EQ after suspend degradation (spec §4.4, §6.3). */
  async function runRetryEq() {
    const audio = deps.getAudio();
    if (eqState.retryDisabled || !audio || !deps.getEqEnabled()) return;
    const source = getAudioSource(audio);
    const sourceSafe = !!source && (
      source === currentEqSafeSource || isLocalAudioProxySource(source)
    );
    if (!sourceSafe) return;

    const operationEpoch = ++sourceIntentEpoch;
    const lifecycleEpoch = graphLifecycleEpoch;
    try {
      const graphReady = await ensureWebAudioEqGraph(lifecycleEpoch);
      if (!isCurrentAudioIntent(operationEpoch, audio, source)) return;
      if (!graphReady) throw new Error('EQ graph unavailable');
      await webAudioEq.resume();
      if (!isCurrentAudioIntent(operationEpoch, audio, source)) return;
      if (webAudioEq.contextState !== 'running') {
        throw new Error('AudioContext not running');
      }
      const recovered = webAudioEq.recoverFromDegradation(audio, deps.getVolume());
      if (!recovered) throw new Error('eq recover failed');
      if (!isCurrentAudioIntent(operationEpoch, audio, source)) {
        webAudioEq.releaseLease(webAudioEq.currentLeaseId);
        return;
      }
      currentEqSafeSource = source;
      eqState.available = true;
      eqState.reason = '';
      eqState.retryFailCount = 0;
    } catch {
      if (!isCurrentAudioIntent(operationEpoch, audio, source)) return;
      audio.volume = deps.getVolume();
      eqState.retryFailCount++;
      if (eqState.retryFailCount >= 3) {
        eqState.retryDisabled = true;
      }
    }
  }

  function retryEq(): Promise<void> {
    if (retryInFlight) return retryInFlight;
    const pending = runRetryEq().finally(() => {
      if (retryInFlight === pending) retryInFlight = null;
    });
    retryInFlight = pending;
    return pending;
  }

  /**
   * Single owner of Html5 backend EQ hooks — dedupes the initEq branch
   * that previously lived inline in initPlayerBackend.
   */
  function makeBackendEqHooks() {
    return {
      prepareSourceUrl: preparePlaybackAudioSourceUrl,
      initEq: async (
        element: HTMLAudioElement,
        crossOriginSafe: boolean,
        isCurrent: () => boolean,
      ) => {
        if (!isCurrent()) return;
        if (!deps.getEqEnabled()) {
          if (!isCurrent()) return;
          currentEqSafeSource = crossOriginSafe ? getAudioSource(element) : '';
          if (!isCurrent()) return;
          eqState.available = false;
          eqState.reason = EQ_UNAVAILABLE_REASON;
          if (!isCurrent()) return;
          element.volume = deps.getVolume();
          return;
        }
        await attachWebAudioEqSource(element, crossOriginSafe, isCurrent);
      },
      disconnectEq: disconnectWebAudioEqSource,
      isEqRerouted: () => webAudioEq.isRerouted,
      setEqVolume: setWebAudioEqVolume,
    };
  }

  function resetRetryState() {
    eqState.retryFailCount = 0;
    eqState.retryDisabled = false;
  }

  return {
    eqState,
    __resetWebAudioEqForTests,
    closeWebAudioEq,
    initWebAudioEQ,
    attachWebAudioEqSource,
    disconnectWebAudioEqSource,
    setWebAudioEqVolume,
    setWebAudioEqBand,
    setWebAudioEqEnabled,
    resumeAudioContext,
    retryEq,
    makeBackendEqHooks,
    resetRetryState,
  };
}

export type PlayerEqApi = ReturnType<typeof createPlayerEq>;
