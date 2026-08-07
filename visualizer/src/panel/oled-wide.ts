// Wide secondary screen sitting on top of the Daisy silhouette on the
// faceplate drawing — the expanded, multi-line readout this project started
// with before oled-mini.ts took over as the primary (and more physically
// honest) 128×32 screen between the knob columns. Kept around as an
// optional "more detail" companion: a dim status strip on top (model · mode
// · transport, like an SSD1306's yellow row) and the last few actions below
// it, newest at the bottom. Toggled from the menu (see labels.ts) and
// remembered across sessions.
//
// An HTML overlay div (not SVG text) so it can render the same html the
// action log uses; positioned through the SVG viewBox so it rides the
// device drawing's drag/pinch transform.

import type { Panel } from './panel';
import { svgToOverlay, labelScale, LABEL_SCALE_EVENT, splitLabelValue } from './overlay-utils';

// Screen rect in SVG user units: over the "Daisy" group on the faceplate
// (id="Daisy" bbox x 60.17–213.33, y 38.84–89.86 in panel.svg) — a couple of
// units bigger on the top, left and bottom so the board's edges (header pins,
// USB) don't peek out from behind it, and clear of the "TouCH" logo letters
// above it (id="pad-p10"/"pad-p11", bbox bottom ~33.6).
//
// The right edge stops at 190 instead of covering the board out to its end,
// because the two LEDs sit at x 193.5–202.1 — and #led-user is not artwork,
// it is driven by bindings.ts and carries the limit/state blinks. Sitting the
// screen on top of it hid a signal that reads nowhere else on the drawing.
// What stays covered to the left of 190 is board only: the reset/boot buttons
// (x 169–182) have no state to show.
const SCREEN = { x: 58, y: 38, w: 132, h: 52 };
const LINES = 3;
const VISIBLE_KEY = 'tp-oled-wide-visible';

interface Line {
  key: string;
  label: string; // html: combo + function (truncates when too long)
  value: string; // plain text, pinned to the right edge, never truncated
}

export class OledWide {
  private el: HTMLDivElement;
  private statusEl: HTMLDivElement;
  private linesEl: HTMLDivElement;
  private lines: Line[] = [];
  private visible: boolean;

  constructor(private overlay: HTMLElement, private panel: Panel) {
    this.el = document.createElement('div');
    this.el.className = 'oled-wide';
    this.visible = localStorage.getItem(VISIBLE_KEY) !== '0';
    this.el.classList.toggle('hidden', !this.visible);
    this.statusEl = document.createElement('div');
    this.statusEl.className = 'oled-status';
    this.linesEl = document.createElement('div');
    this.linesEl.className = 'oled-lines';
    this.el.append(this.statusEl, this.linesEl);
    overlay.appendChild(this.el);
    this.render();
    this.place();
    window.addEventListener('resize', () => this.place());
    window.addEventListener(LABEL_SCALE_EVENT, () => this.place());
    // Follow the device drawing's drag/pinch (layout.ts fires per move) —
    // rAF-throttled like the static labels.
    let raf = 0;
    window.addEventListener('tp-panel-layout', () => {
      if (raf) return;
      raf = requestAnimationFrame(() => {
        raf = 0;
        this.place();
      });
    });
  }

  /** Top strip: the same model/mode/transport line the info panel shows. */
  setStatus(html: string) {
    this.statusEl.innerHTML = html;
  }

  /** Add or update an action line. The same key updating the newest line
   * keeps a knob turn streaming into one line; a key resurfacing from an
   * older line moves back to the bottom (recency order, no duplicates). */
  push(key: string, html: string) {
    const { label, value } = splitLabelValue(html);
    const line = { key, label, value };
    const newest = this.lines[this.lines.length - 1];
    if (newest?.key === key) this.lines[this.lines.length - 1] = line;
    else this.lines = [...this.lines.filter((l) => l.key !== key), line].slice(-LINES);
    this.render();
  }

  /** Replace the newest line's value (the model name landing one frame
   * after an S35 turn) — mirrors the action log's amend-in-place. */
  amendValue(key: string, value: string) {
    const newest = this.lines[this.lines.length - 1];
    if (!newest || newest.key !== key) return;
    newest.value = value;
    this.render();
  }

  setVisible(v: boolean) {
    this.visible = v;
    this.el.classList.toggle('hidden', !v);
  }

  isVisible(): boolean {
    return this.visible;
  }

  private render() {
    this.linesEl.innerHTML = this.lines.length
      ? this.lines
          .map((l) =>
            `<div class="oled-line"><span class="t">${l.label}</span>` +
            (l.value ? `<span class="v">${l.value}</span>` : '') + '</div>')
          .join('')
      : '<div class="oled-line idle"><span class="t">— ready —</span></div>';
  }

  /** Map SCREEN (SVG user units) to overlay px. */
  private place() {
    const a = svgToOverlay(this.panel.svg, this.overlay, SCREEN.x, SCREEN.y);
    const b = svgToOverlay(
      this.panel.svg, this.overlay, SCREEN.x + SCREEN.w, SCREEN.y + SCREEN.h);
    this.el.style.left = `${a.x.toFixed(1)}px`;
    this.el.style.top = `${a.y.toFixed(1)}px`;
    this.el.style.width = `${(b.x - a.x).toFixed(1)}px`;
    this.el.style.height = `${(b.y - a.y).toFixed(1)}px`;
    // Text scales with the screen and the user's A−/A+ setting. When it's
    // cranked past what three lines fit, the oldest line clips off the top
    // (bottom-anchored) — an OLED running out of rows, not a layout bug.
    this.el.style.fontSize =
      `${(((b.x - a.x) / 14) * labelScale()).toFixed(2)}px`;
  }
}
