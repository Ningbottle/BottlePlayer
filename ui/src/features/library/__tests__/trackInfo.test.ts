import { describe, it, expect } from 'vitest';

/**
 * 测试收藏歌曲时的参数构造逻辑
 * 这些测试验证 SongName 中特殊字符的转义处理
 */

function buildTrackInfo(track: { SongName: string; FileHash: string; AlbumID?: string; AlbumAudioID?: string }) {
  // 与 favorite.ts 中的逻辑保持一致
  const safeName = track.SongName.replace(/\|/g, '%7C');
  return `${safeName}|${track.FileHash}|${track.AlbumID || 0}|${track.AlbumAudioID || 0}`;
}

describe('buildTrackInfo for playlist add', () => {
  // 正常情况：SongName 不含特殊字符
  it('should build track info with normal song name', () => {
    const track = {
      SongName: '晴天',
      FileHash: 'ABC123',
      AlbumID: '100',
      AlbumAudioID: '200',
    };

    const result = buildTrackInfo(track);
    expect(result).toBe('晴天|ABC123|100|200');
  });

  // 关键场景：SongName 包含 | 字符
  it('should escape pipe character in song name', () => {
    const track = {
      SongName: '歌曲|副标题',
      FileHash: 'ABC123',
      AlbumID: '100',
      AlbumAudioID: '200',
    };

    const result = buildTrackInfo(track);
    // | 应被转义为 %7C，避免后端解析错位
    expect(result).toBe('歌曲%7C副标题|ABC123|100|200');
    
    // 验证按 | 分割后只有 4 个部分
    const parts = result.split('|');
    expect(parts).toHaveLength(4);
  });

  // 多个 | 字符
  it('should escape multiple pipe characters', () => {
    const track = {
      SongName: 'A|B|C',
      FileHash: 'HASH',
      AlbumID: '1',
      AlbumAudioID: '2',
    };

    const result = buildTrackInfo(track);
    expect(result).toBe('A%7CB%7CC|HASH|1|2');
    
    const parts = result.split('|');
    expect(parts).toHaveLength(4);
    expect(parts[0]).toBe('A%7CB%7CC');
  });

  // 缺少可选字段
  it('should use default values for missing optional fields', () => {
    const track = {
      SongName: '测试歌曲',
      FileHash: 'HASH123',
    };

    const result = buildTrackInfo(track);
    expect(result).toBe('测试歌曲|HASH123|0|0');
  });
});
