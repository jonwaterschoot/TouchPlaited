// Contextual label callouts ("S33 · Timbre · 64%") plus a draggable live-info
// panel (model / mode / transport / step + action log). Callouts appear next
// to whatever just changed and fade out, so a tutorial viewer's eye is guided
// to the control being used.

import type { Panel } from './panel';
import type { DeviceStore, StateEvent, DeviceState } from '../core/state';
import {
  CONTROLS, PADS, SW1_POSITIONS, SW2_POSITIONS, MODE_NAMES, modelName,
  DRUM_NOTES, noteName, fxValueLabel,
} from '../core/controls-meta';

const CALLOUT_TTL_MS = 1600;

// The device's NoteOn arrives immediately but the matching pad-down rides the
// STATE frame (rate-limited to 33 ms), so a note usually lands before its pad:
// hold it briefly and let the pad-down claim it.
const PENDING_NOTE_MS = 300;

// A fresh callout needs a deliberate move (~2 pot steps); real pots jitter
// ±1 step forever (S36 on the test unit). An already-visible callout keeps
// tracking every step so turns read smoothly.
const SHOW_EPS = 2.2 / 127;

// Default info-panel anchor: the free zone of the faceplate, as a fraction of
// the rendered SVG (top-left area left of the Daisy board).
const INFO_DEFAULT = { fx: 0.07, fy: 0.075 };
const INFO_POS_KEY = 'tp-info-pos';
const INFO_SCALE_KEY = 'tp-info-scale';

// Knob circle radius incl. ring stroke, in SVG user units — used to compute
// label anchors geometrically (the rendered bbox is useless: it wobbles with
// the pointer-line rotation).
const KNOB_R = 11;

interface Callout {
  el: HTMLDivElement;
  expiresAt: number; // Infinity = sticky (held pad)
}

interface LogEntry {
  key: string;
  html: string;
}

/** Name a control move including its modifier-pad gesture, e.g. "P0 + S35 ·
 * Model select · bank 0" — used by both the callout and the action log. */
function describeControl(i: number, s: DeviceState): { combo: string; fn: string } {
  const meta = CONTROLS[i];
  if (s.pads[1] && meta.fx)
    return { combo: `P1 + ${meta.name}`, fn: `${meta.fx} ${s.mode === 0 ? '(drums)' : '(pitched)'}` };
  if (i === 5 && s.pads[0]) return { combo: 'P0 + S35', fn: 'Model select · bank 0' };
  if (i === 5 && s.pads[2]) return { combo: 'P2 + S35', fn: 'Model select · bank 1' };
  if (i === 7 && s.pads[0]) return { combo: 'P0 + S37', fn: 'Stereo width' };
  return { combo: meta.name, fn: (s.mode === 0 ? meta.seq : meta.main) ?? meta.main };
}

/** Name a pad press including modifier gestures; null = pure modifier, not
 * worth a log line on its own. */
function describePad(i: number, s: DeviceState): { combo: string; fn: string } | null {
  const meta = PADS[i];
  if (i === 11 && s.pads[2]) return { combo: 'P2 + P11', fn: 'Seq play / pause' };
  if (i === 10 && s.pads[0]) return { combo: 'P0 + P10', fn: 'Root −1 semitone' };
  if (i === 11 && s.pads[0]) return { combo: 'P0 + P11', fn: 'Root +1 semitone' };
  if (i === 10) return { combo: 'P10', fn: s.mode === 0 ? 'Drum pitch −1' : 'Octave −' };
  if (i === 11) return { combo: 'P11', fn: s.mode === 0 ? 'Drum pitch +1' : 'Octave +' };
  if (i >= 3 && i <= 9)
    return { combo: meta.name, fn: (s.mode === 0 && meta.seqRole) || 'Play note' };
  return null; // P0 / P1FX / P2 alone are modifiers
}

export class Labels {
  private callouts = new Map<string, Callout>();
  private lastLabeled = new Map<number, number>(); // control i → value last shown
  private infoPanel: HTMLDivElement;
  private status: HTMLDivElement;
  private logEl: HTMLDivElement;
  private log: LogEntry[] = [];
  private heldPads: number[] = []; // press order, newest last
  private padBaseHtml = new Map<number, string>(); // for appending note info
  private pendingNote: { channel: number; note: number; at: number } | null = null;

