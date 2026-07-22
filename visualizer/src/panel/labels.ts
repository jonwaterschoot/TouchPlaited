// Contextual label callouts ("S33 · Timbre · 64%") plus a draggable live-info
// panel (model / mode / transport / step + a collapsible model section with
// per-engine knob functions and values + action log). Callouts appear next
// to whatever just changed and fade out, so a tutorial viewer's eye is guided
// to the control being used.

import type { Panel } from './panel';
import type { DeviceStore, StateEvent, DeviceState } from '../core/state';
import {
  CONTROLS, PADS, SW1_POSITIONS, SW2_POSITIONS, MODE_NAMES, modelName,
  DRUM_NOTES, noteName, fxValueLabel, engineKnobLabel, formatKnobValue,
  ENGINE_KNOBS, pitchedNote, arpOrderName,
} from '../core/controls-meta';
import type { KnobParam } from '../core/controls-meta';

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
const INFO_SIZE_KEY = 'tp-info-size';
const MODEL_OPEN_KEY = 'tp-model-open';
const OVERLAY_MODE_KEY = 'tp-overlay-mode';

/** Label overlay modes: dynamic callouts only / static designators (S32, P3…)
 * / static full labels of the current model & mode, faceplate-style. */
type OverlayMode = 'dynamic' | 'ids' | 'full';
const OVERLAY_MODES: OverlayMode[] = ['dynamic', 'ids', 'full'];
const OVERLAY_MODE_LABEL: Record<OverlayMode, string> = {
  dynamic: 'dyn', ids: 'S#', full: 'Aa',
};

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
/** S37 Blend on a given engine: names what AUX carries; dead when the engine
 * renders AUX identical to OUT (Six-Op) so blend and width do nothing. */
function blendInfo(model: number): { fn: string; dead: boolean } {
  const aux = ENGINE_KNOBS[model]?.aux;
  if (aux === null) return { fn: 'Blend', dead: true };
  return { fn: aux ? `Blend · OUT↔${aux}` : 'Blend', dead: false };
}

/** `engine` (when set) is the model the label was resolved against — apply()
 * uses it for engine-aware value formatting and the dead-knob message. */
