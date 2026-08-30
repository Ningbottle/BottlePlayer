import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import PlayerProgress from '../PlayerProgress.vue';

const tokensCss = readFileSync(resolve(process.cwd(), 'src/styles/tokens.css'), 'utf8');

function mockRect(el: Element, width: number, left = 0) {
  vi.spyOn(el, 'getBoundingClientRect').mockReturnValue({
    width,
    left,
    right: left + width,
    top: 0,
    bottom: 0,
    height: 0,
    x: left,
    y: 0,
    toJSON: () => {},
  } as DOMRect);
}

describe('PlayerProgress', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    document.head.querySelector('[data-test="progress-token-contract"]')?.remove();
    document.documentElement.removeAttribute('data-skin');
    document.documentElement.removeAttribute('data-mode');
  });

  // ── ARIA ──
  it('renders role=slider with aria-valuemin=0, aria-valuemax=duration, aria-valuenow=currentTime', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120 },
    });
    const slider = wrapper.find('[role="slider"]');
    expect(slider.exists()).toBe(true);
    expect(slider.attributes('aria-valuemin')).toBe('0');
    expect(slider.attributes('aria-valuemax')).toBe('120');
    expect(slider.attributes('aria-valuenow')).toBe('30');
  });

  // ── Click seek ──
  it('emits seek with correct position on track click', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 0, duration: 100 },
    });
    const track = wrapper.find('.progress-track');
    mockRect(track.element, 200, 0);
    await track.trigger('click', { clientX: 100 });
    expect(wrapper.emitted('seek')).toBeTruthy();
    expect(wrapper.emitted('seek')![0]).toEqual([50]);
  });

  // ── Hover preview ──
  it('shows a time preview bubble following the cursor', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 10, duration: 100 },
    });
    const track = wrapper.find('.progress-track');
    mockRect(track.element, 200, 0);

    expect(wrapper.find('.progress-hover-tip').exists()).toBe(false);

    await track.trigger('mousemove', { clientX: 100 });
    const tip = wrapper.find('.progress-hover-tip');
    expect(tip.exists()).toBe(true);
    expect(tip.text()).toBe('00:50');
    expect(tip.attributes('style')).toContain('left: 50%');

    await track.trigger('mouseleave');
    expect(wrapper.find('.progress-hover-tip').exists()).toBe(false);
  });

  it('shows no hover preview when duration is invalid', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 0, duration: 0 },
    });
    const track = wrapper.find('.progress-track');
    mockRect(track.element, 200, 0);

    await track.trigger('mousemove', { clientX: 100 });
    expect(wrapper.find('.progress-hover-tip').exists()).toBe(false);
  });

  it('clamps click position to [0, duration]', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 50, duration: 100 },
    });
    const track = wrapper.find('.progress-track');
    mockRect(track.element, 200, 0);
    await track.trigger('click', { clientX: 300 });
    expect(wrapper.emitted('seek')![0]).toEqual([100]);
    await track.trigger('click', { clientX: -50 });
    expect(wrapper.emitted('seek')![1]).toEqual([0]);
  });

  // ── Keyboard seek ──
  it('ArrowLeft decreases by 5s, clamped to 0', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 100 },
    });
    const slider = wrapper.find('[role="slider"]');
    await slider.trigger('keydown', { key: 'ArrowLeft' });
    expect(wrapper.emitted('seek')![0]).toEqual([25]);

    await wrapper.setProps({ currentTime: 2 });
    await slider.trigger('keydown', { key: 'ArrowLeft' });
    expect(wrapper.emitted('seek')![1]).toEqual([0]);
  });

  it('ArrowRight increases by 5s, clamped to duration', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 100 },
    });
    const slider = wrapper.find('[role="slider"]');
    await slider.trigger('keydown', { key: 'ArrowRight' });
    expect(wrapper.emitted('seek')![0]).toEqual([35]);

    await wrapper.setProps({ currentTime: 98 });
    await slider.trigger('keydown', { key: 'ArrowRight' });
    expect(wrapper.emitted('seek')![1]).toEqual([100]);
  });

  it('Home seeks to 0, End seeks to duration', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 50, duration: 100 },
    });
    const slider = wrapper.find('[role="slider"]');
    await slider.trigger('keydown', { key: 'Home' });
    expect(wrapper.emitted('seek')![0]).toEqual([0]);
    await slider.trigger('keydown', { key: 'End' });
    expect(wrapper.emitted('seek')![1]).toEqual([100]);
  });

  // ── Stable class names ──
  it('uses stable classes: progress-track, progress-fill, progress-thumb', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120 },
    });
    expect(wrapper.find('.progress-track').exists()).toBe(true);
    expect(wrapper.find('.progress-fill').exists()).toBe(true);
    expect(wrapper.find('.progress-thumb').exists()).toBe(true);
  });

  it('sets --progress-pct CSS custom property on track', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120 },
    });
    const track = wrapper.find('.progress-track');
    expect((track.element as HTMLElement).style.getPropertyValue('--progress-pct')).toBe('25%');
  });

  it('does not contain skin condition branches in rendered output', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120 },
    });
    const html = wrapper.html();
    expect(html).not.toContain('data-skin');
    expect(html).not.toContain('aurora');
    expect(html).not.toContain('newsprint');
  });

  it.each([
    [":root[data-skin='aurora'][data-mode='light']", 'Aurora light'],
    [":root[data-skin='aurora'][data-mode='dark']", 'Aurora dark'],
    [":root[data-skin='newsprint'][data-mode='light']", 'Newsprint light'],
    [":root[data-skin='newsprint'][data-mode='dark']", 'Newsprint dark'],
  ])('defines opaque progress tokens for %s', (selector) => {
    const skin = selector.match(/data-skin='([^']+)'/)?.[1];
    const mode = selector.match(/data-mode='([^']+)'/)?.[1];
    expect(skin).toBeTruthy();
    expect(mode).toBeTruthy();

    const style = document.createElement('style');
    style.dataset.test = 'progress-token-contract';
    style.textContent = tokensCss;
    document.head.appendChild(style);
    document.documentElement.dataset.skin = skin!;
    document.documentElement.dataset.mode = mode!;
    const computed = getComputedStyle(document.documentElement);

    for (const token of ['--progress-track', '--progress-fill']) {
      const value = computed.getPropertyValue(token).trim();
      expect(value).toBeTruthy();
      expect(value).not.toMatch(/transparent|rgba?\([^)]*,\s*0(?:\.0+)?\s*\)/i);
    }
  });

  // ── duration = 0 edge case ──
  it('duration=0: no role=slider, no seek on click, aria-disabled', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 0, duration: 0 },
    });
    expect(wrapper.find('[role="slider"]').exists()).toBe(false);
    const track = wrapper.find('.progress-track');
    expect(track.attributes('aria-disabled')).toBe('true');
    mockRect(track.element, 200, 0);
    await track.trigger('click', { clientX: 100 });
    expect(wrapper.emitted('seek')).toBeFalsy();
  });

  it('duration=0: keyboard does not emit seek', async () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 0, duration: 0 },
    });
    const track = wrapper.find('.progress-track');
    await track.trigger('keydown', { key: 'ArrowRight' });
    await track.trigger('keydown', { key: 'Home' });
    await track.trigger('keydown', { key: 'End' });
    expect(wrapper.emitted('seek')).toBeFalsy();
  });

  it('duration=0: --progress-pct is 0% (no NaN)', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 0, duration: 0 },
    });
    const track = wrapper.find('.progress-track');
    const pct = (track.element as HTMLElement).style.getPropertyValue('--progress-pct');
    expect(pct).toBe('0%');
    expect(pct).not.toContain('NaN');
  });

  // ── Buffered prop ──
  it('renders .progress-buffered when buffered prop is passed', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120, buffered: 60 },
    });
    expect(wrapper.find('.progress-buffered').exists()).toBe(true);
  });

  it('does not render .progress-buffered when buffered prop is omitted', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120 },
    });
    expect(wrapper.find('.progress-buffered').exists()).toBe(false);
  });

  it('sets --progress-buffered-pct to correct percentage', () => {
    const wrapper = mount(PlayerProgress, {
      props: { currentTime: 30, duration: 120, buffered: 60 },
    });
    const track = wrapper.find('.progress-track');
    const pct = (track.element as HTMLElement).style.getPropertyValue('--progress-buffered-pct');
    expect(pct).toBe('50%');
  });
});
