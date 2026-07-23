// True-to-hardware OLED emulation: a 128×32 SSD1306, the panel size and
// resolution this project actually plans to solder to the Daisy. It owns the
// free zone between the knob columns that oled.ts (now the wider secondary
// screen, see oled-wide.ts) used to fill on its own.
//
// Two rows only — a small label row and a big value row, the same shape a
// firmware menu on this exact panel would use (a 6×8 fixed-width font for
// the label row, a much larger fixed-width font for the value, both common
// bitmap fonts shipped for 128×32 SSD1306s). The row content is decided by
// a plain char budget (LABEL_CHARS below) and a shrink-to-fit value width,
// not CSS — a C port needs the same two numbers to pick between a
// Font_6x8-equivalent and a larger digit font.
//
// Rendered in two passes: text is laid out on a hidden 128×32 canvas (real
// device resolution, for accurate char-budget measurement), then rasterized
// pixel-by-pixel onto the visible canvas as individual inset squares — the
// dot-matrix grid a real panel's discrete pixels show, not a smoothly
// scaled-up vector. The visible screen is also letterboxed to the real 4:1
// (128:32) aspect ratio inside whatever box the faceplate zone offers, so
// the dots stay square instead of stretching to fill an arbitrary rect.

import type { Panel } from './panel';
import { svgToOverlay } from './overlay-utils';

const W = 128;
const H = 32;
const ASPECT = W / H;
// Font_6x8-class bitmap font: 6px advance → 21 monospace chars across 128px.
const LABEL_CHARS = 21;
const FONT = 'ui-monospace, "Consolas", "Courier New", monospace';
const COLOR = '#ffb238';
const IDLE_MS = 2200; // reverts to the status row after this long untouched

// Visible canvas resolution tracks the actual displayed size (see place()):
// each logical pixel becomes a CELL×CELL device-px square (minus GAP), CELL
// recomputed on every resize/pan/zoom so the canvas is always drawn at its
// native display resolution — no CSS scaling, so the grid never blurs.
const MIN_CELL = 3;
const MAX_CELL = 14;
const GAP = 0.22; // fraction of each cell left as the dark gap between dots
const LIT_THRESHOLD = 90; // red-channel cutoff (0-255) for "this dot is lit"

function truncate(s: string, n: number): string {
  return s.length <= n ? s : s.slice(0, n);
}

export class OledMini {
  private el: HTMLDivElement;
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  // Hidden 128×32 canvas used only for text layout/measurement — its pixels
  // get thresholded into the dot grid, it's never shown directly.
  private textCanvas: HTMLCanvasElement;
  private textCtx: CanvasRenderingContext2D;
  private active = false; // showing the last action vs. the idle status row
  private label = '';
  private value = '';
  private shownAt = 0;
  private idleLabel = '';
  private idleValue = '';
  private cell = 4; // device px per logical pixel — set for real by place()

  constructor(private overlay: HTMLElement, private panel: Panel, private screen: {
    x: number; y: number; w: number; h: number;
  }) {
    this.el = document.createElement('div');
    this.el.className = 'oled-mini';
    this.canvas = document.createElement('canvas');
    this.canvas.width = W * this.cell;
    this.canvas.height = H * this.cell;
    this.el.appendChild(this.canvas);
    overlay.appendChild(this.el);
    this.ctx = this.canvas.getContext('2d')!;
    this.textCanvas = document.createElement('canvas');
    this.textCanvas.width = W;
    this.textCanvas.height = H;
    this.textCtx = this.textCanvas.getContext('2d', { willReadFrequently: true })!;
    this.draw();
    this.place();
    window.addEventListener('resize', () => this.place());
    let raf = 0;
    window.addEventListener('tp-panel-layout', () => {
      if (raf) return;
      raf = requestAnimationFrame(() => {
        raf = 0;
        this.place();
      });
    });
    // Idle timeout: falls back to the status row like a real device's home
    // screen, checked on a slow tick rather than a one-shot timer so a burst
    // of activity doesn't need to keep rescheduling anything.
    setInterval(() => {
      if (this.active && performance.now() - this.shownAt > IDLE_MS) {
        this.active = false;
        this.draw();
      }
    }, 300);
  }