function describeControl(
  i: number, s: DeviceState,
): { combo: string; fn: string; dead?: boolean; engine?: number } {
  const meta = CONTROLS[i];
  // Recording (Seq): every knob edits the selected drum slot — label it for
  // that slot's engine (from the KIT frame), not the global model.
  const recEngine =
    s.mode === 0 && s.recSlot !== null ? s.kit?.[s.recSlot]?.engine : undefined;
  if (recEngine !== undefined) {
    if (s.pads[1] && meta.fx)
      return { combo: `P1 + ${meta.name}`, fn: i === 0 ? 'Slot reverb send' : 'Slot delay send' };
    if (i === 5 && s.pads[0]) return { combo: 'P0 + S35', fn: 'Slot model · bank 0' };
    if (i === 5 && s.pads[2]) return { combo: 'P2 + S35', fn: 'Slot model · bank 1' };
    if (i === 7 && s.pads[0]) return { combo: 'P0 + S37', fn: 'Slot width' };
    if (i === 0) return { combo: 'S30', fn: 'Slot drive' };
    if (i === 5) return { combo: 'S35', fn: 'Slot model select' };
    if (i === 6) return { combo: 'S36', fn: 'Slot volume' };
    if (i === 7) {
      const b = blendInfo(recEngine);
      return { combo: 'S37', fn: b.fn.replace('Blend', 'Slot blend'), dead: b.dead, engine: recEngine };
    }
    const ek = engineKnobLabel(i, recEngine);
    if (ek) return { combo: meta.name, fn: ek.fn, dead: ek.dead, engine: recEngine };
  }
  if (s.pads[1] && meta.fx)
    return { combo: `P1 + ${meta.name}`, fn: `${meta.fx} ${s.mode === 0 ? '(drums)' : '(pitched)'}` };
  if (i === 5 && s.pads[0]) return { combo: 'P0 + S35', fn: 'Model select · bank 0' };
  if (i === 5 && s.pads[2]) return { combo: 'P2 + S35', fn: 'Model select · bank 1' };
  // S37 in the pitched modes is Blend (bare) / stereo width (P0) — both
  // meaningless on engines whose AUX output equals OUT.
  if (i === 7 && s.mode !== 0) {
    const b = blendInfo(s.model);
    if (s.pads[0]) return { combo: 'P0 + S37', fn: 'Stereo width', dead: b.dead, engine: s.model };
    return { combo: 'S37', fn: b.fn, dead: b.dead, engine: s.model };
  }
  if (i === 7 && s.pads[0]) return { combo: 'P0 + S37', fn: 'Stereo width' };
  // Rec sub-state (device-latched, not the live lever): S32–S35 take the
  // Rec-only meanings and S30 is Rec's own drive; S31 stays the shared arp
  // Decay and falls through to the engine-specific branch below.
  if (s.mode === 1 && !s.sndEdit && s.arpSub === 2) {
    if (i === 0) return { combo: 'S30', fn: 'Drive (Rec)' };
    if (i === 2) return { combo: 'S32', fn: 'Speed · layer playback 1x–8x' };
    if (i === 3) return { combo: 'S33', fn: 'Shift · layers in time' };
    if (i === 4) return { combo: 'S34', fn: 'Chance · per-hit probability' };
    if (i === 5) return { combo: 'S35', fn: 'Order · as recorded ↔ shuffled' };
  }
  // Seq tempo is muted while an external clock is master (it still sets the
  // fallback tempo) — say so instead of pretending the knob drives the BPM.
  if (s.mode === 0 && s.recSlot === null && i === 1 && s.clockSrc !== 0)
    return { combo: 'S31', fn: `Tempo · ext ${s.clockSrc === 1 ? 'MIDI' : 'CV'} clock (knob muted)` };
  // Arp/Mel base layer: S31 is the arp's Decay — same engine routing as
  // everywhere, so it gets the engine-specific label too.
  if (s.mode === 1 && !s.sndEdit && i === 1) {
    const ek = engineKnobLabel(1, s.model);
    if (ek) return { combo: meta.name, fn: ek.fn, dead: ek.dead, engine: s.model };
  }
  // Basic Pitch — and the Arp/Mel sound-edit layer, which borrows the same
  // knob layout on the arp's model: timbral knobs get their engine-specific
  // function, and a knob the current engine ignores says so instead of
  // pretending to work.
  if (s.mode === 2 || (s.mode === 1 && s.sndEdit)) {
    const ek = engineKnobLabel(i, s.model);
    if (ek) return { combo: meta.name, fn: ek.fn, dead: ek.dead, engine: s.model };
    if (s.mode === 1) {
      if (i === 0) return { combo: 'S30', fn: 'Drive (sound edit)' };
      if (i === 5) return { combo: 'S35', fn: 'Order — frozen in sound edit' };
    }
  }
  const fn = s.mode === 0 ? meta.seq : s.mode === 1 ? meta.arp : meta.main;
  return { combo: meta.name, fn: fn ?? meta.main };
}

/** Name a pad press including modifier gestures; null = pure modifier, not
 * worth a log line on its own. */
