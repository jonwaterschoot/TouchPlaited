// Compact topbar: a single hamburger button opening a dropdown menu with the
// transports, panel toggles, and fullscreen — sized so future entries (manual
// mode, repository link, …) slot in via addMenuItem(). Hidden entirely with
// ?bare (for OBS overlay use).

import type { DeviceStore } from '../core/state';
import type { Transport } from '../transport/transport';
import { MidiTransport } from '../transport/midi';
import { MockTransport } from '../transport/mock';

export class Toolbar {
  private el: HTMLDivElement;
  private menu: HTMLDivElement;
  private links: HTMLDivElement;
  private statusEl: HTMLDivElement;
  private active: Transport | null = null;

  constructor(parent: HTMLElement, private store: DeviceStore) {
    this.el = document.createElement('div');
    this.el.className = 'toolbar';

    const burger = document.createElement('button');
    burger.className = 'menu-toggle';
    burger.textContent = '☰';
    burger.title = 'Menu';
    burger.onclick = () => this.menu.classList.toggle('open');

    this.menu = document.createElement('div');
    this.menu.className = 'menu';

    this.statusEl = document.createElement('div');
    this.statusEl.className = 'menu-status';
    this.statusEl.textContent = 'idle';

    // cross-links to the other pages of the hosted site (relative, so they
    // work under any base path; harmless no-ops on a dev server)
    this.links = document.createElement('div');
    this.links.className = 'menu-links';
    for (const [label, href] of [
      ['Home', '../'],
      ['Editor', '../editor/'],
      ['Manual', '../manual.html'],
      ['GitHub', 'https://github.com/jonwaterschoot/TouchPlaited'],
    ]) {
      const a = document.createElement('a');
      a.textContent = label;
      a.href = href;
      this.links.appendChild(a);
    }

    this.menu.append(
      this.item('Connect MIDI', () => this.start(new MidiTransport(store))),
      this.item('Demo', () => this.start(new MockTransport(store))),
    );
    if (document.documentElement.requestFullscreen) {
      this.menu.appendChild(
        this.item('Fullscreen', () => {
          if (document.fullscreenElement) void document.exitFullscreen();
          else void document.documentElement.requestFullscreen();
        }),
      );
    }
    this.menu.append(this.links, this.statusEl);

    this.el.append(burger, this.menu);
    parent.appendChild(this.el);

    // choosing an entry closes the menu; so does clicking anywhere else
    this.menu.addEventListener('click', (e) => {
      if ((e.target as Element).closest('button')) this.menu.classList.remove('open');
    });
    document.addEventListener('pointerdown', (e) => {
      if (!this.el.contains(e.target as Node)) this.menu.classList.remove('open');
    });
  }

  /** Add an app entry above the site links (MIDI drawer toggle today;
   * manual mode later). Give it class "menu-item". */
  addMenuItem(el: HTMLElement) {
    this.menu.insertBefore(el, this.links);
  }

  openMenu() {
    this.menu.classList.add('open');
  }

  private item(label: string, onClick: () => void): HTMLButtonElement {
    const b = document.createElement('button');
    b.className = 'menu-item';
    b.textContent = label;
    b.onclick = onClick;
    return b;
  }

  async start(t: Transport) {
    this.active?.disconnect();
    this.active = t;
    try {
      await t.connect();
      this.statusEl.textContent = t.describe();
      this.store.sync();
    } catch (err) {
      this.statusEl.textContent = err instanceof Error ? err.message : String(err);
      this.active = null;
    }
  }

  hide() {
    this.el.style.display = 'none';
  }
}
