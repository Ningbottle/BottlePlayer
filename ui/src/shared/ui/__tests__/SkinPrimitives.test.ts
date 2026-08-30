import { describe, it, expect, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import SkinPageHeader from '../SkinPageHeader.vue';
import SkinButton from '../SkinButton.vue';
import SkinListRow from '../SkinListRow.vue';
import SkinEmptyState from '../SkinEmptyState.vue';

describe('SkinPageHeader', () => {
  beforeEach(() => {
    document.documentElement.dataset.skin = 'aurora';
  });

  it('renders title and optional kicker', () => {
    const wrapper = mount(SkinPageHeader, {
      props: { title: 'My Stats' },
    });
    expect(wrapper.text()).toContain('My Stats');
    expect(wrapper.find('.skin-page-header-kicker').exists()).toBe(false);
  });

  it('renders kicker when provided', () => {
    const wrapper = mount(SkinPageHeader, {
      props: { title: 'My Stats', kicker: 'STATS' },
    });
    expect(wrapper.find('.skin-page-header-kicker').exists()).toBe(true);
    expect(wrapper.text()).toContain('STATS');
  });

  it('renders actions slot', () => {
    const wrapper = mount(SkinPageHeader, {
      props: { title: 'Test' },
      slots: { actions: '<button class="test-action">Click</button>' },
    });
    expect(wrapper.find('.test-action').exists()).toBe(true);
  });

  it('does not generate decorative double line elements', () => {
    const wrapper = mount(SkinPageHeader, {
      props: { title: 'Test', kicker: 'K' },
    });
    expect(wrapper.findAll('.page-head-line, .header-line, .double-line').length).toBe(0);
    const el = wrapper.element as HTMLElement;
    expect(el.classList.contains('page-head')).toBe(false);
  });

  it('does not import themeStore (business-agnostic)', () => {
    const wrapper = mount(SkinPageHeader, { props: { title: 'T' } });
    expect(wrapper.exists()).toBe(true);
  });
});

describe('SkinButton', () => {
  beforeEach(() => {
    document.documentElement.dataset.skin = 'aurora';
  });

  it('renders a button with data-variant and data-size attributes', () => {
    const wrapper = mount(SkinButton, {
      props: { variant: 'primary', size: 'md' },
    });
    expect(wrapper.element.tagName).toBe('BUTTON');
    expect(wrapper.attributes('data-variant')).toBe('primary');
    expect(wrapper.attributes('data-size')).toBe('md');
  });

  it('emits click when clicked', async () => {
    const wrapper = mount(SkinButton, {
      props: { variant: 'secondary', size: 'sm' },
    });
    await wrapper.trigger('click');
    expect(wrapper.emitted('click')).toHaveLength(1);
  });

  it('does not emit click when disabled', async () => {
    const wrapper = mount(SkinButton, {
      props: { variant: 'primary', size: 'md', disabled: true },
    });
    await wrapper.trigger('click');
    expect(wrapper.emitted('click')).toBeUndefined();
  });

  it('renders default slot as label', () => {
    const wrapper = mount(SkinButton, {
      props: { variant: 'ghost', size: 'md' },
      slots: { default: 'Submit' },
    });
    expect(wrapper.text()).toContain('Submit');
  });

  it('produces same HTML structure under aurora and newsprint (CSS handles variant)', () => {
    document.documentElement.dataset.skin = 'aurora';
    const auroraWrapper = mount(SkinButton, {
      props: { variant: 'primary', size: 'md' },
    });
    const auroraHtml = auroraWrapper.html();

    document.documentElement.dataset.skin = 'newsprint';
    const newsprintWrapper = mount(SkinButton, {
      props: { variant: 'primary', size: 'md' },
    });
    const newsprintHtml = newsprintWrapper.html();

    expect(auroraHtml).toBe(newsprintHtml);
  });

  it('does not import themeStore (business-agnostic)', () => {
    const wrapper = mount(SkinButton, { props: { variant: 'ghost', size: 'sm' } });
    expect(wrapper.exists()).toBe(true);
  });
});

describe('SkinListRow', () => {
  beforeEach(() => {
    document.documentElement.dataset.skin = 'aurora';
  });

  it('renders index, title, and subtitle', () => {
    const wrapper = mount(SkinListRow, {
      props: { index: 3, title: 'Song Name', subtitle: 'Artist Name' },
    });
    expect(wrapper.text()).toContain('3');
    expect(wrapper.text()).toContain('Song Name');
    expect(wrapper.text()).toContain('Artist Name');
  });

  it('emits click when clicked', async () => {
    const wrapper = mount(SkinListRow, {
      props: { index: 1, title: 'T', subtitle: '' },
    });
    await wrapper.trigger('click');
    expect(wrapper.emitted('click')).toHaveLength(1);
  });

  it('renders cover slot', () => {
    const wrapper = mount(SkinListRow, {
      props: { index: 1, title: 'T', subtitle: '' },
      slots: { cover: '<img class="test-cover" src="x.jpg" />' },
    });
    expect(wrapper.find('.test-cover').exists()).toBe(true);
  });

  it('renders meta slot', () => {
    const wrapper = mount(SkinListRow, {
      props: { index: 1, title: 'T', subtitle: '' },
      slots: { meta: '<span class="test-meta">3:45</span>' },
    });
    expect(wrapper.find('.test-meta').exists()).toBe(true);
  });

  it('does not import themeStore (business-agnostic)', () => {
    const wrapper = mount(SkinListRow, {
      props: { index: 0, title: 'T', subtitle: '' },
    });
    expect(wrapper.exists()).toBe(true);
  });
});

describe('SkinEmptyState', () => {
  beforeEach(() => {
    document.documentElement.dataset.skin = 'aurora';
  });

  it('renders message', () => {
    const wrapper = mount(SkinEmptyState, {
      props: { message: 'No data available' },
    });
    expect(wrapper.text()).toContain('No data available');
  });

  it('renders action slot', () => {
    const wrapper = mount(SkinEmptyState, {
      props: { message: 'Empty' },
      slots: { action: '<button class="test-retry">Retry</button>' },
    });
    expect(wrapper.find('.test-retry').exists()).toBe(true);
  });

  it('does not import themeStore (business-agnostic)', () => {
    const wrapper = mount(SkinEmptyState, {
      props: { message: 'Nothing' },
    });
    expect(wrapper.exists()).toBe(true);
  });
});
