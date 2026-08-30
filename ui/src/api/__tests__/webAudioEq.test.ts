import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { Html5AudioBackend } from '../html5Backend';
import { WebAudioEq, type EqOptions } from '../webAudioEq';

/**
 * Phase 2 tests for the rewritten WebAudioEq (AudioWorklet + captureStream path).
 *
 * jsdom has no Web Audio API, no AudioWorkletNode, no captureStream. We stub
 * the global constructors + HTMLAudioElement.prototype.captureStream per the
 * plan (§7.2, R2.2). The mock factory below builds a recording mock ctx +
 * worklet node + gain node + media stream source so we can assert graph
 * topology, postMessage payloads, and the §4.2 re-entrancy / resource-release
 * contracts.
 */

// ---------------------------------------------------------------------------
// Mock factories
// ---------------------------------------------------------------------------

interface MockTrack {
  stop: ReturnType<typeof vi.fn>;
  kind: string;
  readyState: string;
}

interface MockStream {
  getAudioTracks: ReturnType<typeof vi.fn>;
  getVideoTracks: ReturnType<typeof vi.fn>;
  _tracks: MockTrack[];
}

interface MockSourceNode {
  connect: ReturnType<typeof vi.fn>;
  disconnect: ReturnType<typeof vi.fn>;
  _stream: MockStream;
  _disconnectTarget?: unknown;
}

interface MockGainParam {
  value: number;
  cancelScheduledValues: ReturnType<typeof vi.fn>;
  setValueAtTime: ReturnType<typeof vi.fn>;
  linearRampToValueAtTime: ReturnType<typeof vi.fn>;
}

interface MockGainNode {
  connect: ReturnType<typeof vi.fn>;
  disconnect: ReturnType<typeof vi.fn>;
  gain: MockGainParam;
}

interface MockWorkletNode {
  port: { postMessage: ReturnType<typeof vi.fn>; onmessage: unknown; close: ReturnType<typeof vi.fn> };
  connect: ReturnType<typeof vi.fn>;
  disconnect: ReturnType<typeof vi.fn>;
  /** Tracks how many source nodes are currently connected as inputs. */
  _inputs: MockSourceNode[];
}

interface MockCtx {
  state: string;
  currentTime: number;
  destination: { connect: ReturnType<typeof vi.fn>; disconnect: ReturnType<typeof vi.fn> };
  resume: ReturnType<typeof vi.fn>;
  close: ReturnType<typeof vi.fn>;
  createGain: ReturnType<typeof vi.fn>;
  createMediaStreamSource: ReturnType<typeof vi.fn>;
  createMediaElementSource: ReturnType<typeof vi.fn>;
  createBiquadFilter: ReturnType<typeof vi.fn>;
  audioWorklet: { addModule: ReturnType<typeof vi.fn> };
  sampleRate: number;
  _gainNode: MockGainNode;
  _workletNode: MockWorkletNode;
  _sourceNodes: MockSourceNode[];
}

function makeMockTrack(): MockTrack {
  return { stop: vi.fn(), kind: 'audio', readyState: 'live' };
}

function makeMockStream(): MockStream {
  const tracks = [makeMockTrack()];
  return {
    getAudioTracks: vi.fn(() => tracks),
    getVideoTracks: vi.fn(() => []),
    _tracks: tracks,
  };
}

/**
 * Build a recording mock AudioContext + the global AudioWorkletNode constructor
 * + HTMLAudioElement.prototype.captureStream stub. Returns handles to every
 * mock node so tests can assert call counts and payloads.
 *
 * The workletNode._inputs array tracks source-node connect/disconnect so the
 * §4.2 re-entrancy contract ("workletNode input count == 1 after 2x
 * attachSource") can be asserted directly.
 */
