import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

vi.mock('gsap', () => {
  const fromTo = vi.fn((_el: any, _from: any, opts: any) => {
    if (opts?.onComplete) opts.onComplete();
  });
  const to = vi.fn((_el: any, opts: any) => {
    if (opts?.onComplete) opts.onComplete();
  });
  return { gsap: { fromTo, to } };
});

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue('') }));

import AddToPlaylistModal from '../AddToPlaylistModal.vue';

describe('AddToPlaylistModal transition', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });
  afterEach(() => {
    document.body.innerHTML = '';
  });

  it('wraps the modal in a <Transition> that calls gsap.fromTo on enter', async () => {
    const { gsap } = await import('gsap');
    const wrapper = mount(AddToPlaylistModal, {
      attachTo: document.body,
      global: { stubs: { transition: false } },
      props: {
        show: true,
        track: { FileHash: 'h', SongName: 's', SingerName: 'a', Duration: 1 } as any,
      },
    });
    await flushPromises();

    expect(document.body.querySelector('.playlist-modal')).not.toBeNull();
    expect(gsap.fromTo).toHaveBeenCalled();
    wrapper.unmount();
  });

  it('calls gsap.to on leave when show flips to false', async () => {
    const { gsap } = await import('gsap');
    const wrapper = mount(AddToPlaylistModal, {
      attachTo: document.body,
      global: { stubs: { transition: false } },
      props: {
        show: true,
        track: { FileHash: 'h', SongName: 's', SingerName: 'a', Duration: 1 } as any,
      },
    });
    await flushPromises();
    vi.mocked(gsap.fromTo).mockClear();
    vi.mocked(gsap.to).mockClear();

    await wrapper.setProps({ show: false });
    await flushPromises();

    expect(gsap.to).toHaveBeenCalled();
  });
});
