// True-to-hardware OLED emulation: a 128×32 SSD1306, the panel size and
// resolution this project actually plans to solder to the Daisy. It owns the
// free zone between the knob columns that oled.ts (now the wider secondary
// screen, see oled-wide.ts) used to fill on its own.
//
// Two rows only — a small label row and a big value row, the same shape a
// firmware menu on this exact panel would use. Both rows are drawn with the
// firmware's actual bitmap fonts (oled-font-data.ts, ported verbatim from
// lib/libDaisy/src/util/oled_fonts.c) using the exact same font-selection
// rule as display/oled_screen.cpp:ShowLine() — Font_6x8 for the label row,
// stepping Font_11x18 -> Font_7x10 -> Font_6x8 for the value row until the
// string's char count fits. Char budgets (LABEL_CHARS, VALUE font/maxChars
// table) mirror oled_screen.cpp exactly; a C-side change to that stepping
// needs the same change made here.
//
// Text is laid out into a 128×32 boolean bit-grid — one bit per real device
// pixel, set directly from the font's glyph data — then rasterized onto the
// visible canvas as individual inset squares, the dot-matrix grid a real
// panel's discrete pixels show. No text rendering pipeline (no anti-aliased
// glyphs, no threshold) sits between the font data and the dots: what's lit
// here is exactly what the hardware's WriteChar() would light.

import type { Panel } from './panel';
import { svgToOverlay } from './overlay-utils';
import { FONT_6X8, FONT_7X10, FONT_11X18, glyphPixel, type BitmapFont } from './oled-font-data';

const W = 128;
const H = 32;
const ASPECT = W / H;
// Font_6x8 budget: 6px advance -> 21 monospace chars across 128px. Matches
// kBufLen/LABEL_CHARS in oled_screen.cpp.
const LABEL_CHARS = Math.floor(W / FONT_6X8.width);
const COLOR = '#ffb238';
const IDLE_MS = 2200; // reverts to the status row after this long untouched
// Matches the firmware's kConfirmFlashMs (display/oled_ui.cpp) — held open
// past the usual redraw cadence so a confirm actually reads as a flash.
const CONFIRM_FLASH_MS = 220;

// Visible canvas resolution tracks the actual displayed size (see place()):
// each logical pixel becomes a CELL×CELL device-px square (minus GAP), CELL
// recomputed on every resize/pan/zoom so the canvas is always drawn at its
// native display resolution — no CSS scaling, so the grid never blurs.
const MIN_CELL = 3;
const MAX_CELL = 14;
const GAP = 0.22; // fraction of each cell left as the dark gap between dots

function truncate(s: string, n: number): string {
  return s.length <= n ? s : s.slice(0, n);
}

/** Right-aligned indicator block on the label row — mirrors StatusIcons in
 * display/oled_screen.h, same geometry and same blink period. The only thing
 * on this screen that survives whatever callout is showing. */
export interface StatusIcons {
  rec: boolean;      // capture live: a circle, top right, blinking
  layers: number;    // committed NoteRec layers, -1 = draw no dots
  mute: number;      // bit i = layer i muted
  open: number;      // layer being recorded into, -1 = none
}
const NO_ICONS: StatusIcons = { rec: false, layers: -1, mute: 0, open: -1 };
const REC_CX = 123, REC_CY = 3, REC_R = 3;
const DOT_X0 = 88, DOT_PITCH = 6, DOT_W = 4;
const LABEL_CHARS_DOTS = 14;
const LABEL_CHARS_REC = 19;
// Matches kBlinkMs in display/oled_ui.cpp.
const BLINK_MS = 400;
// Matches OledScreen::kListRows — 32 px / an 8 px font.
const LIST_ROWS = 4;

/** Same stepping as oled_screen.cpp:ShowLine() — largest font whose char
 * count fits, in char-count terms (not measured pixel width, matching the
 * hardware exactly since these are fixed-advance bitmap fonts). */
function pickValueFont(len: number): BitmapFont {
  if (len <= Math.floor(W / FONT_11X18.width)) return FONT_11X18;
  if (len <= Math.floor(W / FONT_7X10.width)) return FONT_7X10;
  return FONT_6X8;
}