function setupMocks(opts: { workletAddModuleRejects?: boolean } = {}) {
  const createdCtxs: MockCtx[] = [];
  const createdWorkletNodes: MockWorkletNode[] = [];
  const createdStreams: MockStream[] = [];

  // Per-ctx gain + worklet nodes. The workletNode is created by the global
  // AudioWorkletNode constructor (stubbed below), so we need a way to hand it
  // back. We stash it on the ctx via a closure.
  let pendingWorkletNode: MockWorkletNode | null = null;

  /** vi.fn() cannot be used with `new`; use a real constructor that returns the pending node. */
  function AudioWorkletNodeCtor(this: unknown, _ctx: unknown, _name: string) {
    if (!pendingWorkletNode) {
      throw new Error('test setup: pendingWorkletNode not set before AudioWorkletNode ctor');
    }
    return pendingWorkletNode;
  }
  const audioWorkletNodeCtorSpy = vi.fn(AudioWorkletNodeCtor);

  function makeMockCtx(): MockCtx {
    const gainParam: MockGainParam = {
      value: 1.0,
      cancelScheduledValues: vi.fn(function (this: MockGainParam) { return this; }),
      setValueAtTime: vi.fn(function (this: MockGainParam, v: number) {
        this.value = v;
        return this;
      }),
      linearRampToValueAtTime: vi.fn(function (this: MockGainParam, v: number) {
        this.value = v;
        return this;
      }),
    };
    const gainNode: MockGainNode = {
      connect: vi.fn((dest: unknown) => dest),
      disconnect: vi.fn(),
      gain: gainParam,
    };
    const workletNode: MockWorkletNode = {
      port: { postMessage: vi.fn(), onmessage: null, close: vi.fn() },
      connect: vi.fn((dest: unknown) => dest),
      disconnect: vi.fn(),
      _inputs: [],
    };
    createdWorkletNodes.push(workletNode);

    const sourceNodes: MockSourceNode[] = [];
    const ctx: MockCtx = {
      state: 'running',
      currentTime: 0,
      destination: { connect: vi.fn(), disconnect: vi.fn() },
      resume: vi.fn(async () => { ctx.state = 'running'; }),
      close: vi.fn(async () => { ctx.state = 'closed'; }),
      createGain: vi.fn(() => gainNode),
      createMediaStreamSource: vi.fn((_stream: MockStream) => {
        const sn: MockSourceNode = {
          connect: vi.fn((dest: unknown) => {
            if (dest === workletNode) {
              workletNode._inputs.push(sn);
            }
            return dest;
          }),
          disconnect: vi.fn((target?: unknown) => {
            sn._disconnectTarget = target;
            const idx = workletNode._inputs.indexOf(sn);
            if (idx >= 0) workletNode._inputs.splice(idx, 1);
          }),
          _stream: _stream,
        };
        sourceNodes.push(sn);
        return sn;
      }),
      createMediaElementSource: vi.fn(() => ({
        connect: vi.fn(() => ({})),
      })),
      createBiquadFilter: vi.fn(() => ({
        connect: vi.fn(() => ({})),
        type: '',
        frequency: { value: 0 },
        Q: { value: 0 },
        gain: { value: 0 },
      })),
      audioWorklet: {
        addModule: opts.workletAddModuleRejects
          ? vi.fn(async () => { throw new Error('worklet load failed (test)'); })
          : vi.fn(async () => {}),
      },
      sampleRate: 48000,
      _gainNode: gainNode,
      _workletNode: workletNode,
      _sourceNodes: sourceNodes,
    };
    createdCtxs.push(ctx);

    // Stash the workletNode so the AudioWorkletNode ctor stub can return it
    // when the code under test does `new AudioWorkletNode(ctx, 'eq-processor')`.
    pendingWorkletNode = workletNode;
    return ctx;
  }

  // Stub the global AudioWorkletNode constructor.
  vi.stubGlobal('AudioWorkletNode', audioWorkletNodeCtorSpy);

  // Stub URL.createObjectURL / revokeObjectURL (loadEqWorklet uses them).
  const createObjectURLSpy = vi.fn((_blob: Blob) => 'blob:fake-url-' + Math.random());
  const revokeObjectURLSpy = vi.fn();
  vi.stubGlobal('URL', {
    createObjectURL: createObjectURLSpy,
    revokeObjectURL: revokeObjectURLSpy,
  });
  // Blob needs to exist for loadEqWorklet. jsdom has it, but the URL stub
  // above replaces the whole URL object — keep Blob from the real global.
  // (jsdom provides Blob; we don't stub it.)

  // Stub HTMLAudioElement.prototype.captureStream so attachSource can call it.
  const captureStreamImpl = vi.fn(() => {
    const s = makeMockStream();
    createdStreams.push(s);
    return s;
  });
  // Save the original so we can restore. jsdom doesn't have captureStream, so
  // it's undefined originally.
  const origCaptureStream = (HTMLAudioElement.prototype as unknown as { captureStream?: unknown }).captureStream;
  (HTMLAudioElement.prototype as unknown as { captureStream: unknown }).captureStream = captureStreamImpl;

  function teardown() {
    vi.unstubAllGlobals();
    if (origCaptureStream === undefined) {
      delete (HTMLAudioElement.prototype as unknown as { captureStream?: unknown }).captureStream;
    } else {
      (HTMLAudioElement.prototype as unknown as { captureStream: unknown }).captureStream = origCaptureStream;
    }
  }

  return {
    makeMockCtx,
    AudioWorkletNodeCtor: audioWorkletNodeCtorSpy,
    createdCtxs,
    createdWorkletNodes,
    createdStreams,
    createObjectURLSpy,
    revokeObjectURLSpy,
    captureStreamImpl,
    teardown,
  };
}

