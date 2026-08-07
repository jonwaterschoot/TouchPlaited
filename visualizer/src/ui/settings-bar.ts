// One bar for the display settings, pinned to the stage's top-left corner.
//
// These buttons used to live in two places that had each grown their own row:
// a cluster pinned to the device drawing (label/screen text size + reset) and
// the info panel's title bar (overlay mode, info font size, reset). Same kind
// of control, two homes, two idioms — and neither row was where you'd look
// for "settings". They are one bar now, and the two things that genuinely
// belong to a moved object stay with it: the drawing keeps its ⠿ grip and the
// info panel keeps its ⠿ title bar, because a drag handle has to be on the
// thing it drags.
//
// The bar is fixed to the stage rather than following the drawing: it is
// chrome, not part of the device, and a settings row that slides around while
// you pan is exactly what made the old cluster hard to aim at. ⚙ collapses it
// to a single icon (persisted) so it can get out of a video shot; on phone-
// sized viewports it starts collapsed.

const OPEN_KEY = 'tp-settings-open';
/** Collapsing the bar changes its footprint — anything that keeps out from
 * under it (the drawing's drag grip) re-places on this. */
export const SETTINGS_TOGGLE_EVENT = 'tp-settings-toggle';

export class SettingsBar {
  private el: HTMLDivElement;
  private body: HTMLDivElement;
  private groups: HTMLDivElement;
  private resets: (() => void)[] = [];
  private open: boolean;

  constructor(parent: HTMLElement) {
    this.el = document.createElement('div');
    this.el.className = 'settings-bar';

    const gear = document.createElement('button');
    gear.className = 'settings-gear';
    gear.textContent = '⚙';
    gear.title = 'Show/hide the display settings';

    this.body = document.createElement('div');
    this.body.className = 'settings-body';
    this.groups = document.createElement('div');
    this.groups.className = 'settings-groups';

    const reset = SettingsBar.button('⟲', 'Reset the drawing, the info panel and the text sizes', () => {
      for (const fn of this.resets) fn();
    });
    reset.classList.add('settings-reset');
    this.body.append(this.groups, reset);

    const stored = localStorage.getItem(OPEN_KEY);
    this.open = stored !== null
      ? stored === '1'
      : !matchMedia('(max-width: 820px), (max-height: 500px)').matches;
    this.applyOpen();
    gear.addEventListener('click', () => {
      this.open = !this.open;
      localStorage.setItem(OPEN_KEY, this.open ? '1' : '0');
      this.applyOpen();
    });

    this.el.append(gear, this.body);
    parent.appendChild(this.el);
  }

  /** A captioned run of buttons. `order` fixes the left-to-right sequence
   * independently of who constructs first — the bar's contributors are
   * scattered across layout.ts and labels.ts, and their construction order is
   * a wiring detail, not a design decision. */
  addGroup(caption: string, order: number, ...els: HTMLElement[]) {
    const g = document.createElement('div');
    g.className = 'settings-group';
    g.dataset.order = String(order);
    if (caption) {
      const cap = document.createElement('span');
      cap.className = 'settings-cap';
      cap.textContent = caption;
      g.appendChild(cap);
    }
    g.append(...els);
    const after = [...this.groups.children].find(
      (c) => Number((c as HTMLElement).dataset.order) > order,
    );
    this.groups.insertBefore(g, after ?? null);
  }

  /** Called by the one ⟲ — every contributor restores its own defaults. */
  onReset(fn: () => void) {
    this.resets.push(fn);
  }

  static button(txt: string, title: string, onClick: () => void): HTMLButtonElement {
    const b = document.createElement('button');
    b.textContent = txt;
    b.title = title;
    // The drawing under the bar captures pointerdown for its drag gesture;
    // without this the click never lands (the same trap the info panel's
    // buttons had to work around).
    b.addEventListener('pointerdown', (e) => e.stopPropagation());
    b.addEventListener('click', (e) => {
      e.stopPropagation();
      onClick();
    });
    return b;
  }

  hide() {
    this.el.style.display = 'none';
  }

  /** Viewport rect, for the few things that have to stay out from under the
   * bar — the drawing's drag grip pins itself to the same corner. */
  bounds(): DOMRect {
    return this.el.getBoundingClientRect();
  }

  private applyOpen() {
    this.el.classList.toggle('open', this.open);
    window.dispatchEvent(new Event(SETTINGS_TOGGLE_EVENT));
  }
}