  /** The one thing currently happening — a knob turn, a pad hint, a switch
   * flip. Replaces whatever was showing; there's no history on this screen,
   * only the live callouts used to work this way. */
  show(label: string, value: string) {
    this.active = true;
    this.label = label;
    this.value = value;
    this.shownAt = performance.now();
    this.draw();
  }

  /** Value lands a frame after the label (model name after an S35 turn). */
  amendValue(value: string) {
    if (!this.active) return;
    this.value = value;
    this.shownAt = performance.now();
    this.draw();
  }

  /** Home-screen content: what shows when nothing's been touched recently. */
  setStatus(label: string, value: string) {
    this.idleLabel = label;
    this.idleValue = value;
    if (!this.active) this.draw();
  }

  private draw() {
    const label = this.active ? this.label : this.idleLabel;
    // A live action and the idle status never blend — a dead-knob line (no
    // %, just "no effect on …") shows on its own rather than borrowing the
    // idle row's value underneath it.
    const value = this.active ? this.value : this.idleValue;
    this.layoutText(label, value);
    this.rasterize();
  }

  /** Lay text out at true device resolution (128×32) so char budgets and
   * the shrink-to-fit value measurement match real font metrics. */
  private layoutText(label: string, value: string) {
    const ctx = this.textCtx;
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = COLOR;
    ctx.textBaseline = 'top';

    ctx.font = `8px ${FONT}`;
    ctx.fillText(truncate(label.toUpperCase(), LABEL_CHARS), 1, 1);

    if (value) {
      const px = this.fitFont(value, W - 2, 20, 9);
      ctx.font = `bold ${px}px ${FONT}`;
      ctx.fillText(value, 1, 32 - px - 2);
    }
  }

  /** Largest font size (px, stepping down) that fits `text` in `maxWidth` —
   * the value row never truncates, it shrinks instead. */
  private fitFont(text: string, maxWidth: number, maxPx: number, minPx: number): number {
    const ctx = this.textCtx;
    for (let px = maxPx; px > minPx; px--) {
      ctx.font = `bold ${px}px ${FONT}`;
      if (ctx.measureText(text).width <= maxWidth) return px;
    }
    return minPx;
  }

  /** Threshold the laid-out text into an on/off grid and paint each lit
   * pixel as an inset square — the visible dot-matrix, gaps and all. */
  private rasterize() {
    const { data } = this.textCtx.getImageData(0, 0, W, H);
    const ctx = this.ctx;
    const cell = this.cell;
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.fillStyle = COLOR;
    const dot = cell * (1 - GAP);
    const inset = (cell - dot) / 2;
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        const lit = data[(y * W + x) * 4] > LIT_THRESHOLD; // red channel
        if (!lit) continue;
        ctx.fillRect(x * cell + inset, y * cell + inset, dot, dot);
      }
    }
  }

  /** Map `screen` (SVG user units) to overlay px, then letterbox to the
   * real 128:32 aspect ratio inside that box — the faceplate zone isn't
   * that exact shape, and stretching it would turn the dots into oblongs.
   * The canvas is also resized to match 1:1 with device px at that size
   * (times devicePixelRatio), so the dot grid is drawn at its final
   * resolution directly — nothing is left for the browser to rescale. */
  private place() {
    const a = svgToOverlay(this.panel.svg, this.overlay, this.screen.x, this.screen.y);
    const b = svgToOverlay(
      this.panel.svg, this.overlay, this.screen.x + this.screen.w, this.screen.y + this.screen.h);
    const boxW = b.x - a.x;
    const boxH = b.y - a.y;
    let w = boxW;
    let h = w / ASPECT;
    if (h > boxH) {
      h = boxH;
      w = h * ASPECT;
    }
    this.el.style.left = `${(a.x + (boxW - w) / 2).toFixed(1)}px`;
    this.el.style.top = `${(a.y + (boxH - h) / 2).toFixed(1)}px`;
    this.el.style.width = `${w.toFixed(1)}px`;
    this.el.style.height = `${h.toFixed(1)}px`;

    const dpr = window.devicePixelRatio || 1;
    const cell = Math.min(MAX_CELL, Math.max(MIN_CELL, Math.round((w * dpr) / W)));
    if (cell !== this.cell || this.canvas.width !== W * cell) {
      this.cell = cell;
      this.canvas.width = W * cell;
      this.canvas.height = H * cell;
      this.rasterize();
    }
  }
}
