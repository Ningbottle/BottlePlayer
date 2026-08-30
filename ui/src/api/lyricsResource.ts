import { reactive } from 'vue';
import type { Track } from '../shared/music/track';

export interface LyricLine {
  time: number;
  text: string;
}

export interface LyricsResourceState {
  loading: boolean;
  lines: LyricLine[];
  error: Error | null;
}

export type LoadLyrics = (track: Track) => Promise<LyricLine[]>;

export class LyricsResource {
  readonly state = reactive<LyricsResourceState>({
    loading: false,
    lines: [],
    error: null,
  });

  private generation = 0;
  private currentTrack: Track | null = null;
  private disposed = false;

  constructor(private readonly loadLyrics: LoadLyrics) {}

  async load(track: Track | null): Promise<void> {
    if (this.disposed) return;

    const generation = ++this.generation;
    this.currentTrack = track;
    if (!track) {
      this.state.loading = false;
      this.state.lines = [];
      this.state.error = null;
      return;
    }

    this.state.loading = true;
    this.state.lines = [];
    this.state.error = null;

    try {
      const lines = await this.loadLyrics(track);
      if (!this.isCurrent(generation)) return;
      this.state.lines = lines;
    } catch (error) {
      if (!this.isCurrent(generation)) return;
      this.state.error = error instanceof Error ? error : new Error('Failed to load lyrics');
    } finally {
      if (this.isCurrent(generation)) this.state.loading = false;
    }
  }

  async retry(): Promise<void> {
    if (!this.currentTrack || this.disposed) return;
    await this.load(this.currentTrack);
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.generation++;
    this.currentTrack = null;
    this.state.loading = false;
    this.state.lines = [];
    this.state.error = null;
  }

  private isCurrent(generation: number): boolean {
    return !this.disposed && generation === this.generation;
  }
}