function describePad(i: number, s: DeviceState): { combo: string; fn: string } | null {
  const meta = PADS[i];
  const inRec = s.mode === 1 && s.arpSub === 2;
  if (i === 11 && s.pads[2]) return { combo: 'P2 + P11', fn: 'Seq play / pause' };
  // P2+P10 is the melodic transport everywhere except in Rec, where it
  // arms/disarms capture instead (disarming keeps committed layers looping).
  if (i === 10 && s.pads[2])
    return inRec
      ? { combo: 'P2 + P10', fn: s.recArmed ? 'Rec disarm capture' : 'Rec arm capture' }
      : { combo: 'P2 + P10', fn: 'Arp/loop stop / start' };
  // P2 + P3–P7 in Rec: per-layer gesture (tap = mute, hold = clear,
  // multi-hold = clear all) — the pad neither sounds nor records.
  if (inRec && s.pads[2] && i >= 3 && i <= 7)
    return { combo: `P2 + P${i}`, fn: `Layer ${i - 2} mute · hold = clear` };
  if (inRec && s.pads[0] && i === 10)
    return { combo: 'P0 + P10', fn: 'Undo · open take, then newest layer' };
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
  private modelHead: HTMLDivElement;
  private modelBody: HTMLDivElement;
  private modelOpen: boolean;
  private staticWrap: HTMLDivElement;
  private overlayMode: OverlayMode = 'dynamic';
  private hlTarget: Element | null = null;
  private logEl: HTMLDivElement;
  private log: LogEntry[] = [];
  private heldPads: number[] = []; // press order, newest last
  private padBaseHtml = new Map<number, string>(); // for appending note info
  private pendingNote: { channel: number; note: number; at: number } | null = null;

  constructor(
    private overlay: HTMLElement,
    private panel: Panel,
    private store: DeviceStore,
  ) {
    // Static label layer sits under the info panel and callouts.
    this.staticWrap = document.createElement('div');
    this.staticWrap.className = 'static-labels';
    overlay.appendChild(this.staticWrap);

    this.infoPanel = document.createElement('div');
    this.infoPanel.className = 'info-panel';
    // Always-visible title bar: a drag handle (the hover-revealed grip/font
    // controls don't work on touch, so grabbing the panel needs a permanent
    // target), the overlay-mode + font-size controls, and a reset button
    // replacing the old double-click gesture, which mobile users can't
    // discover or reliably trigger.
    const handle = document.createElement('div');
    handle.className = 'info-handle';
    const dragIcon = document.createElement('span');
    dragIcon.className = 'info-drag-icon';
    dragIcon.textContent = '⠿';
    this.status = document.createElement('div');
    this.status.className = 'status-chip';
    // Collapsible model section — the current engine's knob functions and
    // values (Seq: the whole drum kit). Collapsed by default on small
    // screens, remembered across sessions.
    const modelSection = document.createElement('div');
    modelSection.className = 'model-section';
    this.modelHead = document.createElement('div');
    this.modelHead.className = 'model-head';
    this.modelBody = document.createElement('div');
    this.modelBody.className = 'model-body';
    modelSection.append(this.modelHead, this.modelBody);
    const storedOpen = localStorage.getItem(MODEL_OPEN_KEY);
    this.modelOpen = storedOpen !== null
      ? storedOpen === '1'
      : !matchMedia('(max-width: 820px), (max-height: 500px)').matches;
    // stopPropagation: the panel-drag pointerdown captures the pointer, which
    // would swallow this click — the collapse toggle never fired.
    this.modelHead.addEventListener('pointerdown', (e) => e.stopPropagation());
    this.modelHead.addEventListener('click', () => {
      this.modelOpen = !this.modelOpen;
      localStorage.setItem(MODEL_OPEN_KEY, this.modelOpen ? '1' : '0');
      this.renderModel(store.state);
    });
    // Hovering a row lights the matching control on the drawing.
    this.modelBody.addEventListener('mouseover', (e) => {
      const row = (e.target as HTMLElement).closest<HTMLElement>('[data-ctl],[data-pad]');
      this.setHl(row);
    });
    this.modelBody.addEventListener('mouseleave', () => this.setHl(null));
    this.logEl = document.createElement('div');
    this.logEl.className = 'action-log';
    const grip = document.createElement('div');
    grip.className = 'info-grip';
    grip.textContent = '◢';
    // Content scrolls inside a wrapper so a user-set panel height clips the
    // list, not the grip (which hangs outside the panel bounds).
    const scroll = document.createElement('div');
    scroll.className = 'info-scroll';
    scroll.append(this.status, modelSection, this.logEl);
    // Font size is its own control now — the grip resizes the box only.
    // Lives in the header, grouped with the overlay-mode and reset buttons.
    const fontCtl = document.createElement('div');
    fontCtl.className = 'info-controls';
    const mkFont = (txt: string, d: number) => {
      const b = document.createElement('button');
      b.textContent = txt;
      b.addEventListener('pointerdown', (e) => e.stopPropagation());
      b.addEventListener('click', (e) => {
        e.stopPropagation();
        this.applyInfoScale(this.infoScale + d);
        localStorage.setItem(INFO_SCALE_KEY, String(this.infoScale));
      });
      return b;
    };
    // Label-overlay mode cycle: dynamic → designators → full labels.
    const storedMode = localStorage.getItem(OVERLAY_MODE_KEY) as OverlayMode | null;
    if (storedMode && OVERLAY_MODES.includes(storedMode)) this.overlayMode = storedMode;
    const ovBtn = document.createElement('button');
    ovBtn.textContent = OVERLAY_MODE_LABEL[this.overlayMode];
    ovBtn.title = 'Label overlay: dynamic callouts / designators / full labels';
    ovBtn.addEventListener('pointerdown', (e) => e.stopPropagation());
    ovBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      const next =
        OVERLAY_MODES[(OVERLAY_MODES.indexOf(this.overlayMode) + 1) % OVERLAY_MODES.length];
      this.overlayMode = next;
      localStorage.setItem(OVERLAY_MODE_KEY, next);
      ovBtn.textContent = OVERLAY_MODE_LABEL[next];
      this.renderStatic(this.store.state);
    });
    fontCtl.append(ovBtn, mkFont('A−', -0.15), mkFont('A+', 0.15));
    const resetBtn = document.createElement('button');
    resetBtn.textContent = '⟲';
    resetBtn.title = 'Reset panel position, size and font';
    resetBtn.addEventListener('pointerdown', (e) => e.stopPropagation());
    resetBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      localStorage.removeItem(INFO_POS_KEY);
      localStorage.removeItem(INFO_SCALE_KEY);
      localStorage.removeItem(INFO_SIZE_KEY);
      this.applyInfoScale(1);
      this.applyInfoSize(null, null);
      this.placeInfoPanel();
    });
    fontCtl.append(resetBtn);
    handle.append(dragIcon, fontCtl);
    this.infoPanel.append(handle, scroll, grip);
    overlay.appendChild(this.infoPanel);
    this.initInfoDrag(handle);
    this.initInfoResize(grip);
    this.applyInfoScale(parseFloat(localStorage.getItem(INFO_SCALE_KEY) ?? '1'));
    try {
      const sz = JSON.parse(localStorage.getItem(INFO_SIZE_KEY) ?? 'null');
      if (sz) this.applyInfoSize(sz.w, sz.h);
    } catch { /* keep defaults */ }
    this.placeInfoPanel();
    window.addEventListener('resize', () => {
      this.placeInfoPanel();
      this.renderStatic(this.store.state);
    });
    // The device drawing itself can be dragged / pinch-zoomed (layout.ts
    // fires this on every transform change, incl. per-pointermove) — follow
    // it, throttled to one re-render per frame.
    let layoutRaf = 0;
    window.addEventListener('tp-panel-layout', () => {
      if (layoutRaf) return;
      layoutRaf = requestAnimationFrame(() => {
        layoutRaf = 0;
        this.renderStatic(this.store.state);
      });
    });

    store.on((ev, s) => this.apply(ev, s));
    this.renderStatus(store.state);
    this.renderModel(store.state);
    this.renderStatic(store.state);
    setInterval(() => this.expire(), 250);
  }

  private apply(ev: StateEvent, s: DeviceState) {
    // The model section reflects knob values, engine, mode, kit and rec state.
    if (ev.kind === 'control' || ev.kind === 'model' || ev.kind === 'mode' ||
        ev.kind === 'kit' || ev.kind === 'recSlot' || ev.kind === 'sndEdit' ||
        ev.kind === 'arpSub' || ev.kind === 'clockSrc' || ev.kind === 'sync')
      this.renderModel(s);
    // The static label overlays additionally track switches and pitch state
    // (pad note names) — cheap full re-render, they're a handful of divs.
    if (ev.kind === 'mode' || ev.kind === 'model' || ev.kind === 'sw' ||
        ev.kind === 'kit' || ev.kind === 'recSlot' || ev.kind === 'sndEdit' ||
        ev.kind === 'arpSub' || ev.kind === 'clockSrc' ||
        ev.kind === 'octave' || ev.kind === 'root' || ev.kind === 'sync')
      this.renderStatic(s);
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
        const { combo, fn, dead, engine } = describeControl(ev.i, s);
        let value: string;
        if (dead) {
          // Unassigned knob on this engine — name the fact, skip the %.
          const html = `<b>${combo}</b> ${fn} <i>no effect on ${modelName(engine ?? s.model)}</i>`;
          this.show(meta.svgId, html);
          this.addLog(combo, html);
          break;
        }
        if (engine !== undefined && ev.i >= 1 && ev.i <= 4) {
          // Engine-aware rendering: quantized selectors (Six-Op patch, chord
          // type) show the selected item instead of a %.
          const param: KnobParam =
            (['decay', 'harmonics', 'timbre', 'morph'] as const)[ev.i - 1];
          value = formatKnobValue(engine, param, ev.v);
          const html = `<b>${combo}</b> ${fn} <span>${value}</span>`;
          this.show(meta.svgId, html);
          this.addLog(combo, html);
          break;
        }
        if (s.pads[1] && meta.fx && !(s.mode === 0 && s.recSlot !== null)) {
          // FX layer: show the decoded result ("Room 45%", "Off"), not the
          // knob %. The FX frame carries the device's own decode; fall back
          // to the raw knob until the first frame lands. In rec mode the same
          // combo edits the slot's raw send trim — plain % there.
          value = ev.i === 0
            ? fxValueLabel('reverb', s.fx.reverb ?? ev.v)
            : fxValueLabel('delay', s.fx.delay ?? ev.v);
        } else if (meta.name === 'S35' && s.mode === 1 && s.arpSub === 2
                   && !s.sndEdit && !s.pads[0] && !s.pads[2]) {
          // Rec's Order is a left/right-of-center choice, not the arp walk.
          value = ev.v < 0.5 ? 'as recorded' : 'shuffled';
        } else if (meta.name === 'S35' && s.mode === 1 && !s.pads[0] && !s.pads[2]) {
          // Bare S35 in Arp/Mel is the note Order — name the setting, same
          // spirit as the model names on Basic Pitch's S35.
          value = arpOrderName(ev.v);
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
          const names = s.mode === 0 ? SW1_POSITIONS.seq
                      : s.mode === 1 ? SW1_POSITIONS.arp
                                     : SW1_POSITIONS.pitch;
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
      case 'recLayers': {
        // Distinguish a clear (layer count dropped) from a mute/unmute
        // toggle (count unchanged, mask bit flipped) purely from the two
        // snapshots — the firmware's LED already disambiguates these live
        // (see MANUAL.md), this just gives the same story a log line.
        if (ev.layers < ev.prevLayers) {
          const html = ev.layers === 0 && ev.prevLayers > 1
            ? `<b>Rec</b> all layers cleared`
            : `<b>Rec</b> layer cleared <span>${ev.layers}/5 left</span>`;
          this.addLog('rec-clear', html);
        } else if (ev.layers > ev.prevLayers) {
          this.addLog('rec-clear', `<b>Rec</b> layer ${ev.layers} recorded`);
        } else {
          const diff = ev.mute ^ ev.prevMute;
          for (let i = 0; i < 5; i++) {
            if (!(diff & (1 << i))) continue;
            const muted = (ev.mute & (1 << i)) !== 0;
            this.addLog(`rec-layer-${i + 1}`,
              `<b>Rec</b> layer ${i + 1} <span>${muted ? 'muted' : 'unmuted'}</span>`);
          }
        }
        this.renderStatus(s);
        break;
      }
      case 'clockSrc': {
        const name = ev.v === 1 ? 'MIDI' : ev.v === 2 ? 'CV' : null;
        this.addLog('clock', name
          ? `<b>Clock</b> external <span>${name}</span> — tempo knob muted`
          : `<b>Clock</b> back to <span>internal</span> (knob tempo)`);
        this.renderStatus(s);
        break;
      }
      case 'arpSub': {
        // Arm/disarm is its own gesture (P2+P10); a sub-state move is the
        // latched SW1 change — log whichever actually happened.
        if (ev.armed !== ev.prevArmed) {
          this.addLog('rec-arm', ev.armed
            ? `<b>Rec</b> <span>armed</span> — pads record`
            : `<b>Rec</b> <span>disarmed</span> — playback continues`);
        }
        if (ev.sub !== ev.prevSub) {
          const names = ['Arp', 'Hold', 'Rec'];
          this.addLog('arp-sub',
            `<b>Arp/Mel</b> state → <span>${names[ev.sub] ?? ev.sub}</span>`);
        }
        this.renderStatus(s);
        break;
      }
      case 'mode':
      case 'playing':
      case 'seqStep':
      case 'recSlot':
      case 'sndEdit':
      case 'connected':
      case 'sync':
        this.renderStatus(s);
        break;
    }
  }

  /** Row hover → highlight the matching control/pad on the drawing. */
  private setHl(row: HTMLElement | null) {
    let target: Element | null = null;
    if (row?.dataset.ctl !== undefined) {
      const i = parseInt(row.dataset.ctl!, 10);
      target = this.panel.knobs.get(i)?.g ?? this.panel.faders.get(i)?.rect ?? null;
    } else if (row?.dataset.pad !== undefined) {
      target = this.panel.pads[parseInt(row.dataset.pad!, 10)] ?? null;
    }
    if (target === this.hlTarget) return;
    this.hlTarget?.classList.remove('ctl-hl');
    this.hlTarget = target;
    target?.classList.add('ctl-hl');
  }

  /** The collapsible model section: the full knob map — what every pot and
   * fader does right now, with values — plus the drum kit in Seq. Rows
   * highlight their control on the drawing on hover (see setHl). */
  private renderModel(s: DeviceState) {
    const arrow = this.modelOpen ? '▾' : '▸';
    this.modelBody.style.display = this.modelOpen ? '' : 'none';
    const edit = s.mode === 1 && s.sndEdit;
    const title = s.mode === 0 ? 'Drum kit' : `${modelName(s.model)} #${s.model}`;
    const sub = s.mode === 0 && s.recSlot !== null ? ` · rec P${s.recSlot + 3}`
              : s.mode === 1 && s.arpSub === 2
                ? ` · Rec${s.recArmed ? ' ●' : ''}${edit ? ' · sound edit' : ''}`
                : edit ? ' · sound edit' : '';
    this.modelHead.innerHTML = `${arrow} <b>${title}</b>${sub}`;
    if (!this.modelOpen) return;

    // Knob map: every control's bare role right now (held-pad combos still
    // announce themselves via callouts). Values are the pot positions; in
    // Arp/Mel play the settings are latched, so values are hidden there.
    const bare: DeviceState = { ...s, pads: new Array(12).fill(false) };
    const showValues = s.mode !== 1 || edit;
    const rows: string[] = [];
    for (let i = 0; i < CONTROLS.length; i++) {
      const d = describeControl(i, bare);
      let value = '';
      if (d.dead) value = '—';
      else if (!showValues) value = '';
      else if (d.engine !== undefined && i >= 1 && i <= 4) {
        const param: KnobParam =
          (['decay', 'harmonics', 'timbre', 'morph'] as const)[i - 1];
        value = formatKnobValue(d.engine, param, s.controls[i]);
      } else if (s.mode === 0 && s.recSlot === null && i === 1) {
        // Under an external clock the knob is muted — the BPM shown would be
        // the fallback tempo, not what's playing.
        value = s.clockSrc !== 0
          ? 'ext' : `${Math.round(60 + s.controls[1] * 120)} BPM`;
      } else {
        value = `${Math.round(s.controls[i] * 100)}%`;
      }
      rows.push(`<div class="model-row${d.dead ? ' dead' : ''}" data-ctl="${i}">` +
        `<span class="k"><b>${CONTROLS[i].name}</b> ${d.fn}</span>` +
        `<span class="v">${value}</span></div>`);
    }
    if (s.mode === 1 && !edit)
      rows.push('<div class="model-row"><span class="k"><i>arp knob values follow the latched settings</i></span></div>');

    // Seq: the drum kit below the knob map.
    if (s.mode === 0) {
      if (!s.kit) {
        rows.push('<div class="model-row"><span class="k"><i>waiting for kit data…</i></span></div>');
      } else {
        const pct = (v: number) => Math.round(v * 100);
        const cells: string[] = [
          '<span></span><span></span>',
          '<span class="n head">h</span><span class="n head">t</span>',
          '<span class="n head">m</span><span class="n head">d</span>',
          '<span class="n head">note</span>',
        ];
        s.kit.forEach((slot, i) => {
          const dead = ENGINE_KNOBS[slot.engine]?.morph === null;
          const rec = s.recSlot === i ? ' rec' : '';
          const role = PADS[i + 3]?.seqRole ?? '';
          const pad = ` data-pad="${i + 3}"`;
          cells.push(
            `<span class="pad${rec}"${pad}><b>P${i + 3}</b> ${role}</span>`,
            `<span class="eng${rec}"${pad}>${modelName(slot.engine)}</span>`,
            `<span class="n${rec}"${pad}>${pct(slot.harmonics)}</span>`,
            `<span class="n${rec}"${pad}>${pct(slot.timbre)}</span>`,
            `<span class="n${rec}"${pad}>${dead ? '–' : pct(slot.morph)}</span>`,
            `<span class="n${rec}"${pad}>${pct(slot.decay)}</span>`,
            `<span class="n${rec}"${pad}>${noteName(slot.note)}</span>`,
          );
        });
        rows.push(`<div class="kit-grid">${cells.join('')}</div>`);
      }
    }
    this.modelBody.innerHTML = rows.join('');
  }

  /** Static label overlay ('ids' / 'full' modes): permanent labels anchored
   * to the panel geometry in a condensed faceplate font. 'ids' shows the
   * designators only; 'full' shows each control's current function and the
   * pads' roles/notes. Values stay on the dynamic callouts. */
  private renderStatic(s: DeviceState) {
    this.staticWrap.innerHTML = '';
    if (this.overlayMode === 'dynamic') return;
    const ids = this.overlayMode === 'ids';
    // Font tracks the rendered panel size: KNOB_R svg units → overlay px.
    const scale = this.svgToOverlay(KNOB_R, 0).x - this.svgToOverlay(0, 0).x;
    const fontPx = Math.max(8.5, Math.min(22, scale * 0.72));
    const mk = (x: number, y: number, text: string, cls = '') => {
      if (!text) return;
      const el = document.createElement('div');
      el.className = `static-label${cls}`;
      el.style.left = `${x.toFixed(1)}px`;
      el.style.top = `${y.toFixed(1)}px`;
      el.style.fontSize = `${fontPx.toFixed(1)}px`;
      el.textContent = text;
      this.staticWrap.appendChild(el);
    };
    const bare: DeviceState = { ...s, pads: new Array(12).fill(false) };
    const fullText = (i: number, withName = true) => {
      const d = describeControl(i, bare);
      return {
        text: `${withName ? `${CONTROLS[i].name}\n` : ''}${d.fn.replace(/ · /g, '\n')}`,
        cls: d.dead ? ' dead' : '',
      };
    };
    // Knobs: the designator sits inside the cap, faceplate-style; the
    // function text (full mode) hangs below it on its own.
    for (const [i, knob] of this.panel.knobs) {
      const c = this.svgToOverlay(knob.cx, knob.cy);
      mk(c.x, c.y, CONTROLS[i].name, ' in-knob');
      if (!ids) {
        const t = fullText(i, false);
        const p = this.svgToOverlay(knob.cx, knob.cy + KNOB_R + 1.5);
        mk(p.x, p.y, t.text, t.cls);
      }
    }
    for (const [i, fader] of this.panel.faders) {
      const bb = fader.rect.getBBox();
      const p = this.svgToOverlay(bb.x + bb.width / 2, fader.maxY + 4);
      if (ids) mk(p.x, p.y, CONTROLS[i].name);
      else { const t = fullText(i); mk(p.x, p.y, t.text, t.cls); }
    }
    const ov = this.overlay.getBoundingClientRect();
    this.panel.pads.forEach((el, i) => {
      const t = el.getBoundingClientRect();
      const x = t.left - ov.left + t.width / 2;
      const y = t.top - ov.top + t.height / 2;
      let text = PADS[i].name.replace(' FX', '');
      if (!ids) {
        if (i >= 3 && i <= 9) {
          text = s.mode === 0
            ? (PADS[i].seqRole ?? text)
            : noteName(pitchedNote(i, s.swA, s.root, s.octave));
        } else if (PADS[i].hint) {
          text = `${text}\n${PADS[i].hint}`;
        }
      }
      mk(x, y, text, ' pad');
    });
    for (const which of ['A', 'B'] as const) {
      const g = which === 'A' ? this.panel.sw1 : this.panel.sw2;
      const t = g.getBoundingClientRect();
      const x = t.left - ov.left + t.width / 2;
      const y = t.top - ov.top + t.height + 2;
      if (ids) { mk(x, y, which === 'A' ? 'SW1' : 'SW2'); continue; }
      const pos = which === 'A'
        ? (s.mode === 0 ? SW1_POSITIONS.seq : s.mode === 1 ? SW1_POSITIONS.arp : SW1_POSITIONS.pitch)[s.swA]
        : SW2_POSITIONS[s.swB];
      mk(x, y, `${which === 'A' ? 'SW1' : 'SW2'}\n${pos ?? ''}`);
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
    // In Arp/Mel the mode chip carries the device's latched sub-state (which
    // the SW1 lever may disagree with after a mode round-trip).
    const modeName = s.mode === 1
      ? `Arp/Mel · ${['Arp', 'Hold', 'Rec'][s.arpSub] ?? s.arpSub}`
      : MODE_NAMES[s.mode] ?? `mode ${s.mode}`;
    const parts = [
      `<b>${modelName(s.model)}</b> #${s.model}`,
      modeName,
      s.playing ? '▶' : '⏸',
    ];
    if (s.seqStep !== null) parts.push(`step ${s.seqStep + 1}`);
    if (s.clockSrc !== 0) parts.push(`<i>⏱ ${s.clockSrc === 1 ? 'MIDI' : 'CV'} clock</i>`);
    if (s.mode === 1 && s.sndEdit) parts.push('<i>sound edit</i>');
    if (s.mode === 1 && s.arpSub === 2)
      parts.push(s.recArmed ? '<i>● armed</i>' : '<i>unarmed</i>');
    if (s.mode === 1 && s.recLayers > 0)
      parts.push(`<i>${s.recLayers} layer${s.recLayers > 1 ? 's' : ''}</i>`);
    if (s.recSlot !== null) parts.push(`<i>REC P${s.recSlot + 3}</i>`);
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

  private initInfoDrag(handle: HTMLDivElement) {
    const p = this.infoPanel;
    let startX = 0, startY = 0, origX = 0, origY = 0;

    handle.addEventListener('pointerdown', (e) => {
      handle.setPointerCapture(e.pointerId);
      p.classList.add('dragging');
      startX = e.clientX;
      startY = e.clientY;
      origX = p.offsetLeft;
      origY = p.offsetTop;
    });
    handle.addEventListener('pointermove', (e) => {
      if (!p.classList.contains('dragging')) return;
      p.style.left = `${origX + e.clientX - startX}px`;
      p.style.top = `${origY + e.clientY - startY}px`;
    });
    handle.addEventListener('pointerup', (e) => {
      p.classList.remove('dragging');
      handle.releasePointerCapture(e.pointerId);
      const o = this.overlay.getBoundingClientRect();
      localStorage.setItem(INFO_POS_KEY, JSON.stringify({
        fx: p.offsetLeft / Math.max(1, o.width),
        fy: p.offsetTop / Math.max(1, o.height),
      }));
    });
  }

  private infoScale = 1;

  private applyInfoScale(scale: number) {
    this.infoScale = Math.min(3, Math.max(0.6, Number.isFinite(scale) ? scale : 1));
    this.infoPanel.style.fontSize = `${(14 * this.infoScale).toFixed(1)}px`;
  }

  /** Explicit box size in px (null = the 30ch default). Text wraps and the
   * content scrolls to fit — resizing never changes the font. */
  private applyInfoSize(w: number | null, h: number | null) {
    this.infoPanel.style.width = w ? `${Math.round(w)}px` : '';
    this.infoPanel.style.height = h ? `${Math.round(h)}px` : '';
  }

  /** Corner grip: drag resizes the panel box. Font size lives on the A−/A+
   * buttons instead, so a bigger box means more text fits, not bigger text. */
  private initInfoResize(grip: HTMLDivElement) {
    let startX = 0, startY = 0, startW = 0, startH = 0;
    let w: number | null = null, h: number | null = null;

    grip.addEventListener('pointerdown', (e) => {
      e.stopPropagation(); // don't start a panel drag
      grip.setPointerCapture(e.pointerId);
      startX = e.clientX;
      startY = e.clientY;
      startW = this.infoPanel.offsetWidth;
      startH = this.infoPanel.offsetHeight;
    });
    grip.addEventListener('pointermove', (e) => {
      if (!grip.hasPointerCapture(e.pointerId)) return;
      w = Math.min(900, Math.max(180, startW + e.clientX - startX));
      h = Math.min(1000, Math.max(90, startH + e.clientY - startY));
      this.applyInfoSize(w, h);
    });
    grip.addEventListener('pointerup', (e) => {
      grip.releasePointerCapture(e.pointerId);
      if (w !== null) localStorage.setItem(INFO_SIZE_KEY, JSON.stringify({ w, h }));
    });
  }
}
