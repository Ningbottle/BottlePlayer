export type PlaybackEventType = 'position' | 'state' | 'ended' | 'error';

export interface PlaybackEvent {
  type: PlaybackEventType;
  position?: number;
  duration?: number;
  state?: string;
  error?: string;
}

export type PlaybackState = {
  state: string;  // 'playing' | 'paused' | 'stopped' | 'uninitialized'
  position: number;
  duration: number;
};

export interface PlayerBackend {
  readonly kind: 'html5' | 'native';
  initialize(): Promise<boolean>;
  playUrl(url: string): Promise<boolean>;
  pause(): Promise<void>;
  resume(): Promise<void>;
  stop(): Promise<void>;
  seek(seconds: number): Promise<void>;
  setVolume(v: number): Promise<void>;
  setRate(r: number): Promise<void>;
  getState(): Promise<PlaybackState>;
  shutdown(): Promise<void>;
  onEvent(cb: (e: PlaybackEvent) => void): () => void;
}
