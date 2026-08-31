/**
 * Typed stats/AI gateway — the only Stats Feature surface for Tauri commands.
 * Command names, arg field names, and payload parsing live here; StatsView
 * consumes typed data and never parses raw Tauri JSON strings.
 */
import { invokeTauri } from '../../platform/tauri/invoke';

export type StatsRange = '1d' | '7d' | '30d';
export type StatsTopKind = 'song' | 'artist' | 'album';

export interface StatsSummary {
  total_plays: number;
  total_listened_seconds: number;
  unique_songs: number;
  unique_artists: number;
  completion_rate: number;
}

export interface StatsTopItem {
  song_hash?: string;
  album_id?: string;
  name: string;
  singer?: string;
  album?: string;
  cover_url?: string;
  play_count: number;
  total_listened_seconds: number;
}

export interface StatsTimelineItem {
  date: string;
  count: number;
}

export async function getStatsSummary(range: StatsRange): Promise<StatsSummary> {
  const raw = await invokeTauri<string>('stats_get_summary', { range });
  return JSON.parse(raw) as StatsSummary;
}

export async function getStatsTop(
  kind: StatsTopKind,
  range: StatsRange,
  limit: number,
): Promise<StatsTopItem[]> {
  const raw = await invokeTauri<string>('stats_get_top', { kind, range, limit });
  const parsed = JSON.parse(raw) as { items?: StatsTopItem[] };
  return parsed.items ?? [];
}

export async function getStatsTimeline(range: StatsRange): Promise<StatsTimelineItem[]> {
  const raw = await invokeTauri<string>('stats_get_timeline', { range });
  const parsed = JSON.parse(raw) as { items?: StatsTimelineItem[] };
  return parsed.items ?? [];
}

/** Structured stats snapshot handed to the AI analyze command. */
export interface StatsAnalyzeInput {
  summary: StatsSummary;
  topSongs?: StatsTopItem[];
  topArtists?: StatsTopItem[];
  timeline?: StatsTimelineItem[];
}

export async function analyzeStats(apiKey: string, stats: StatsAnalyzeInput): Promise<string> {
  return invokeTauri<string>('ai_analyze', {
    apiKey,
    statsJson: JSON.stringify(stats),
  });
}