  constructor(
    private overlay: HTMLElement,
    private panel: Panel,
    store: DeviceStore,
  ) {
    this.infoPanel = document.createElement('div');
    this.infoPanel.className = 'info-panel';
    this.status = document.createElement('div');
    this.status.className = 'status-chip';
    this.logEl = document.createElement('div');
    this.logEl.className = 'action-log';
    const grip = document.createElement('div');
    grip.className = 'info-grip';
    grip.textContent = '◢';
    this.infoPanel.append(this.status, this.logEl, grip);
    overlay.appendChild(this.infoPanel);
    this.initInfoDrag();
    this.initInfoResize(grip);
    this.applyInfoScale(parseFloat(localStorage.getItem(INFO_SCALE_KEY) ?? '1'));
    this.placeInfoPanel();
    window.addEventListener('resize', () => this.placeInfoPanel());

    store.on((ev, s) => this.apply(ev, s));
    this.renderStatus(store.state);
    setInterval(() => this.expire(), 250);
  }

  private apply(ev: StateEvent, s: DeviceState) {
    switch (ev.kind) {
      case 'control': {
        const meta = CONTROLS[ev.i];
        const existing = this.callouts.get(meta.svgId);
        const visible = existing && existing.expiresAt > performance.now();
        if (!visible) {
          const prev = this.lastLabeled.get(ev.i);
          if (prev !== undefined && Math.abs(ev.v - prev) < SHOW_EPS) return;
        }
        this.lastLabeled.set(ev.i, ev.v);
        const { combo, fn } = describeControl(ev.i, s);
        let value: string;
        if (s.pads[1] && meta.fx) {
          // FX layer: show the decoded result ("Room 45%", "Off"), not the
          // knob %. The FX frame carries the device's own decode; fall back
          // to the raw knob until the first frame lands.
          value = ev.i === 0
            ? fxValueLabel('reverb', s.fx.reverb ?? ev.v)
            : fxValueLabel('delay', s.fx.delay ?? ev.v);
        } else if (meta.name === 'S35' && s.mode !== 0) {
          // s.model may be one frame stale mid-turn; the model event that
          // follows rewrites this line (see the 'model' case).
          value = `${modelName(s.model)} #${s.model}`;
        } else {
          value = `${Math.round(ev.v * 100)}%`;
        }
        const html = `<b>${combo}</b> ${fn} <span>${value}</span>`;
        this.show(meta.svgId, html);
        this.addLog(combo, html);
        break;
      }
      case 'pad': {
        const meta = PADS[ev.i];
        if (ev.v) {
          this.heldPads.push(ev.i);
          const d = describePad(ev.i, s);
          const html = d
            ? `<b>${d.combo}</b> ${d.fn}`
            : `<b>${meta.name}</b>${meta.hint ? ` ${meta.hint}` : ''}`;
          this.padBaseHtml.set(ev.i, html);
          this.show(meta.svgId, html, Infinity);
          if (d) this.addLog(d.combo, html);
          // Claim a note that arrived just before this pad-down (see
          // PENDING_NOTE_MS).
          const pn = this.pendingNote;
          if (
            pn &&
            performance.now() - pn.at < PENDING_NOTE_MS &&
            (pn.channel === 9 ? DRUM_NOTES[ev.i] === pn.note : ev.i >= 3 && ev.i <= 9)
          ) {
            this.pendingNote = null;
            this.attachNote(ev.i, pn.note, s);
          }
        } else {
          this.heldPads = this.heldPads.filter((p) => p !== ev.i);
          this.padBaseHtml.delete(ev.i);
          this.release(meta.svgId);
        }
        break;
      }
      case 'note': {
        if (!ev.on) break;
        // Attach the device's own note output to the pad that triggered it:
        // ch1 notes go to the most recent held musical pad; ch10 (drums) only
        // when the note matches the pad's GM slot (the sequencer also emits
        // ch10 notes that belong to no pad).
        const pad = [...this.heldPads].reverse().find((p) =>
          ev.channel === 9 ? DRUM_NOTES[p] === ev.note : p >= 3 && p <= 9);
        if (pad !== undefined && this.padBaseHtml.has(pad)) {
          this.attachNote(pad, ev.note, s);
        } else if (ev.channel === 0 || ev.channel === 9) {
          // The pad-down usually trails the note by up to 33 ms (STATE frame
          // rate limit) — hold the note so the pad-down can claim it.
          this.pendingNote = { channel: ev.channel, note: ev.note, at: performance.now() };
        }
        break;
      }
      case 'sw': {
        if (ev.which === 'A') {
          const names = s.mode === 0 ? SW1_POSITIONS.seq : SW1_POSITIONS.pitch;
          const html = `<b>SW1</b> <span>${names[ev.v] ?? ev.v}</span>`;
          this.show('sw1', html);
          this.addLog('SW1', html);
        } else {
          const html = `<b>SW2</b> <span>${SW2_POSITIONS[ev.v] ?? ev.v}</span>`;
          this.show('sw2', html);
          this.addLog('SW2', html);
        }
        break;
      }
      case 'model': {
        // A model-select turn already logged its own S35 line — fold the
        // resulting model into that line's value instead of adding another.
        const top = this.log[0];
        if (top && top.key.endsWith('S35')) {
          top.html = top.html.replace(
            /\s*(<span>[^<]*<\/span>)?$/,
            ` <span>${modelName(ev.v)} #${ev.v}</span>`,
          );
          this.renderLog();
        } else {
          this.addLog('model', `Model → <b>${modelName(ev.v)}</b> #${ev.v}`);
        }
        this.renderStatus(s);
        break;
      }
      case 'mode':
      case 'playing':
      case 'seqStep':
      case 'connected':
      case 'sync':
        this.renderStatus(s);
        break;
    }
  }

