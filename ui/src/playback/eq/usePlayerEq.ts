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

  const eqState = reactive({
    available: false,
    reason: EQ_UNAVAILABLE_REASON,
    retryFailCount: 0,
    retryDisabled: false,
  });

  function __resetWebAudioEqForTests() {
    webAudioEq.close();
  }

  function closeWebAudioEq() {
    webAudioEq.close();
  }

  function syncEqAvailabilityFromReroute() {
    eqState.available = webAudioEq.isRerouted;
    eqState.reason = eqState.available ? '' : EQ_UNAVAILABLE_REASON;
  }

  async function preparePlaybackAudioSourceUrl(url: string) {
    if (!deps.getEqEnabled()) {
      return { url, crossOriginSafe: false };
    }
    return prepareAudioSourceUrl(url);
  }

  /** Build the long-lived worklet graph once at app startup (spec §5.1). */
  function initWebAudioEQ() {
    webAudioEq.init({
      enabled: deps.getEqEnabled(),
      bands: deps.getEqBands(),
      onDegraded: () => {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
      },
      onRecovered: () => {
        syncEqAvailabilityFromReroute();
      },
    });
  }

  function restoreElementVolume(element: HTMLAudioElement) {
    element.volume = deps.getVolume();
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
    currentEqSafeSource = crossOriginSafe ? getAudioSource(element) : '';
    if (!crossOriginSafe) {
      eqState.available = false;
      eqState.reason = EQ_UNAVAILABLE_REASON;
      restoreElementVolume(element);
      recordEqEvent('noop', `cors_unsafe volume=${deps.getVolume()}`);
      return;
    }

    await webAudioEq.awaitReady();
    if (!isCurrent()) return;

    const volumeBefore = element.volume;
    try {
      await webAudioEq.resume();
    } catch (e) {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      restoreElementVolume(element);
      recordEqEvent(
        'fail',
        `resume_reject ctx=${webAudioEq.contextState} volume_before=${volumeBefore} volume_after=${element.volume} err=${e instanceof Error ? e.message : String(e)}`,
      );
      return;
    }
    if (!isCurrent()) return;
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
    if (!isCurrent()) {
      webAudioEq.releaseLease(leaseId);
      recordEqEvent('noop', `stale_after_attach released_lease=${leaseId}`);
      return;
    }
    syncEqAvailabilityFromReroute();
    if (!isCurrent()) {
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
    webAudioEq.setEnabled(enabled, deps.getEqBands());
    if (!enabled) {
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
    void webAudioEq.resume().then(() => {
      const audio = deps.getAudio();
      if (webAudioEq.contextState !== 'running') {
        if (audio) {
          eqState.available = false;
          eqState.reason = EQ_DEGRADED_REASON;
          audio.volume = deps.getVolume();
          if (webAudioEq.isRerouted) {
            webAudioEq.enterDegradation(audio, deps.getVolume());
          }
        }
      }
    }).catch(() => {
      const audio = deps.getAudio();
      if (audio) {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
        audio.volume = deps.getVolume();
        if (webAudioEq.isRerouted) {
          webAudioEq.enterDegradation(audio, deps.getVolume());
        }
      }
    });
  }

  /** Retry EQ after suspend degradation (spec §4.4, §6.3). */
  async function retryEq() {
    if (eqState.retryDisabled || !deps.getAudio()) return;
    try {
      await webAudioEq.resume();
      if (webAudioEq.contextState !== 'running') {
        throw new Error('AudioContext not running');
      }
      const audio = deps.getAudio();
      const recovered = audio
        ? webAudioEq.recoverFromDegradation(audio, deps.getVolume())
        : false;
      if (!recovered) throw new Error('eq recover failed');
      eqState.available = true;
      eqState.reason = '';
      eqState.retryFailCount = 0;
    } catch {
      const audio = deps.getAudio();
      if (audio) audio.volume = deps.getVolume();
      eqState.retryFailCount++;
      if (eqState.retryFailCount >= 3) {
        eqState.retryDisabled = true;
      }
    }
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
