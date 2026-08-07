// Compact topbar: a single hamburger button opening a dropdown menu. The menu
// is grouped rather than flat — it had grown into one list where "Demo",
// "Fullscreen", "Expanded display: On" and a row of site links all looked like
// the same kind of thing, and the links that navigate away from the app read
// as buttons. Three sections now, each captioned:
//
//   Connect   the transports, with the connection status under them
//   Display   everything that changes what you see, incl. fullscreen/wake lock
//   Elsewhere the other pages of the site — rows with a → , not buttons
//
// New entries slot in via addMenuItem(el, section) / addAction(). Hidden
// entirely with ?bare (for OBS overlay use).

import type { DeviceStore } from '../core/state';
import type { Transport } from '../transport/transport';
import { MidiTransport } from '../transport/midi';
import { MockTransport } from '../transport/mock';

export type MenuSection = 'connect' | 'view';

const WAKE_KEY = 'tp-wake-lock';

export class Toolbar {
  private el: HTMLDivElement;
  private menu: HTMLDivElement;
  private sections: Record<MenuSection, HTMLDivElement>;
  private statusEl: HTMLDivElement;
  private active: Transport | null = null;
  private wakeLock: WakeLockSentinel | null = null;
  private wakeWanted = false;
  private wakeBtn: HTMLButtonElement | null = null;

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

    this.sections = {
      connect: this.section('Connect'),
      view: this.section('Display'),
    };
    this.sections.connect.append(
      this.item('Connect MIDI', () => this.start(new MidiTransport(store))),
      this.item('Demo', () => this.start(new MockTransport(store))),
      this.statusEl,
    );

    // Fullscreen is feature-detected because iPhone Safari has no Fullscreen
    // API at all (iPad and video elements only) — which is also why the wake
    // lock below matters there rather than being a nicety: on a phone,
    // fullscreen isn't available to keep the screen up even indirectly.
    if (document.documentElement.requestFullscreen) {
      this.sections.view.appendChild(
        this.item('Fullscreen', () => {
          if (document.fullscreenElement) void document.exitFullscreen();
          else void document.documentElement.requestFullscreen();
        }),
      );
    }
    this.initWakeLock();

    // The other pages of the hosted site: relative hrefs, so they work under
    // any base path (harmless no-ops on a dev server). Real <a> rows with a
    // trailing arrow — these leave the app, and nothing else in the menu does.
    const links = this.section('Elsewhere');
    links.classList.add('menu-links');
    for (const [label, href, external] of [
      ['Home', '../', false],
      ['Manual', '../manual.html', false],
      ['Pattern editor', '../editor/', false],
      ['Code map', '../codemap/', false],
      ['Source on GitHub', 'https://github.com/jonwaterschoot/TouchPlaited', true],
    ] as const) {
      const a = document.createElement('a');
      a.className = 'menu-link';
      a.href = href;
      a.innerHTML = `<span>${label}</span><span class="menu-arrow">${external ? '↗' : '→'}</span>`;
      if (external) {
        a.target = '_blank';
        a.rel = 'noopener';
      }
      links.appendChild(a);
    }

    this.menu.append(this.sections.connect, this.sections.view, links);
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

  /** Add an app entry to a section (Display unless told otherwise). Give it
   * class "menu-item". */
  addMenuItem(el: HTMLElement, section: MenuSection = 'view') {
    this.sections[section].appendChild(el);
  }

  /** Same, for the common case of a plain labelled action. */
  addAction(label: string, onClick: () => void, section: MenuSection = 'view'): HTMLButtonElement {
    const b = this.item(label, onClick);
    this.addMenuItem(b, section);
    return b;
  }

  openMenu() {
    this.menu.classList.add('open');
  }

  private section(caption: string): HTMLDivElement {
    const s = document.createElement('div');
    s.className = 'menu-section';
    const c = document.createElement('div');
    c.className = 'menu-caption';
    c.textContent = caption;
    s.appendChild(c);
    return s;
  }

  private item(label: string, onClick: () => void): HTMLButtonElement {
    const b = document.createElement('button');
    b.className = 'menu-item';
    b.textContent = label;
    b.onclick = onClick;
    return b;
  }

  /** Screen Wake Lock — the phone screen sleeping mid-session is the one
   * thing that actually breaks mobile use, and the API needs a user gesture,
   * so it belongs on a menu entry rather than being applied on load. The wish
   * is remembered; the lock itself is re-taken whenever the page comes back
   * to the foreground (the browser drops it on every tab switch, screen
   * blank and orientation change) and, after a reload, on the first tap
   * anywhere — which is the earliest gesture we are allowed to use. */
  private initWakeLock() {
    const supported = 'wakeLock' in navigator;
    const label = () =>
      `Keep screen awake: ${!supported ? 'n/a' : this.wakeWanted ? 'On' : 'Off'}`;
    const btn = this.item('', () => {
      this.wakeWanted = !this.wakeWanted;
      localStorage.setItem(WAKE_KEY, this.wakeWanted ? '1' : '0');
      btn.textContent = label();
      if (this.wakeWanted) void this.acquireWake();
      else void this.releaseWake();
    });
    this.wakeBtn = btn;
    btn.textContent = label();
    if (!supported) {
      btn.disabled = true;
      btn.title =
        'This browser has no Screen Wake Lock API (Safari before iOS 16.4). ' +
        'Nothing else can hold the screen up — raise the system sleep timeout.';
      this.sections.view.appendChild(btn);
      return;
    }
    btn.title = 'Hold the screen on while the visualizer is in front';
    this.sections.view.appendChild(btn);

    this.wakeWanted = localStorage.getItem(WAKE_KEY) === '1';
    btn.textContent = label();
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible' && this.wakeWanted && !this.wakeLock)
        void this.acquireWake();
    });
    // Re-arm after a reload: a stored wish can't be honoured until the user
    // touches something. Once is enough — visibilitychange covers the rest.
    if (this.wakeWanted) {
      const arm = () => {
        document.removeEventListener('pointerdown', arm);
        if (this.wakeWanted && !this.wakeLock) void this.acquireWake();
      };
      document.addEventListener('pointerdown', arm);
    }
  }

  private async acquireWake() {
    try {
      this.wakeLock = await navigator.wakeLock.request('screen');
      // The browser releases it on its own (backgrounding, low battery);
      // forget the sentinel so the next visibility change re-takes it.
      this.wakeLock.addEventListener('release', () => {
        this.wakeLock = null;
      });
    } catch (err) {
      this.wakeLock = null;
      this.wakeWanted = false;
      localStorage.setItem(WAKE_KEY, '0');
      if (this.wakeBtn) this.wakeBtn.textContent = 'Keep screen awake: failed';
      this.statusEl.textContent = err instanceof Error ? err.message : String(err);
    }
  }

  private async releaseWake() {
    const l = this.wakeLock;
    this.wakeLock = null;
    try {
      await l?.release();
    } catch {
      /* already gone */
    }
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