/** Make a minimal mock HTMLAudioElement for attachSource tests. */
function makeMockAudio(
  captureStreamImpl?: () => MockStream,
): HTMLAudioElement {
  return {
    volume: 1.0,
    crossOrigin: null,
    src: '',
    captureStream: () =>
      (captureStreamImpl ? captureStreamImpl() : makeMockStream()) as unknown as MediaStream,
  } as unknown as HTMLAudioElement;
}

function mockCtxFactory(ctx: MockCtx) {
  return () => ctx as unknown as import('../webAudioEq').AudioContextLike;
}

/** init() kicks off async buildGraph; tests must await this before asserting graph state. */
async function initAndReady(eq: WebAudioEq, opts: EqOptions): Promise<void> {
  eq.init(opts);
  await eq.awaitReady();
}

// ---------------------------------------------------------------------------
// Step 2.1 — init graph construction (RED)
// ---------------------------------------------------------------------------

describe('WebAudioEq (new path) — Step 2.1: init graph construction', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    mocks = setupMocks();
  });
  afterEach(() => mocks.teardown());

  it('creates exactly one AudioContext via the factory', async () => {
    const ctx = mocks.makeMockCtx();
    const factory = vi.fn(mockCtxFactory(ctx));
    const eq = new WebAudioEq(factory);
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(factory).toHaveBeenCalledTimes(1);
  });

  it('calls audioWorklet.addModule once with a blob: URL', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(ctx.audioWorklet.addModule).toHaveBeenCalledTimes(1);
    const arg = ctx.audioWorklet.addModule.mock.calls[0][0] as string;
    expect(typeof arg).toBe('string');
    expect(arg.startsWith('blob:')).toBe(true);
  });

  it('constructs exactly one AudioWorkletNode(ctx, "eq-processor")', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(mocks.AudioWorkletNodeCtor).toHaveBeenCalledTimes(1);
    expect(mocks.AudioWorkletNodeCtor.mock.calls[0][0]).toBe(ctx);
    expect(mocks.AudioWorkletNodeCtor.mock.calls[0][1]).toBe('eq-processor');
  });

  it('creates exactly one GainNode via ctx.createGain', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(ctx.createGain).toHaveBeenCalledTimes(1);
  });

  it('connects workletNode -> gainNode -> ctx.destination', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    const { _workletNode, _gainNode } = ctx;
    // workletNode.connect(gainNode)
    expect(_workletNode.connect).toHaveBeenCalledWith(_gainNode);
    // gainNode.connect(ctx.destination)
    expect(_gainNode.connect).toHaveBeenCalledWith(ctx.destination);
  });

  it('postMessages initial setBands + setEnabled to the worklet port', async () => {
    const ctx = mocks.makeMockCtx();
    const bands = [1, -2, 3, 0, 0, 0, 0, 0, 0, 0];
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands });
    const port = ctx._workletNode.port;
    expect(port.postMessage).toHaveBeenCalledWith({ type: 'setBands', bands });
    expect(port.postMessage).toHaveBeenCalledWith({ type: 'setEnabled', enabled: true });
  });

  it('does NOT call createMediaElementSource (spec §3.1 invariant)', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(ctx.createMediaElementSource).not.toHaveBeenCalled();
  });

  it('init does NOT receive an audio element (new signature: init(opts))', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
    // The new init takes a single EqOptions argument — no audio element.
await initAndReady(eq, { enabled: false, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    // Sanity: captureStream was never called because init doesn't attach a source.
    expect(mocks.captureStreamImpl).not.toHaveBeenCalled();
  });

  it('isRerouted is false after init (no source attached yet)', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(eq.isRerouted).toBe(false);
  });

  it('fires onDegraded when worklet addModule rejects', async () => {
    const mocks2 = setupMocks({ workletAddModuleRejects: true });
    try {
      const ctx = mocks2.makeMockCtx();
      const onDegraded = vi.fn();
      const eq = new WebAudioEq(mockCtxFactory(ctx));
  await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], onDegraded });
      expect(onDegraded).toHaveBeenCalledTimes(1);
    } finally {
      mocks2.teardown();
    }
  });
});