export class OledMini {
  private el: HTMLDivElement;
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  // 128×32 bit-grid, one boolean per real device pixel — the "screen" the
  // hardware's WriteChar() would actually light.
  private bits = new Uint8Array(W * H);
  private active = false; // showing the last action vs. the idle status row
  private label = '';
  private value = '';
  private shownAt = 0;
  private idleLabel = '';
  private idleValue = '';
  private progressMode = false; // showing a hold's progress bar, not label/value
  private progressLabel = '';
  private progressPct = 0; // 0..1
  private progressNote = ''; // what crossing the next threshold does
  private flashTimer: ReturnType<typeof setTimeout> | null = null; // confirm text owns the screen
  private icons: StatusIcons = NO_ICONS;
  private listRows: string[] | null = null; // combo list owns the screen
  private blinkPhase = false;
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
      if (this.listRows !== null) return;  // held modifier owns the screen
      if (this.active && performance.now() - this.shownAt > IDLE_MS) {
        this.active = false;
        this.progressMode = false;
        this.draw();
      }
    }, 300);
    // Capture-indicator pulse. The firmware forces the same redraw from
    // OledUi::Service when the phase flips (kBlinkMs); here it's a timer,
    // and like there it only runs while something is actually animating.
    setInterval(() => {
      if (!this.icons.rec && this.icons.open < 0) return;
      const phase = (Math.floor(performance.now() / BLINK_MS) % 2) !== 0;
      if (phase === this.blinkPhase) return;
      this.blinkPhase = phase;
      if (!this.progressMode && this.flashTimer === null) this.draw();
    }, BLINK_MS / 4);
  }

  /** The one thing currently happening — a knob turn, a pad hint, a switch
   * flip. Replaces whatever was showing; there's no history on this screen,
   * only the live callouts used to work this way. A hold in progress (or its
   * confirm flash) owns the screen unconditionally on real hardware
   * (OledUi::Service checks hold_kind before anything else and returns
   * early) — mirrored here by simply ignoring show() while progressMode or
   * a flash is active, rather than letting whichever call happens to land
   * last win. */
  show(label: string, value: string) {
    if (this.progressMode || this.flashTimer !== null) return;
    this.listRows = null;
    this.active = true;
    this.label = label;
    this.value = value;
    this.shownAt = performance.now();
    this.draw();
  }

  /** Value lands a frame after the label (model name after an S35 turn). */
  amendValue(value: string) {
    if (!this.active || this.progressMode || this.flashTimer !== null) return;
    this.value = value;
    this.shownAt = performance.now();
    this.draw();
  }

  /** A hold building toward a threshold (display/oled_ui.cpp's hold_kind) —
   * label row as show(), value row replaced by a filled bar. Fires on every
   * STATE frame while the hold is live, same as a continuously-turning
   * knob would, so the bar visibly fills. */
  showProgress(label: string, pct: number, note = '') {
    if (this.flashTimer !== null) return; // a confirm is still on screen
    this.listRows = null;
    this.active = true;
    this.progressMode = true;
    this.progressLabel = label;
    this.progressPct = pct;
    this.progressNote = note;
    this.shownAt = performance.now();
    this.draw();
  }

  /** A threshold just fired (display/oled_ui.cpp's hold_stage edge) — label
   * row as usual, value row replaced by confirm text for CONFIRM_FLASH_MS,
   * matching the firmware's own held-open redraw throttle, then releases
   * back to whatever showProgress()/show() calls land next. */
  confirmFlash(label: string, text: string) {
    this.listRows = null;
    this.active = true;
    this.progressMode = false;
    this.shownAt = performance.now();
    this.layoutText(label, text);
    this.rasterize();
    if (this.flashTimer !== null) clearTimeout(this.flashTimer);
    this.flashTimer = setTimeout(() => {
      this.flashTimer = null;
      this.shownAt = performance.now();
      this.draw();
    }, CONFIRM_FLASH_MS);
  }

  /** Releases the screen once hold_kind returns to 0, and hands it straight
   * back to the status row — matching OledUi::Service, which forces that
   * redraw on the same edge so a finished bar can't sit frozen on the panel
   * (a hold's end changes nothing else the priority chain watches). */
  clearProgress() {
    if (!this.progressMode) return;
    this.progressMode = false;
    this.active = false;
    if (this.flashTimer === null) this.draw();
  }

  /** Four rows of Font_6x8 — the whole 32 px height, no label/value split.
   * The combo list a held modifier shows; only the first LIST_ROWS entries
   * are drawn, so a caller scrolls by advancing the slice it passes.
   * Mirrors OledScreen::ShowList() (display/oled_screen.cpp). */
  showList(rows: string[]) {
    this.active = true;
    this.progressMode = false;
    this.listRows = rows;
    this.shownAt = performance.now();
    this.draw();
  }

  /** Releases a combo list when its modifier comes up. */
  clearList() {
    if (this.listRows === null) return;
    this.listRows = null;
    this.active = false;
    this.draw();
  }

  private layoutList(rows: string[]) {
    this.bits.fill(0);
    const shown = Math.min(rows.length, LIST_ROWS);
    for (let i = 0; i < shown; i++)
      this.blitText(FONT_6X8, truncate(rows[i].toUpperCase(), LABEL_CHARS),
                    1, i * FONT_6X8.height);
  }

  /** Home-screen content: what shows when nothing's been touched recently. */
  setStatus(label: string, value: string) {
    this.idleLabel = label;
    this.idleValue = value;
    if (!this.active) this.draw();
  }

  private draw() {
    if (this.active && this.listRows !== null) {
      this.layoutList(this.listRows);
      this.rasterize();
      return;
    }
    if (this.active && this.progressMode) {
      this.layoutProgress(this.progressLabel, this.progressPct, this.progressNote);
      this.rasterize();
      return;
    }
    const label = this.active ? this.label : this.idleLabel;
    // A live action and the idle status never blend — a dead-knob line (no
    // %, just "no effect on …") shows on its own rather than borrowing the
    // idle row's value underneath it.
    const value = this.active ? this.value : this.idleValue;
    this.layoutText(label, value);
    this.rasterize();
  }

  /** Blit `font`'s glyphs for `text` into the bit-grid starting at (x0, y0),
   * exactly mirroring the firmware's WriteChar/WriteString (display.h). */
  private blitText(font: BitmapFont, text: string, x0: number, y0: number) {
    for (let i = 0; i < text.length; i++) {
      const cx = x0 + i * font.width;
      if (cx + font.width > W) break;
      for (let row = 0; row < font.height; row++) {
        const y = y0 + row;
        if (y < 0 || y >= H) continue;
        for (let col = 0; col < font.width; col++) {
          if (glyphPixel(font, text[i], col, row)) this.bits[y * W + cx + col] = 1;
        }
      }
    }
  }

  /** Lay text into the 128×32 bit-grid using the firmware's exact font
   * selection: Font_6x8 for the label row, Font_11x18 -> Font_7x10 ->
   * Font_6x8 for the value row (oled_screen.cpp:ShowLine). */
  private layoutText(label: string, value: string) {
    this.bits.fill(0);

    // Indicator block first — it owns the right end of the label row, so the
    // label's budget depends on what's drawn here.
    const ic = this.icons;
    let budget = LABEL_CHARS;
    const blink = (Math.floor(performance.now() / BLINK_MS) % 2) !== 0;
    if (ic.layers >= 0) {
      budget = LABEL_CHARS_DOTS;
      for (let i = 0; i < 5; i++) {
        const x0 = DOT_X0 + i * DOT_PITCH;
        const x1 = x0 + DOT_W - 1;
        if (i === ic.open) {
          // The take being recorded into pulses with the rec circle.
          if (blink) this.drawRectFilled(x0, 2, x1, 5);
          else this.drawRectOutline(x0, 2, x1, 5);
        } else if (i < ic.layers) {
          if (((ic.mute >> i) & 1) === 0) this.drawRectFilled(x0, 2, x1, 5);
          else this.drawRectOutline(x0, 2, x1, 5);
        } else {
          this.drawRectFilled(x0 + 1, 3, x0 + 2, 4);
        }
      }
    } else if (ic.rec) {
      budget = LABEL_CHARS_REC;
    }
    if (ic.rec && blink) this.drawCircleOutline(REC_CX, REC_CY, REC_R);

    this.blitText(FONT_6X8, truncate(label.toUpperCase(), budget), 1, 0);

    if (value) {
      const font = pickValueFont(value.length);
      const maxChars = Math.floor(W / font.width);
      this.blitText(font, truncate(value, maxChars), 1, H - font.height);
    }
  }

  /** Same midpoint-circle outline the firmware's DrawCircle() rasterizes. */
  private drawCircleOutline(cx: number, cy: number, r: number) {
    let x = r, y = 0, err = 1 - r;
    while (x >= y) {
      for (const [dx, dy] of [[x, y], [y, x], [-x, y], [-y, x],
                              [-x, -y], [-y, -x], [x, -y], [y, -x]] as const)
        this.setBit(cx + dx, cy + dy);
      y++;
      if (err < 0) err += 2 * y + 1;
      else { x--; err += 2 * (y - x) + 1; }
    }
  }

  /** The persistent indicator block; redraws immediately so a state change
   * (arming, a layer committing) shows without waiting for the blink tick. */
  setIcons(icons: StatusIcons) {
    const a = this.icons;
    if (a.rec === icons.rec && a.layers === icons.layers
        && a.mute === icons.mute && a.open === icons.open) return;
    this.icons = icons;
    if (!this.progressMode && this.flashTimer === null) this.draw();
  }

  private setBit(x: number, y: number) {
    if (x >= 0 && x < W && y >= 0 && y < H) this.bits[y * W + x] = 1;
  }

  private drawRectOutline(x1: number, y1: number, x2: number, y2: number) {
    for (let x = x1; x <= x2; x++) { this.setBit(x, y1); this.setBit(x, y2); }
    for (let y = y1; y <= y2; y++) { this.setBit(x1, y); this.setBit(x2, y); }
  }

  private drawRectFilled(x1: number, y1: number, x2: number, y2: number) {
    for (let y = y1; y <= y2; y++)
      for (let x = x1; x <= x2; x++) this.setBit(x, y);
  }

  /** Label row as layoutText(), middle row an outlined bar filled
   * left-to-right by `pct`, bottom row a Font_6x8 note saying what crossing
   * the threshold does — identical geometry to the firmware's
   * OledScreen::ShowProgress() (display/oled_screen.cpp). */
  private layoutProgress(label: string, pct: number, note: string) {
    this.bits.fill(0);
    this.blitText(FONT_6X8, truncate(label.toUpperCase(), LABEL_CHARS), 1, 0);

    const OX1 = 1, OY1 = 12, OX2 = 126, OY2 = 21;
    this.drawRectOutline(OX1, OY1, OX2, OY2);
    const FX1 = OX1 + 2, FY1 = OY1 + 2, FY2 = OY2 - 2;
    const maxW = OX2 - 2 - FX1; // inner width at 100%
    const fillW = Math.max(0, Math.min(maxW, Math.round(maxW * pct)));
    if (fillW > 0) this.drawRectFilled(FX1, FY1, FX1 + fillW - 1, FY2);

    if (note)
      this.blitText(FONT_6X8, truncate(note.toUpperCase(), LABEL_CHARS), 1, H - FONT_6X8.height);
  }

  /** Paint each lit bit-grid pixel as an inset square — the visible
   * dot-matrix, gaps and all. */
  private rasterize() {
    const ctx = this.ctx;
    const cell = this.cell;
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.fillStyle = COLOR;
    const dot = cell * (1 - GAP);
    const inset = (cell - dot) / 2;
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        if (!this.bits[y * W + x]) continue;
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

    // Snap to the exact CSS size that `cell` device-px-per-logical-pixel
    // implies, rather than letterboxing to (w, h) and picking cell to fit.
    // The canvas backing store is always a whole multiple of W×H device
    // px, so if the element's CSS size doesn't land on precisely
    // cell/dpr px, the browser has to rescale the already-rasterized dot
    // grid — and nearest-neighbor scaling ("pixelated") on a non-integer
    // ratio stretches some dots to one extra physical pixel and not
    // others, which reads as uneven/blurry letterforms rather than a
    // crisp panel. Snapping first guarantees a true 1:1 device-pixel
    // mapping, so what's rasterized is exactly what's shown.
    const dpr = window.devicePixelRatio || 1;
    const cell = Math.min(MAX_CELL, Math.max(MIN_CELL, Math.round((w * dpr) / W)));
    w = (W * cell) / dpr;
    h = (H * cell) / dpr;

    this.el.style.left = `${(a.x + (boxW - w) / 2).toFixed(1)}px`;
    this.el.style.top = `${(a.y + (boxH - h) / 2).toFixed(1)}px`;
    this.el.style.width = `${w.toFixed(1)}px`;
    this.el.style.height = `${h.toFixed(1)}px`;

    if (cell !== this.cell || this.canvas.width !== W * cell) {
      this.cell = cell;
      this.canvas.width = W * cell;
      this.canvas.height = H * cell;
      this.rasterize();
    }
  }
}
