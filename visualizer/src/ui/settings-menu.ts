// The display settings, as a "Settings" section of the ☰ menu.
//
// These used to be a bar of their own pinned to the stage's top-left with a ⚙
// toggle, while the ☰ menu owned the top-right. On a phone the open bar ran
// into the ☰ button, and two corner controls for "how the page looks" was one
// too many anyway — so the settings now live in the one menu. The two things
// that belong to a moved object stay with it: the drawing keeps its ⠿ grip and
// the info panel keeps its ⠿ title bar, because a drag handle has to be on the
// thing it drags.
//
// Each contributor adds a captioned row of buttons; the single ⟲ at the bottom
// resets all of them. The buttons stop their clicks from bubbling, so A−/A+
// can be pressed repeatedly without the menu closing under you.

export class SettingsMenu {
  private groups: HTMLDivElement;
  private resets: (() => void)[] = [];

  constructor(section: HTMLElement) {
    this.groups = document.createElement('div');
    this.groups.className = 'settings-groups';

    const reset = document.createElement('button');
    reset.className = 'menu-item';
    reset.textContent = '⟲ Reset layout & text sizes';
    reset.title = 'Reset the drawing, the info panel and the text sizes';
    reset.addEventListener('click', () => {
      for (const fn of this.resets) fn();
    });

    section.append(this.groups, reset);
  }

  /** A captioned run of buttons. `order` fixes the top-to-bottom sequence
   * independently of who constructs first — the contributors are scattered
   * across layout.ts and labels.ts, and their construction order is a wiring
   * detail, not a design decision. */
  addGroup(caption: string, order: number, ...els: HTMLElement[]) {
    const g = document.createElement('div');
    g.className = 'settings-group';
    g.dataset.order = String(order);
    const cap = document.createElement('span');
    cap.className = 'settings-cap';
    cap.textContent = caption;
    const btns = document.createElement('div');
    btns.className = 'settings-btns';
    btns.append(...els);
    g.append(cap, btns);
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
    // Keeps the menu open (it closes on any button click that reaches it).
    b.addEventListener('click', (e) => {
      e.stopPropagation();
      onClick();
    });
    return b;
  }
}
