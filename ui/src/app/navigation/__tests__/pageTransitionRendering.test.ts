import { afterEach, describe, expect, it, vi } from 'vitest';
import { defineComponent, h, KeepAlive, nextTick, ref, Transition } from 'vue';
import { flushPromises, mount } from '@vue/test-utils';
import { transitionEnter, transitionLeave } from '../pageTransitions';
import { settleActiveTransitionSessions } from '../transitionSession';

afterEach(() => {
  settleActiveTransitionSessions()();
  vi.unstubAllGlobals();
});

describe('reduced-motion page rendering', () => {
  it('switches out-in pages with real Transition and KeepAlive without reentrant patch errors', async () => {
    vi.stubGlobal('matchMedia', () => ({ matches: true }));
    const page = ref('settings');
    const errors: unknown[] = [];
    const Settings = defineComponent({ name: 'TestSettings', render: () => h('main', '设置内容') });
    const Stats = defineComponent({ name: 'TestStats', render: () => h('main', '统计内容') });
    const Harness = defineComponent({
      components: { Transition, KeepAlive },
      setup: () => ({ page, Settings, Stats, transitionEnter, transitionLeave }),
      template: `<section>
        <Transition mode="out-in" :css="false" @enter="transitionEnter" @leave="transitionLeave">
          <KeepAlive :include="['TestStats']">
            <component :is="page === 'settings' ? Settings : Stats" :key="page" />
          </KeepAlive>
        </Transition>
      </section>`,
    });
    const wrapper = mount(Harness, {
      attachTo: document.body,
      global: { stubs: { transition: false }, config: { errorHandler: (e) => errors.push(e) } },
    });
    try {
      for (const value of ['stats', 'settings', 'stats']) {
        page.value = value;
        await nextTick();
        await flushPromises();
        expect(errors.map(error => error instanceof Error ? error.stack : error)).toEqual([]);
        expect(wrapper.text()).toBe(value === 'stats' ? '统计内容' : '设置内容');
      }
    } finally { wrapper.unmount(); }
  });
});