// ---------------------------------------------------------------------------
// Step 2.3 — attachSource + re-entrancy contract
// ---------------------------------------------------------------------------

describe('WebAudioEq (new path) — Step 2.3: attachSource + re-entrancy', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    mocks = setupMocks();
  });
  afterEach(() => mocks.teardown());

  async function makeInitializedEq() {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    return { eq, ctx };
  }

  it('attachSource sets audio.volume=0, calls captureStream, createMediaStreamSource, connect to workletNode', async () => {
    const { eq, ctx } = await makeInitializedEq();
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.8;
    expect(eq.attachSource(audio, 0.8)).toBe(true);
    expect(audio.volume).toBe(0);
    expect(mocks.captureStreamImpl).toHaveBeenCalledTimes(1);
    expect(ctx.createMediaStreamSource).toHaveBeenCalledTimes(1);
    const sourceNode = ctx._sourceNodes[0];
    expect(sourceNode.connect).toHaveBeenCalledWith(ctx._workletNode);
    expect(eq.isRerouted).toBe(true);
  });

  it('re-entrancy: 2x attachSource without disconnectSource — first sourceNode.disconnect called, first stream tracks stopped, workletNode input count == 1, captureStream called 2x', async () => {
    const { eq, ctx } = await makeInitializedEq();
    const audio1 = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    const audio2 = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);

    audio1.volume = 0.4;
    audio2.volume = 0.9;
    expect(eq.attachSource(audio1, 0.4)).toBe(true);
    const firstSourceNode = ctx._sourceNodes[0];
    const firstStream = mocks.createdStreams[0];
    const firstTracks = firstStream._tracks;

    expect(eq.attachSource(audio2, 0.9)).toBe(true);
    expect(audio1.volume).toBe(0.4);
    expect(audio2.volume).toBe(0);

    // First sourceNode.disconnect was called (re-entrancy self-cleanup).
    expect(firstSourceNode.disconnect).toHaveBeenCalled();
    // First stream's audio tracks were stopped.
    expect(firstTracks.length).toBe(1);
    expect(firstTracks[0].stop).toHaveBeenCalled();
    // workletNode input count is exactly 1 (old disconnected, new connected).
    expect(ctx._workletNode._inputs.length).toBe(1);
    // captureStream was called twice (once per attachSource).
    expect(mocks.captureStreamImpl).toHaveBeenCalledTimes(2);
    // isRerouted stays true.
    expect(eq.isRerouted).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Step 2.4 — disconnectSource resource release
// ---------------------------------------------------------------------------

describe('WebAudioEq (new path) — Step 2.4: disconnectSource resource release', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    mocks = setupMocks();
  });
  afterEach(() => mocks.teardown());

  async function makeInitializedEqWithSource() {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.73;
    expect(eq.attachSource(audio, 0.73)).toBe(true);
    const sourceNode = ctx._sourceNodes[0];
    const stream = mocks.createdStreams[0];
    return { eq, ctx, sourceNode, stream, audio };
  }

  it('disconnectSource disconnects sourceNode, stops all audio tracks, nulls refs, sets isRerouted=false', async () => {
    const { eq, sourceNode, stream, audio } = await makeInitializedEqWithSource();
    eq.disconnectSource();
    expect(sourceNode.disconnect).toHaveBeenCalled();
    expect(stream.getAudioTracks).toHaveBeenCalled();
    const tracks = stream._tracks;
    expect(tracks[0].stop).toHaveBeenCalled();
    expect(eq.isRerouted).toBe(false);
    expect(audio.volume).toBe(0.73);
  });

  it('disconnectSource is idempotent (2x calls do not throw)', async () => {
    const { eq } = await makeInitializedEqWithSource();
    expect(() => {
      eq.disconnectSource();
      eq.disconnectSource();
    }).not.toThrow();
  });

  it('disconnectSource before attachSource is a no-op (no throw)', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    expect(() => eq.disconnectSource()).not.toThrow();
    expect(eq.isRerouted).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Step 2.5 — setBand / setEnabled / setVolume
// ---------------------------------------------------------------------------

describe('WebAudioEq (new path) — Step 2.5: setBand / setEnabled / setVolume', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    mocks = setupMocks();
  });
  afterEach(() => mocks.teardown());

  async function makeInitializedEq(bands = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]) {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, { enabled: true, bands });
    return { eq, ctx };
  }

  it('setBand posts setBands with the updated bands array at the given index', async () => {
    const { eq, ctx } = await makeInitializedEq([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    ctx._workletNode.port.postMessage.mockClear();
    eq.setBand(2, 3.5, true);
    const expectedBands = [0, 0, 3.5, 0, 0, 0, 0, 0, 0, 0];
    expect(ctx._workletNode.port.postMessage).toHaveBeenCalledWith({
      type: 'setBands',
      bands: expectedBands,
    });
  });

  it('setBand with enabled=false does not postMessage (EQ disabled, no-op)', async () => {
    const { eq, ctx } = await makeInitializedEq();
    ctx._workletNode.port.postMessage.mockClear();
    eq.setBand(2, 3.5, false);
    expect(ctx._workletNode.port.postMessage).not.toHaveBeenCalled();
  });

  it('setEnabled posts setEnabled with the new enabled flag', async () => {
    const { eq, ctx } = await makeInitializedEq();
    ctx._workletNode.port.postMessage.mockClear();
    eq.setEnabled(false, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    expect(ctx._workletNode.port.postMessage).toHaveBeenCalledWith({
      type: 'setEnabled',
      enabled: false,
    });
  });

  it('setVolume writes gainNode.gain.value when isRerouted', async () => {
    const { eq, ctx } = await makeInitializedEq();
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    expect(eq.attachSource(audio, 1)).toBe(true);
    expect(eq.isRerouted).toBe(true);
    eq.setVolume(0.7);
    expect(ctx._gainNode.gain.value).toBe(0.7);
  });

  it('setVolume does NOT write gainNode when !isRerouted (degraded mode — backend.setVolume handles audio.volume)', async () => {
    const { eq, ctx } = await makeInitializedEq();
    // No attachSource → isRerouted is false.
    expect(eq.isRerouted).toBe(false);
    const before = ctx._gainNode.gain.value;
    eq.setVolume(0.4);
    expect(ctx._gainNode.gain.value).toBe(before);
  });
});

// ---------------------------------------------------------------------------
// Step 2.6 — resume + degradation trigger + close
// ---------------------------------------------------------------------------

describe('WebAudioEq (new path) — Step 2.6: resume / degradation / close', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    mocks = setupMocks();
  });
  afterEach(() => mocks.teardown());

  async function makeInitializedEq() {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
await initAndReady(eq, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      onDegraded: () => {},
    });
    return { eq, ctx };
  }

  it('resume success: ctx.resume resolves, onDegraded NOT called', async () => {
    await makeInitializedEq();
    const onDegraded = vi.fn();
    // Re-init with the spy so we can assert it wasn't called.
    const ctx2 = mocks.makeMockCtx();
    const eq2 = new WebAudioEq(mockCtxFactory(ctx2));
    await initAndReady(eq2, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      onDegraded,
    });
    ctx2.state = 'suspended';
    ctx2.resume = vi.fn(async () => { ctx2.state = 'running'; });
    await eq2.resume();
    expect(ctx2.resume).toHaveBeenCalledTimes(1);
    expect(onDegraded).not.toHaveBeenCalled();
  });

  it('resume failure: ctx.resume rejects (throws to caller)', async () => {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
    await initAndReady(eq, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    });
    ctx.state = 'suspended';
    ctx.resume = vi.fn(async () => {
      throw new Error('NotAllowedError: no user gesture');
    });
    await expect(eq.resume()).rejects.toThrow('NotAllowedError');
  });

  it('close: disconnects workletNode + gainNode, closes ctx, cleans source/stream', async () => {
    const { eq, ctx } = await makeInitializedEq();
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.61;
    expect(eq.attachSource(audio, 0.61)).toBe(true);
    const sourceNode = ctx._sourceNodes[0];
    const stream = mocks.createdStreams[0];

    eq.close();

    expect(sourceNode.disconnect).toHaveBeenCalled();
    expect(stream._tracks[0].stop).toHaveBeenCalled();
    expect(ctx._workletNode.disconnect).toHaveBeenCalled();
    expect(ctx._gainNode.disconnect).toHaveBeenCalled();
    expect(ctx.close).toHaveBeenCalled();
    expect(eq.isRerouted).toBe(false);
    expect(audio.volume).toBe(0.61);
  });

  it('close without attachSource: disconnects workletNode + gainNode, closes ctx (no throw)', async () => {
    const { eq, ctx } = await makeInitializedEq();
    expect(() => eq.close()).not.toThrow();
    expect(ctx._workletNode.disconnect).toHaveBeenCalled();
    expect(ctx._gainNode.disconnect).toHaveBeenCalled();
    expect(ctx.close).toHaveBeenCalled();
  });
});

