import { beforeEach, describe, expect, it, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import AuroraShell from '../AuroraShell.vue';
import NewsprintShell from '../NewsprintShell.vue';

vi.mock('../../../platform/tauri/windows', () => ({
  minimizeCurrentWindow: vi.fn(), toggleMaximizeCurrentWindow: vi.fn(), closeCurrentWindow: vi.fn(),
}));

beforeEach(() => localStorage.clear());

async function pointer(el: Element, type: string, clientX: number): Promise<void> {
  const event = new MouseEvent(type, { button: 0, clientX, bubbles: true });
  Object.defineProperty(event, 'pointerId', { value: 1 });
  el.dispatchEvent(event);
  await nextTick();
}

describe('sidebar resize', () => {
  it.each([AuroraShell, NewsprintShell])('drags the shell column, clamps width and stops after cancellation', async (Shell) => {
    const wrapper = mount(Shell);
    const handle = wrapper.get('[role="separator"]');
    const column = wrapper.get('.shell-sidebar');
    vi.spyOn(column.element, 'getBoundingClientRect').mockReturnValue({ width: 240 } as DOMRect);
    await pointer(handle.element, 'pointerdown', 240);
    await pointer(handle.element, 'pointermove', 312);
    expect(wrapper.attributes('style')).toContain('--sidebar-width: 312px');
    await pointer(handle.element, 'pointermove', 1000);
    expect(handle.attributes('aria-valuenow')).toBe('360');
    await handle.trigger('pointercancel');
    await pointer(handle.element, 'pointermove', 220);
    expect(handle.attributes('aria-valuenow')).toBe('360');
    expect(localStorage.getItem('sidebar_width')).toBe('360');
    wrapper.unmount();
  });

  it('restores the saved width across skins and supports keyboard resizing and reset', async () => {
    localStorage.setItem('sidebar_width', '300');
    const wrapper = mount(NewsprintShell);
    await nextTick();
    const handle = wrapper.get('[role="separator"]');
    expect(wrapper.attributes('style')).toContain('--sidebar-width: 300px');
    await handle.trigger('keydown', { key: 'ArrowLeft' });
    expect(localStorage.getItem('sidebar_width')).toBe('290');
    await handle.trigger('keydown', { key: 'Home' });
    expect(handle.attributes('aria-valuenow')).toBe('200');
    await handle.trigger('dblclick');
    expect(localStorage.getItem('sidebar_width')).toBe('240');
    wrapper.unmount();
    const next = mount(AuroraShell);
    await nextTick();
    expect(next.attributes('style')).toContain('--sidebar-width: 240px');
    next.unmount();
  });
});
