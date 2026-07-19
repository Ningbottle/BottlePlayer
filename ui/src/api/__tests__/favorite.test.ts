import { describe, it, expect } from 'vitest';
import { normalizePlaylists } from '../favoriteStore';

describe('normalizePlaylists', () => {
  // 基本场景：标准 API 响应格式
  it('should extract playlists from standard API response with info array', () => {
    const payload = {
      status: 1,
      data: {
        info: [
          { listid: '123', listname: '我的歌单', songcount: 10 },
          { listid: '456', name: '收藏歌单', song_count: 5 },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(2);
    expect(result[0]).toEqual({ id: '123', name: '我的歌单', songcount: 10, listid: '123' });
    expect(result[1]).toEqual({ id: '456', name: '收藏歌单', songcount: 5, listid: '456' });
  });

  // 使用 list 字段
  it('should extract playlists from data.list', () => {
    const payload = {
      status: 1,
      data: {
        list: [
          { specialid: '789', specialname: '特别歌单' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('789');
    expect(result[0].name).toBe('特别歌单');
  });

  // 使用 global_collection_id（用户收藏歌单格式）
  it('should extract playlists with global_collection_id', () => {
    const payload = {
      status: 1,
      data: {
        info: [
          { global_collection_id: 'collection_3_12345_67890_0', name: '收藏的歌单' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('collection_3_12345_67890_0');
    expect(result[0].name).toBe('收藏的歌单');
  });

  // 用户歌单真实场景：打开歌单需要 global_collection_id，添加歌曲需要纯数字 listid
  it('should keep global_collection_id and numeric listid separate', () => {
    const payload = {
      status: 1,
      data: {
        info: [
          {
            global_collection_id: 'collection_3_12345_67890_0',
            listid: '67890',
            listname: '收藏的歌单',
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
      songcount: 0,
    });
  });

  // 去重：相同 id 的歌单只保留一个
  it('should deduplicate playlists by id', () => {
    const payload = {
      data: {
        info: [
          { listid: '123', name: '歌单A' },
          { listid: '123', name: '歌单A（重复）' },
          { listid: '456', name: '歌单B' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(2);
    expect(result[0].id).toBe('123');
    expect(result[0].name).toBe('歌单A'); // 保留第一个
    expect(result[1].id).toBe('456');
  });

  // 空响应
  it('should return empty array for empty response', () => {
    expect(normalizePlaylists({})).toEqual([]);
    expect(normalizePlaylists(null)).toEqual([]);
    expect(normalizePlaylists(undefined)).toEqual([]);
    expect(normalizePlaylists({ status: 0, data: null })).toEqual([]);
  });

  // 缺少 id 字段的条目应被过滤
  it('should filter out items without id', () => {
    const payload = {
      data: {
        info: [
          { listid: '123', name: '有效歌单' },
          { name: '无id歌单' },  // 缺少 id，应被过滤
          { listid: '', name: '空id歌单' },  // 空 id，应被过滤
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('123');
  });

  // 默认名称：当 name 为空时使用 '无标题歌单'
  it('should use default name when name is empty', () => {
    const payload = {
      data: {
        info: [
          { listid: '123' },  // 没有 name
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].name).toBe('无标题歌单');
  });

  // 深层嵌套的响应格式
  it('should handle deeply nested response', () => {
    const payload = {
      status: 1,
      data: {
        cloud_list: [
          { listid: '111', name: '云盘歌单' },
        ],
      },
    };

    const result = normalizePlaylists(payload);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('111');
  });
});