  /** Append the device's note output to a held pad's callout and log line. */
  private attachNote(pad: number, note: number, s: DeviceState) {
    const base = this.padBaseHtml.get(pad);
    if (!base) return;
    const noteHtml = `<span>${noteName(note)} · ${note}</span>`;
    this.show(PADS[pad].svgId, `${base} ${noteHtml}`, Infinity);
    const d = describePad(pad, s);
    if (d) this.addLog(d.combo, `<b>${d.combo}</b> ${d.fn} ${noteHtml}`);
  }

  /** Rolling list of the last few actions; repeats of the same gesture update
   * in place instead of flooding the list. */
  private addLog(key: string, html: string) {
    if (this.log[0]?.key === key) this.log[0].html = html;
    else this.log.unshift({ key, html });
    this.log = this.log.slice(0, 4);
    this.renderLog();
  }

  private renderLog() {
    this.logEl.innerHTML = this.log
      .map((e) => `<div class="action-line">${e.html}</div>`)
      .join('');
  }

  private renderStatus(s: DeviceState) {
    const parts = [
      `<b>${modelName(s.model)}</b> #${s.model}`,
      MODE_NAMES[s.mode] ?? `mode ${s.mode}`,
      s.playing ? '▶' : '⏸',
    ];
    if (s.seqStep !== null) parts.push(`step ${s.seqStep + 1}`);
    if (!s.connected) parts.push('<i>not connected</i>');
    this.status.innerHTML = parts.join(' · ');
  }

  private show(svgId: string, html: string, ttl = CALLOUT_TTL_MS) {
    let c = this.callouts.get(svgId);
    if (!c) {
      const el = document.createElement('div');
      el.className = 'callout';
      this.overlay.appendChild(el);
      c = { el, expiresAt: 0 };
      this.callouts.set(svgId, c);
    }
    c.el.innerHTML = html;
    c.el.classList.remove('fading');
    c.expiresAt = ttl === Infinity ? Infinity : performance.now() + ttl;
    this.place(svgId, c.el);
  }

  private release(svgId: string) {
    const c = this.callouts.get(svgId);
    if (c) c.expiresAt = performance.now() + 400;
  }