// ---------------------------------------------------------------------------
// Phase 4 — degradation / recovery order (spec §3.3)
// ---------------------------------------------------------------------------

describe('WebAudioEq (new path) — Phase 4: degradation order (§3.3)', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    vi.useFakeTimers();
    mocks = setupMocks();
  });
  afterEach(() => {
    vi.useRealTimers();
    mocks.teardown();
  });

  async function makeReroutedEq(onDegraded = vi.fn(), onRecovered = vi.fn()) {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
    await initAndReady(eq, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      onDegraded,
      onRecovered,
    });
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.65;
    expect(eq.attachSource(audio, 0.65)).toBe(true);
    return { eq, ctx, audio, onDegraded, onRecovered };
  }

  it('enterDegradation: disconnect(workletNode) before audio.volume=vol, then onDegraded', async () => {
    const onDegraded = vi.fn();
    const { eq, ctx, audio } = await makeReroutedEq(onDegraded);
    const sourceNode = ctx._sourceNodes[0]!;
    const callOrder: string[] = [];
    sourceNode.disconnect.mockImplementation(() => {
      callOrder.push('disconnect');
    });
    Object.defineProperty(audio, 'volume', {
      configurable: true,
      get: () => 0,
      set: (v: number) => {
        callOrder.push(`volume=${v}`);
      },
    });

    eq.enterDegradation(audio, 0.65);
    expect(onDegraded).not.toHaveBeenCalled();
    expect(callOrder.indexOf('disconnect')).toBe(-1);
    vi.advanceTimersByTime(50);

    expect(callOrder.indexOf('disconnect')).toBeGreaterThanOrEqual(0);
    expect(callOrder.indexOf('disconnect')).toBeLessThan(callOrder.indexOf('volume=0.65'));
    expect(onDegraded).toHaveBeenCalledTimes(1);
    expect(eq.isRerouted).toBe(false);
  });

  it('enterDegradation ramps gainNode to 0 before unmute (spec §4.4)', async () => {
    const { eq, ctx, audio } = await makeReroutedEq();
    eq.setVolume(0.8);

    eq.enterDegradation(audio, 0.65);

    const { gain } = ctx._gainNode;
    expect(gain.cancelScheduledValues).toHaveBeenCalledWith(0);
    expect(gain.setValueAtTime).toHaveBeenCalledWith(0.8, 0);
    expect(gain.linearRampToValueAtTime).toHaveBeenCalledWith(0, 0.05);
  });

  it('enterDegradation stops all audio tracks on currentStream (spec §4.2)', async () => {
    const { eq, ctx } = await makeReroutedEq();
    const stream = mocks.createdStreams[0]!;
    const tracks = stream._tracks;

    eq.enterDegradation(makeMockAudio(), 0.7);
    vi.advanceTimersByTime(50);

    expect(tracks[0].stop).toHaveBeenCalled();
    expect(ctx._sourceNodes[0]!.disconnect).toHaveBeenCalled();
    expect(eq.isRerouted).toBe(false);
  });

  it('recoverFromDegradation does not pre-zero volume; only recovers after attachSource returns true', async () => {
    const onRecovered = vi.fn();
    const { eq, audio } = await makeReroutedEq(vi.fn(), onRecovered);
    eq.disconnectSource();
    audio.volume = 0.8;
    const attachSpy = vi.spyOn(eq, 'attachSource');

    expect(eq.recoverFromDegradation(audio, 0.8)).toBe(true);

    expect(attachSpy).toHaveBeenCalledWith(audio, 0.8);
    expect(onRecovered).toHaveBeenCalledTimes(1);
    expect(eq.isRerouted).toBe(true);
    expect(audio.volume).toBe(0);
    attachSpy.mockRestore();
  });

  it('recoverFromDegradation ramps gainNode back to outputVolume (spec §4.4)', async () => {
    const { eq, ctx, audio } = await makeReroutedEq();
    eq.setVolume(0.55);
    ctx._gainNode.gain.value = 0;

    eq.recoverFromDegradation(audio, 0.65);

    const { gain } = ctx._gainNode;
    expect(gain.cancelScheduledValues).toHaveBeenCalledWith(0);
    expect(gain.setValueAtTime).toHaveBeenCalledWith(0, 0);
    expect(gain.linearRampToValueAtTime).toHaveBeenCalledWith(0.55, 0.05);
  });
});

