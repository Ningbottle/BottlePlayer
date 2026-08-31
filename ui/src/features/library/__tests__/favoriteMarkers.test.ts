import { describe, it, expect, beforeEach } from 'vitest';
import {
  __resetFavoriteMarkersForTests,
  isFavoriteMarker,
  isLikedPlaylistName,
  markFavorite,
  markFavorites,
} from '../favoriteMarkers';

describe('favoriteMarkers', () => {
  beforeEach(() => {
    __resetFavoriteMarkersForTests();
  });

  it('marks a single hash and persists across reload helpers', () => {
    expect(isFavoriteMarker('h1')).toBe(false);
    markFavorite('h1');
    expect(isFavoriteMarker('h1')).toBe(true);
  });

  it('bulk-marks hashes from 我喜欢的音乐 page load', () => {
    markFavorites(['a', 'b', '', 'a']);
    expect(isFavoriteMarker('a')).toBe(true);
    expect(isFavoriteMarker('b')).toBe(true);
    expect(isFavoriteMarker('c')).toBe(false);
  });

  it('recognizes liked playlist names', () => {
    expect(isLikedPlaylistName('我喜欢的音乐')).toBe(true);
    expect(isLikedPlaylistName(' 我喜欢 ')).toBe(true);
    expect(isLikedPlaylistName('Liked Songs')).toBe(true);
    expect(isLikedPlaylistName('Favorites')).toBe(true);
    expect(isLikedPlaylistName('通勤精选')).toBe(false);
    expect(isLikedPlaylistName('')).toBe(false);
    expect(isLikedPlaylistName(null)).toBe(false);
  });
});
