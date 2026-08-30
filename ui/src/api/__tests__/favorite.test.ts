import { describe, it, expect } from 'vitest';
import { normalizePlaylists } from '../favoriteStore';

describe('normalizePlaylists', () => {
  it('keeps global_collection_id as navigation id and numeric listid for add/del', () => {
    const payload = {
      status: 1,
      data: {
        info: [
          {
            global_collection_id: 'collection_3_12345_67890_0',
            listid: '67890',
            listname: '收藏的歌单',
            songcount: 10,
          },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual({
      id: 'collection_3_12345_67890_0',
      listid: '67890',
      name: '收藏的歌单',
      songcount: 10,
    });
  });

  it('does not let user playlists fall back to numeric listid or specialid', () => {
    const payload = {
      status: 1,
      data: {
        info: [
          { listid: '123', listname: '只有数字 listid' },
          { specialid: '789', specialname: '公共歌单' },
          { global_collection_id: 'collection_3_1_2_0', listid: '2', listname: '有效用户歌单' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('collection_3_1_2_0');
    expect(result[0].listid).toBe('2');
  });

  it('deduplicates playlists by global_collection_id', () => {
    const payload = {
      data: {
        info: [
          { global_collection_id: 'collection_3_1_123_0', listid: '123', name: '歌单A' },
          { global_collection_id: 'collection_3_1_123_0', listid: '123', name: '歌单A（重复）' },
          { global_collection_id: 'collection_3_1_456_0', listid: '456', name: '歌单B' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(2);
    expect(result[0].id).toBe('collection_3_1_123_0');
    expect(result[0].name).toBe('歌单A');
    expect(result[1].id).toBe('collection_3_1_456_0');
  });

  it('returns empty array for empty response', () => {
    expect(normalizePlaylists({})).toEqual([]);
    expect(normalizePlaylists(null)).toEqual([]);
    expect(normalizePlaylists(undefined)).toEqual([]);
    expect(normalizePlaylists({ status: 0, data: null })).toEqual([]);
  });

  it('filters items missing a collection GID or numeric listid', () => {
    const payload = {
      data: {
        info: [
          { global_collection_id: 'collection_3_1_123_0', listid: '123', name: '有效歌单' },
          { name: '无id歌单' },
          { listid: '', name: '空id歌单' },
          { global_collection_id: 'collection_3_1_9_0', listname: '缺 listid' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('collection_3_1_123_0');
  });

  it('uses default name when name is empty', () => {
    const payload = {
      data: {
        info: [
          { global_collection_id: 'collection_3_1_123_0', listid: '123' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].name).toBe('无标题歌单');
  });
});