describe('WebAudioEq volume lease', () => {
  let mocks: ReturnType<typeof setupMocks>;

  beforeEach(() => {
    mocks = setupMocks();
  });
  afterEach(() => mocks.teardown());

  async function makeEq() {
    const ctx = mocks.makeMockCtx();
    const eq = new WebAudioEq(mockCtxFactory(ctx));
    await initAndReady(eq, { enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    return { eq, ctx };
  }

  it('captureStream throw returns false and does not leave volume at 0', async () => {
    const { eq } = await makeEq();
    const audio = makeMockAudio() as HTMLAudioElement & { captureStream: () => MediaStream };
    audio.volume = 0.5;
    audio.captureStream = () => {
      throw new Error('captureStream failed');
    };
    expect(eq.attachSource(audio, 0.5)).toBe(false);
    expect(audio.volume).toBe(0.5);
    expect(eq.isRerouted).toBe(false);
  });

  it('createMediaStreamSource throw stops the new stream tracks and restores volume', async () => {
    const { eq, ctx } = await makeEq();
    ctx.createMediaStreamSource.mockImplementation(() => {
      throw new Error('createMediaStreamSource failed');
    });
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.44;
    expect(eq.attachSource(audio, 0.44)).toBe(false);
    expect(audio.volume).toBe(0.44);
    expect(eq.isRerouted).toBe(false);
    expect(mocks.createdStreams[0]!._tracks[0]!.stop).toHaveBeenCalled();
  });

  it('connect throw stops tracks, restores volume, and returns false', async () => {
    const { eq, ctx } = await makeEq();
    ctx.createMediaStreamSource.mockImplementation((stream: MockStream) => {
      const sn = {
        connect: vi.fn(() => {
          throw new Error('connect failed');
        }),
        disconnect: vi.fn(),
        _stream: stream,
      };
      ctx._sourceNodes.push(sn);
      return sn;
    });
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.3;
    expect(eq.attachSource(audio, 0.3)).toBe(false);
    expect(audio.volume).toBe(0.3);
    expect(eq.isRerouted).toBe(false);
    expect(ctx._sourceNodes[0]!.disconnect).toHaveBeenCalled();
    expect(mocks.createdStreams[0]!._tracks[0]!.stop).toHaveBeenCalled();
  });

  it('returns false and does not change volume when ctx/worklet is unavailable', async () => {
    const eq = new WebAudioEq(() => null);
    eq.init({ enabled: true, bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] });
    await eq.awaitReady();
    const audio = makeMockAudio();
    audio.volume = 0.22;
    expect(eq.attachSource(audio, 0.22)).toBe(false);
    expect(audio.volume).toBe(0.22);
  });

  it('disconnect after setVolume restores the current volume, not the attach-time fallback', async () => {
    const { eq } = await makeEq();
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.7;
    expect(eq.attachSource(audio, 0.7)).toBe(true);
    expect(audio.volume).toBe(0);
    eq.setVolume(0.2);
    eq.disconnectSource();
    expect(audio.volume).toBe(0.2);
  });

  it('backend.setVolume then stop restores the current volume, not the attach fallback', async () => {
    const { eq } = await makeEq();
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.pause = vi.fn();
    audio.load = vi.fn();
    (audio as HTMLAudioElement & { captureStream: () => MediaStream }).captureStream = () =>
      mocks.captureStreamImpl() as unknown as MediaStream;
    const backend = new Html5AudioBackend(audio, {
      disconnectEq: () => eq.disconnectSource(),
      isEqRerouted: () => eq.isRerouted,
      setEqVolume: (vol) => eq.setVolume(vol),
    });
    audio.volume = 0.7;
    expect(eq.attachSource(audio, 0.7)).toBe(true);
    expect(audio.volume).toBe(0);
    await backend.setVolume(0.2);
    await backend.stop();
    expect(audio.volume).toBe(0.2);
    expect(eq.isRerouted).toBe(false);
  });

  it('second disconnect is idempotent and does not overwrite a later user volume', async () => {
    const { eq } = await makeEq();
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio.volume = 0.5;
    expect(eq.attachSource(audio, 0.5)).toBe(true);
    eq.disconnectSource();
    expect(audio.volume).toBe(0.5);
    audio.volume = 0.9;
    eq.disconnectSource();
    expect(audio.volume).toBe(0.9);
  });

  it('cancelling enterDegradation via disconnect still restores volume', async () => {
    vi.useFakeTimers();
    try {
      const { eq } = await makeEq();
      const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
      audio.volume = 0.7;
      expect(eq.attachSource(audio, 0.7)).toBe(true);
      eq.enterDegradation(audio, 0.7);
      eq.disconnectSource();
      expect(audio.volume).toBe(0.7);
      vi.advanceTimersByTime(50);
      expect(audio.volume).toBe(0.7);
    } finally {
      vi.useRealTimers();
    }
  });

  it('releaseLease is a no-op for a stale lease id after a newer attach', async () => {
    const { eq } = await makeEq();
    const audio1 = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    const audio2 = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);
    audio1.volume = 0.2;
    audio2.volume = 0.8;
    expect(eq.attachSource(audio1, 0.2)).toBe(true);
    const firstLease = eq.currentLeaseId;
    expect(eq.attachSource(audio2, 0.8)).toBe(true);
    eq.releaseLease(firstLease);
    expect(eq.isRerouted).toBe(true);
    expect(audio2.volume).toBe(0);
    expect(audio1.volume).toBe(0.2);
  });

  it('B1: each attach/disconnect lease restores the latest preference across EQ off→on cycles', async () => {
    const { eq } = await makeEq();
    const audio = makeMockAudio(() => mocks.captureStreamImpl() as MockStream);

    // First lease: attach with preference 0.6, user raises volume while rerouted.
    audio.volume = 0.6;
    expect(eq.attachSource(audio, 0.6)).toBe(true);
    expect(audio.volume).toBe(0);
    eq.setVolume(0.9);
    eq.disconnectSource();
    expect(audio.volume).toBe(0.9);
    expect(eq.isRerouted).toBe(false);

    // Second lease after re-enable: attach passes the NEW preference; the
    // previous lease's volume must not leak into the new lease restore.
    audio.volume = 0.4;
    expect(eq.attachSource(audio, 0.4)).toBe(true);
    expect(audio.volume).toBe(0);
    eq.disconnectSource();
    expect(audio.volume).toBe(0.4);
  });
});