  /** Rendered device rect in overlay coords. The SVG element box is
   * letterboxed when width-limited (portrait phones), so its bounding rect
   * is NOT the drawn device — compute from the viewBox instead. */
  private deviceRect() {
    const vb = this.panel.svg.viewBox.baseVal;
    const a = this.svgToOverlay(vb.x, vb.y);
    const b = this.svgToOverlay(vb.x + vb.width, vb.y + vb.height);
    return {
      left: a.x, top: a.y, right: b.x, bottom: b.y,
      width: b.x - a.x, height: b.y - a.y,
    };
  }

  /** Map a point in SVG user units to overlay-local pixels. */
  private svgToOverlay(x: number, y: number): { x: number; y: number } {
    const svg = this.panel.svg;
    const vb = svg.viewBox.baseVal;
    const s = svg.getBoundingClientRect();
    const o = this.overlay.getBoundingClientRect();
    // preserveAspectRatio default: content scaled uniformly, centered
    const scale = Math.min(s.width / vb.width, s.height / vb.height);
    const ox = s.left - o.left + (s.width - vb.width * scale) / 2 - vb.x * scale;
    const oy = s.top - o.top + (s.height - vb.height * scale) / 2 - vb.y * scale;
    return { x: ox + x * scale, y: oy + y * scale };
  }

  private place(svgId: string, el: HTMLDivElement) {
    const o = this.overlay.getBoundingClientRect();

    // Knobs: anchor from panel geometry, immune to the rotating pointer line.
    const knobIdx = CONTROLS.findIndex((c) => c.svgId === svgId);
    const knob = knobIdx >= 0 ? this.panel.knobs.get(knobIdx) : undefined;
    if (knob) {
      const c = this.svgToOverlay(knob.cx, knob.cy);
      const r = this.svgToOverlay(knob.cx + KNOB_R, knob.cy).x - c.x;
      let x = c.x + r + 8;
      el.style.top = `${c.y}px`;
      el.style.transform = 'translateY(-50%)';
      el.style.left = `${x}px`;
      const w = el.offsetWidth;
      if (x + w > o.width - 4) {
        x = c.x - r - 8 - w;
        el.style.left = `${Math.max(4, x)}px`;
      }
      return;
    }

    const target = this.panel.elementFor(svgId);
    if (!target) return;
    const t = target.getBoundingClientRect();

    // Pads: centered on the shape, clamped inside the overlay — a long label
    // on an edge pad must never widen the page (mobile browsers pan the
    // visual viewport when content overflows).
    if (svgId.startsWith('pad-')) {
      el.style.transform = '';
      const x = t.left - o.left + t.width / 2 - el.offsetWidth / 2;
      const y = t.top - o.top + t.height / 2 - el.offsetHeight / 2;
      el.style.left = `${Math.min(Math.max(4, x), o.width - el.offsetWidth - 4)}px`;
      el.style.top = `${Math.min(Math.max(4, y), o.height - el.offsetHeight - 4)}px`;
      return;
    }

    // Faders / switches: to the side, flipping near the right edge.
    let x = t.right - o.left + 8;
    const y = t.top - o.top + t.height / 2;
    el.style.top = `${y}px`;
    el.style.transform = 'translateY(-50%)';
    el.style.left = `${x}px`;
    const w = el.offsetWidth;
    if (x + w > o.width - 4) {
      x = t.left - o.left - 8 - w;
      el.style.left = `${Math.max(4, x)}px`;
    }
  }

  private expire() {
    const now = performance.now();
    for (const [id, c] of this.callouts) {
      if (c.expiresAt !== Infinity && now > c.expiresAt) {
        c.el.classList.add('fading');
        if (now > c.expiresAt + 350) {
          c.el.remove();
          this.callouts.delete(id);
        }
      }
    }
  }

  // --- draggable info panel ------------------------------------------------

