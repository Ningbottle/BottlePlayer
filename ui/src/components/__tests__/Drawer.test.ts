import { describe, it, expect, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import Drawer from '../Drawer.vue';

describe('Drawer theming demotion', () => {
  beforeEach(() => {
    localStorage.clear();
    document.documentElement.className = '';
    document.documentElement.style.cssText = '';
  });

  it('does NOT add the html.dark class when dark mode is desired (themeStore owns dark)', () => {
    const wrapper = mount(Drawer, { props: { collapsed: false } });
    // Drawer should have NO isDarkMode toggle at all after demotion.
    const darkLabels = wrapper.findAll('label').filter((l) => l.text().includes('深色模式'));
    expect(darkLabels).toHaveLength(0);
    expect(document.documentElement.classList.contains('dark')).toBe(false);
  });

  it('does NOT write --paper/--ink base tokens to documentElement.style', () => {
    mount(Drawer, { props: { collapsed: false } });
    const style = document.documentElement.style;
    // After demotion, Drawer must not write these base tokens.
    expect(style.getPropertyValue('--paper')).toBe('');
    expect(style.getPropertyValue('--ink')).toBe('');
    expect(style.getPropertyValue('--paper-2')).toBe('');
    expect(style.getPropertyValue('--ink-soft')).toBe('');
  });

  it('writes micro-adjustment variables and applies user-selected accent for all skins', () => {
    mount(Drawer, { props: { collapsed: false } });
    const style = document.documentElement.style;
    expect(style.getPropertyValue('--glass-blur')).not.toBe('');
    expect(style.getPropertyValue('--grain')).not.toBe('');
    expect(style.getPropertyValue('--accent')).not.toBe('');
  });

  it('leaves the default font token to the active skin', () => {
    mount(Drawer, { props: { collapsed: false } });

    expect(document.documentElement.style.getPropertyValue('--font-serif')).toBe('');
  });
});
