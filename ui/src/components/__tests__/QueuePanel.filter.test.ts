import { describe, it, expect, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import QueuePanel from '../QueuePanel.vue';
import { playerStore } from '../../api/playerStore';

describe('QueuePanel filter', () => {
  beforeEach(() => {
    playerStore.queue = [
      {
        FileHash: 'a',
        SongName: '晴天',
        SingerName: '周杰伦',
        Duration: 100,
      } as any,
      {
        FileHash: 'b',
        SongName: '稻香',
        SingerName: '周杰伦',
        Duration: 100,
      } as any,
      {
        FileHash: 'c',
        SongName: '海阔天空',
        SingerName: 'Beyond',
        Duration: 100,
      } as any,
    ];
    playerStore.currentTrack = null;
  });

  it('filters queue by song or artist substring', async () => {
    const w = mount(QueuePanel, { props: { show: true } });
    expect(w.findAll('.item')).toHaveLength(3);

    const input = w.get('input.queue-filter');
    await input.setValue('beyond');
    expect(w.findAll('.item')).toHaveLength(1);
    expect(w.text()).toContain('海阔天空');

    await input.setValue('周杰');
    expect(w.findAll('.item')).toHaveLength(2);

    await input.setValue('不存在的歌');
    expect(w.findAll('.item')).toHaveLength(0);
    expect(w.text()).toContain('无匹配结果');
  });
});