  private placeInfoPanel() {
    const o = this.overlay.getBoundingClientRect();
    let fx = INFO_DEFAULT.fx;
    let fy = INFO_DEFAULT.fy;
    const stored = localStorage.getItem(INFO_POS_KEY);
    if (stored) {
      try {
        ({ fx, fy } = JSON.parse(stored));
      } catch {
        /* fall back to default */
      }
    } else {
      // Default: use the free space the current shape of the screen offers.
      // On mobile sizes the device is small, so the panel goes next to it
      // (landscape) or below it (portrait); on desktop it sits inside the
      // faceplate's free zone. Anchored to the rendered device, not the
      // window, so it tracks the panel when resizing.
      const r = this.deviceRect();
      const mobile = matchMedia('(max-width: 820px), (max-height: 500px)').matches;
      if (mobile && o.height > o.width) {
        // portrait: in the letterbox space below the device
        const maxY = o.height - this.infoPanel.offsetHeight - 8;
        fx = Math.max(8, r.left) / Math.max(1, o.width);
        fy = Math.min(r.bottom + 10, Math.max(8, maxY)) / Math.max(1, o.height);
      } else if (mobile) {
        // landscape: in the letterbox space beside the device
        fx = 8 / Math.max(1, o.width);
        fy = Math.max(8, r.top) / Math.max(1, o.height);
      } else {
        fx = (r.left + r.width * 0.08) / Math.max(1, o.width);
        fy = (r.top + r.height * 0.08) / Math.max(1, o.height);
      }
    }
    const x = Math.min(Math.max(0, fx), 0.95) * o.width;
    const y = Math.min(Math.max(0, fy), 0.95) * o.height;
    this.infoPanel.style.left = `${x}px`;
    this.infoPanel.style.top = `${y}px`;
  }

  private initInfoDrag() {
    const p = this.infoPanel;
    let startX = 0, startY = 0, origX = 0, origY = 0;

    p.addEventListener('pointerdown', (e) => {
      p.setPointerCapture(e.pointerId);
      p.classList.add('dragging');
      startX = e.clientX;
      startY = e.clientY;
      origX = p.offsetLeft;
      origY = p.offsetTop;
    });
    p.addEventListener('pointermove', (e) => {
      if (!p.classList.contains('dragging')) return;
      p.style.left = `${origX + e.clientX - startX}px`;
      p.style.top = `${origY + e.clientY - startY}px`;
    });
    p.addEventListener('pointerup', (e) => {
      p.classList.remove('dragging');
      p.releasePointerCapture(e.pointerId);
      const o = this.overlay.getBoundingClientRect();
      localStorage.setItem(INFO_POS_KEY, JSON.stringify({
        fx: p.offsetLeft / Math.max(1, o.width),
        fy: p.offsetTop / Math.max(1, o.height),
      }));
    });
    // double-click: back to the default spot and size
    p.addEventListener('dblclick', () => {
      localStorage.removeItem(INFO_POS_KEY);
      localStorage.removeItem(INFO_SCALE_KEY);
      this.applyInfoScale(1);
      this.placeInfoPanel();
    });
  }

  private infoScale = 1;

  private applyInfoScale(scale: number) {
    this.infoScale = Math.min(3, Math.max(0.6, Number.isFinite(scale) ? scale : 1));
    this.infoPanel.style.fontSize = `${(14 * this.infoScale).toFixed(1)}px`;
  }

  /** Corner grip: drag toward bottom-right to grow (everything is em-based). */
  private initInfoResize(grip: HTMLDivElement) {
    let startX = 0, startY = 0, startScale = 1;

    grip.addEventListener('pointerdown', (e) => {
      e.stopPropagation(); // don't start a panel drag
      grip.setPointerCapture(e.pointerId);
      startX = e.clientX;
      startY = e.clientY;
      startScale = this.infoScale;
    });
    grip.addEventListener('pointermove', (e) => {
      if (!grip.hasPointerCapture(e.pointerId)) return;
      const d = (e.clientX - startX + e.clientY - startY) / 2;
      this.applyInfoScale(startScale * (1 + d / 120));
    });
    grip.addEventListener('pointerup', (e) => {
      grip.releasePointerCapture(e.pointerId);
      localStorage.setItem(INFO_SCALE_KEY, String(this.infoScale));
    });
  }
}
