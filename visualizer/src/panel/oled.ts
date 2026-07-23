// OLED-style faceplate screen in the free zone between the knob columns.
// The dynamic callouts used to pop up next to each control and overlapped
// each other on small screens; now the control itself just lights up and
// this screen carries the text: a dim status strip on top (model · mode ·
// transport, like an SSD1306's yellow row) and the last few actions below
// it, newest at the bottom. An HTML overlay div (not SVG text) so it can
// render the same html the action log uses; positioned through the SVG
// viewBox so it rides the device drawing's drag/pinch transform.

import type { Panel } from './panel';
import { svgToOverlay, labelScale, LABEL_SCALE_EVENT } from './overlay-utils';

// Screen rect in SVG user units: the faceplate zone between the knob
// columns (S30/S31 left, S34/S35 right), below the S31–S34 row.
const SCREEN = { x: 66.1, y: 148, w: 100.1, h: 44 };
const LINES = 3;

interface Line {
  key: string;
  label: string; // html: combo + function (truncates when too long)
  value: string; // plain text, pinned to the right edge, never truncated
}

export class Oled {
  private el: HTMLDivElement;
  private statusEl: HTMLDivElement;
  private linesEl: HTMLDivElement;
  private lines: Line[] = [];

  constructor(private overlay: HTMLElement, private panel: Panel) {
    this.el = document.createElement('div');
    this.el.className = 'oled';
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
    // Split a trailing "<span>value</span>" off so the value renders
    // right-pinned and a long function name truncates before it does.
    const m = html.match(/^(.*)\s<span>([^<]*)<\/span>$/);
    const line = { key, label: m ? m[1] : html, value: m ? m[2] : '' };
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
