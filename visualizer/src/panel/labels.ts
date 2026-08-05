// Live-action feedback: whatever just changed lights up on the drawing and
// its text ("S33 · Timbre · 64%") goes to the OLED-style faceplate screen
// (oled.ts) — the old per-control callouts overlapped each other on small
// screens. Also owns the draggable live-info panel (model / mode / transport
// / step + a collapsible model section with per-engine knob functions and
// values + action log) and the static label overlays.

import type { Panel } from './panel';
import { OledWide } from './oled-wide';
import { OledMini } from './oled-mini';
import {
  svgToOverlay as mapSvgToOverlay, labelScale, LABEL_SCALE_EVENT,
  splitLabelValue, stripTags,
} from './overlay-utils';
import type { DeviceStore, StateEvent, DeviceState } from '../core/state';
import {
  CONTROLS, PADS, SW1_POSITIONS, SW2_POSITIONS, MODE_NAMES, modelName,
  DRUM_NOTES, noteName, fxValueLabel, engineKnobLabel, formatKnobValue,
  ENGINE_KNOBS, pitchedNote, arpOrderName, patternValue, patternSlotValue,
  densityValue, chanceValue, ROOT_NAMES, scaleNotes,
} from '../core/controls-meta';
import type { KnobParam } from '../core/controls-meta';

const HL_TTL_MS = 1600;
// Matches kRecCycleMs in display/oled_ui.cpp — how long each phase of the Rec
// status row's model / hold-to-save / copy cycle stays up.
const REC_CYCLE_MS = 2600;

// The device's NoteOn arrives immediately but the matching pad-down rides the
// STATE frame (rate-limited to 33 ms), so a note usually lands before its pad:
// hold it briefly and let the pad-down claim it.
const PENDING_NOTE_MS = 300;
// Matches kListScrollMs in display/oled_ui.cpp.
const LIST_SCROLL_MS = 1400;

// A fresh highlight needs a deliberate move (~2 pot steps); real pots jitter
// ±1 step forever (S36 on the test unit). An already-lit control keeps
// tracking every step so turns read smoothly on the screen.
const SHOW_EPS = 2.2 / 127;

// Default info-panel anchor: the free zone of the faceplate, as a fraction of
// the rendered SVG (top-left area left of the Daisy board).
const INFO_DEFAULT = { fx: 0.07, fy: 0.075 };
const INFO_POS_KEY = 'tp-info-pos';
const INFO_SCALE_KEY = 'tp-info-scale';
const INFO_SIZE_KEY = 'tp-info-size';
const MODEL_OPEN_KEY = 'tp-model-open';
const OVERLAY_MODE_KEY = 'tp-overlay-mode';

/** Label overlay modes: no static labels (highlights + screen only) / static
 * designators (S32, P3…) / static full labels of the current model & mode,
 * faceplate-style. */
type OverlayMode = 'dynamic' | 'ids' | 'full';
const OVERLAY_MODES: OverlayMode[] = ['dynamic', 'ids', 'full'];
const OVERLAY_MODE_LABEL: Record<OverlayMode, string> = {
  dynamic: 'dyn', ids: 'S#', full: 'Aa',
};

// Knob circle radius incl. ring stroke, in SVG user units — used to compute
// label anchors geometrically (the rendered bbox is useless: it wobbles with
// the pointer-line rotation).
const KNOB_R = 11;

interface Highlight {
  el: Element;
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

/** Combo + function for a hold's progress bar — ported 1:1 from
 * display/oled_ui.cpp's OledUi::Service hold_kind switch, kept in the same
 * wording (the mini screen uppercases everything anyway). */
/** The state P2+P10 just landed in, named as one thing instead of as whichever
 * single flag moved — ported 1:1 from melodic_state() in
 * display/oled_ui.cpp. Transport and capture-arm are independent, so neither
 * flag on its own describes what the device is doing. */
function melodicState(mode: number, arpSub: number, running: boolean, armed: boolean): string {
  if (mode !== 1) return running ? 'Mel play' : 'Mel stopped';
  if (arpSub === 2) {
    if (!running) return 'Rec stopped';
    return armed ? 'Rec + play' : 'Play no rec';
  }
  return running ? 'Arp play' : 'Arp stopped';
}

/** What holding P0 / P1 / P2 unlocks, per mode — ported 1:1 from the combo
 * tables in display/oled_ui.cpp, including the four-row screen budget that
 * forced the ± pairs onto one row each. Rows are self-labelling, so no header
 * row is spent naming the modifier already under your finger. */
const COMBO_ROWS: Record<string, string[]> = {
  'p0.seq':     ['S37 drum width', '+P2 hold: vary kit'],
  'p0.seqrec':  ['S35 slot model b0', 'S37 slot width', '+P2 hold: vary pad'],
  'p0.pitch':   ['S35 model bank 0', 'S37 stereo width',
                 'P10/P11 root -/+', '+P2 hold: randomize'],
  'p0.arp':     ['S35 model bank 0', 'S37 stereo width',
                 '+P1 hold: sound edit', '+P2 hold: vary sound'],
  'p0.arprec':  ['S35 model bank 0', 'S37 stereo width', 'P10 undo layer',
                 '+P1 hold: sound edit', '+P2 hold: vary sound'],
  'p1.seq':     ['S30 reverb (drums)', 'S35 delay (drums)'],
  'p1.seqrec':  ['S30 slot reverb send', 'S35 slot delay send'],
  'p1.pitch':   ['S30 reverb', 'S35 delay'],
  'p1.arp':     ['S30 reverb', 'S35 delay',
                 'P10/P11 arp octaves', '+P0 hold: sound edit'],
  'p2.seq':     ['P10 mel transport', 'P11 drum play/pause', '+P0 hold: vary kit'],
  'p2.seqrec':  ['S35 slot model b1', '+P0 hold: vary pad'],
  'p2.pitch':   ['S35 model bank 1', 'P10 mel transport',
                 'P11 drum play/pause', '+P0 hold: randomize'],
  'p2.arp':     ['S35 model bank 1', 'P10 mel transport',
                 'P11 drum play/pause', '+P0 hold: vary sound'],
  'p2.arprec':  ['S35 model bank 1', 'P10 rec cycle', 'P11 drum play/pause',
                 'P3-P7 layer gestures', '+P0 hold: vary sound'],
};

function comboRows(mod: number, s: DeviceState): string[] {
  const seqRec = s.mode === 0 && s.recSlot !== null;
  const arpRec = s.mode === 1 && s.arpSub === 2;
  const ctx = seqRec ? 'seqrec'
            : s.mode === 0 ? 'seq'
            : s.mode === 1 ? (arpRec && mod !== 1 ? 'arprec' : 'arp')
            : 'pitch';
  return COMBO_ROWS[`p${mod}.${ctx}`] ?? COMBO_ROWS[`p${mod}.pitch`];
}

function holdLabel(kind: number, mode: number, recSlot: number | null): string {
  switch (kind) {
    case 1: return `P0+P2 ${mode === 0 ? 'Re-randomize' : 'Vary sound'}`;
    case 2: return 'P3-P9 Rec entry';
    case 3: return 'P2+pad Clear layer';
    case 4: return 'Rec Copy layer';
    case 5: return `Hold ${recSlot === null ? 'pad' : `P${recSlot + 3}`} to save`;
    case 6: return 'Rec Exit';
    case 7: return 'P0+P1 Sound edit';
    // Same combo as kind 1, named for its scope: in Rec it changes the one
    // pad being edited, not the whole kit.
    case 8: return `P0+P2 ${recSlot === null ? 'pad' : `P${recSlot + 3}`} sound`;
    default: return 'Hold';
  }
}

/** What flashes when hold_stage just rose — ported 1:1 from oled_ui.cpp's
 * confirm_text(). Names the change rather than the stage number it used to
 * print: "Stage 2" said a threshold was crossed but not which of the three
 * per-mode meanings it had. */
function confirmText(kind: number, mode: number, stage: number, outcome: number): string {
  switch (kind) {
    case 1:
      if (mode === 0) return stage >= 2 ? 'New kit' : 'Kit varied';
      if (stage >= 3) return 'Live knobs';
      return stage >= 2 ? 'Varied more' : 'Varied';
    case 2: return 'Recording';
    case 3: return outcome === 2 ? 'Empty' : 'Cleared';
    case 4: return 'Copied';
    case 5: return 'Saved';
    case 6: return 'Cancelled';
    // outcome: 1 entered sound edit, 2 left it — sndEdit has already flipped
    // by the time the latched flash draws, so it can't be read here.
    case 7: return outcome === 2 ? 'Arp knobs' : 'Sound edit';
    case 8: return stage >= 2 ? 'New sound' : 'Pad varied';
    default: return 'OK';
  }
}

/** What crossing the *next* threshold will do — the note row under the bar,
 * ported 1:1 from oled_ui.cpp's hold_note(). `done` is how many thresholds
 * have already fired. */
function holdNote(kind: number, mode: number, done: number, sndEdit: boolean): string {
  switch (kind) {
    // Times are kStageBlocks apart (2 s each) — these still said 1s/2s/3s
    // from before the stages were doubled.
    case 1:
      if (mode === 0) return done === 0 ? '2s vary kit' : '4s new kit';
      if (done === 0) return '2s vary sound';
      if (done === 1) return '4s vary more';
      return '6s back to live knobs';
    case 2: return '2s enter rec mode';
    case 3: return 'clear this layer';
    case 4: return 'copy slot to pad';
    case 5: return 'keep these edits';
    // Same combo both ways, so the note has to say which way this press is
    // going — the only warning before every knob changes meaning.
    case 7: return sndEdit ? 'back to arp knobs' : 'knobs edit the sound';
    case 8: return done === 0 ? '2s vary this pad' : '4s new sound, in role';
    default: return '';
  }
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
  // Root shifting transposes the whole scale (compute_note() adds the root
  // before the degree offsets), and it clamps at C/B rather than wrapping —
  // deliberately, since on a unit with no screen the dead end is the only
  // landmark there is. Name where it landed, and the scale it puts the pads in.
  if (i === 10 && s.pads[0])
    return { combo: 'P0 + P10', fn: `Root −1 → ${ROOT_NAMES[s.root] ?? '?'} · ${scaleNotes(s.swA, s.root)}` };
  if (i === 11 && s.pads[0])
    return { combo: 'P0 + P11', fn: `Root +1 → ${ROOT_NAMES[s.root] ?? '?'} · ${scaleNotes(s.swA, s.root)}` };
  if (i === 10) return { combo: 'P10', fn: s.mode === 0 ? 'Drum pitch −1' : 'Octave −' };
  if (i === 11) return { combo: 'P11', fn: s.mode === 0 ? 'Drum pitch +1' : 'Octave +' };
  if (i >= 3 && i <= 9)
    return { combo: meta.name, fn: (s.mode === 0 && meta.seqRole) || 'Play note' };
  return null; // P0 / P1FX / P2 alone are modifiers
}

// Mini screen rect in SVG user units: the faceplate zone between the knob
// columns (S30/S31 left, S34/S35 right), below the S31–S34 row — where the
// screen has always lived; oled-wide.ts moved up onto the Daisy silhouette.
const MINI_SCREEN = { x: 66.1, y: 148, w: 100.1, h: 44 };
const OLED_WIDE_VISIBLE_KEY = 'tp-oled-wide-visible';

export class Labels {
  private hls = new Map<string, Highlight>();
  private oledWide: OledWide;
  private oledMini: OledMini;
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
  private listMod = -1;     // modifier whose combo list owns the mini screen
  private listOffset = 0;   // first row shown, for lists over four rows
  private listAt = 0;       // when the current window went up
  private lastHoldKind = 0; // edge-detects a hold_stage rise into a confirm flash
  private lastHoldStage = 0;

  constructor(
    private overlay: HTMLElement,
    private panel: Panel,
    private store: DeviceStore,
    addToMenu?: (el: HTMLElement) => void,
  ) {
    // Static label layer sits under the info panel and the OLED screens.
    this.staticWrap = document.createElement('div');
    this.staticWrap.className = 'static-labels';
    overlay.appendChild(this.staticWrap);

    // The primary faceplate screen — a real 128×32 panel emulation, showing
    // whatever's currently happening. The wide screen on the Daisy is an
    // optional companion with more history/detail, toggled from the menu.
    this.oledMini = new OledMini(overlay, panel, MINI_SCREEN);
    this.oledWide = new OledWide(overlay, panel);
    if (addToMenu) {
      const btn = document.createElement('button');
      btn.className = 'menu-item';
      const label = (v: boolean) => `Expanded display: ${v ? 'On' : 'Off'}`;
      btn.textContent = label(this.oledWide.isVisible());
      btn.title = 'Show/hide the wider screen above the Daisy (history + status)';
      btn.addEventListener('click', () => {
        const v = !this.oledWide.isVisible();
        this.oledWide.setVisible(v);
        localStorage.setItem(OLED_WIDE_VISIBLE_KEY, v ? '1' : '0');
        btn.textContent = label(v);
      });
      addToMenu(btn);
    }

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
    ovBtn.title = 'Label overlay: screen only / designators / full labels';
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
    window.addEventListener(LABEL_SCALE_EVENT, () =>
      this.renderStatic(this.store.state));
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
    // Combo-list scroll. Only the two lists that exceed the screen's four
    // rows ever move, and both by exactly one row — see comboRows().
    setInterval(() => {
      if (this.listMod < 0) return;
      const rows = comboRows(this.listMod, this.store.state);
      const max = rows.length - 4;
      if (max <= 0 || performance.now() - this.listAt < LIST_SCROLL_MS) return;
      this.listOffset = (this.listOffset + 1) % (max + 1);
      this.listAt = performance.now();
      this.oledMini.showList(rows.slice(this.listOffset));
    }, 200);
    // Rec status-row cycle. On the device the row repaints whenever its own
    // text changes, which the cycle drives for free; here nothing repaints
    // without a state event, so it needs its own tick — and only while Seq
    // slot editing is actually up.
    setInterval(() => {
      if (this.store.state.mode === 0 && this.store.state.recSlot !== null)
        this.renderStatus(this.store.state);
    }, REC_CYCLE_MS / 4);
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
        const existing = this.hls.get(meta.svgId);
        const visible = existing && existing.expiresAt > performance.now();
        if (!visible) {
          const prev = this.lastLabeled.get(ev.i);
          if (prev !== undefined && Math.abs(ev.v - prev) < SHOW_EPS) return;
        }
        this.lastLabeled.set(ev.i, ev.v);
        const { combo, fn, dead, engine } = describeControl(ev.i, s);
        this.highlight(meta.svgId);
        // While a pot is armed behind a pickup its own position drives
        // nothing — the stored value is what's in effect, so that's what
        // gets formatted here, in whatever units the knob normally speaks.
        // The pot's real position is shown spatially instead, by the mini
        // screen's pickup track (screenPush below).
        const armed = ((s.pickupArmed >> ev.i) & 1) !== 0;
        const shown = armed ? s.pickupTarget[ev.i] : ev.v;
        const pickup = armed
          ? { pot: Math.round(ev.v * 127), target: Math.round(shown * 127) }
          : undefined;
        let value: string;
        if (dead) {
          // Unassigned knob on this engine — name the fact, skip the %.
          this.addLog(combo,
            `<b>${combo}</b> ${fn} <i>no effect on ${modelName(engine ?? s.model)}</i>`);
          break;
        }
        if (engine !== undefined && ev.i >= 1 && ev.i <= 4) {
          // Engine-aware rendering: quantized selectors (Six-Op patch, chord
          // type) show the selected item instead of a %.
          const param: KnobParam =
            (['decay', 'harmonics', 'timbre', 'morph'] as const)[ev.i - 1];
          value = formatKnobValue(engine, param, shown);
          this.addLog(combo, `<b>${combo}</b> ${fn} <span>${value}</span>`, pickup);
          break;
        }
        if (s.pads[1] && meta.fx && !(s.mode === 0 && s.recSlot !== null)) {
          // FX layer: show the decoded result ("Room 45%", "Off"), not the
          // knob %. The FX frame carries the device's own decode; fall back
          // to the raw knob until the first frame lands. In rec mode the same
          // combo edits the slot's raw send trim — plain % there.
          value = ev.i === 0
            ? fxValueLabel('reverb', s.fx.reverb ?? shown)
            : fxValueLabel('delay', s.fx.delay ?? shown);
        } else if (meta.name === 'S35' && s.mode === 1 && s.arpSub === 2
                   && !s.sndEdit && !s.pads[0] && !s.pads[2]) {
          // Rec's Order is a left/right-of-center choice, not the arp walk.
          value = shown < 0.5 ? 'as recorded' : 'shuffled';
        } else if (meta.name === 'S35' && s.mode === 1 && !s.pads[0] && !s.pads[2]) {
          // Bare S35 in Arp/Mel is the note Order — name the setting, same
          // spirit as the model names on Basic Pitch's S35.
          value = arpOrderName(shown);
        } else if (meta.name === 'S35' && s.mode !== 0) {
          // s.model may be one frame stale mid-turn; the model event that
          // follows rewrites this line (see the 'model' case).
          value = `${modelName(s.model)} #${s.model}`;
        } else if (meta.name === 'S35' && s.mode === 0 && s.recSlot === null
                   && !s.pads[0] && !s.pads[2]) {
          // Bare S35 in Seq is the pattern variant — name it and show its
          // position in the genre, not a meaningless %.
          value = patternValue(s.swA, shown);
        } else if (meta.name === 'S33' && s.mode === 0 && s.recSlot === null) {
          // Seq Density: name the stage instead of a raw %.
          value = densityValue(shown);
        } else if (meta.name === 'S34' && s.mode === 0 && s.recSlot === null) {
          // Seq Chance: name the zone — a raw % reads backwards here.
          value = chanceValue(shown);
        } else {
          value = `${Math.round(shown * 100)}%`;
        }
        this.addLog(combo, `<b>${combo}</b> ${fn} <span>${value}</span>`, pickup);
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
          this.highlight(meta.svgId, Infinity);
          if (d) this.addLog(d.combo, html);
          // Pure modifier (P0/P1FX/P2): worth a screen line, not a log line.
          else this.screenPush(meta.name, html);
          // A modifier going down lists what it unlocks here, and holds the
          // screen until it's released — same as OledUi::Service.
          if (ev.i <= 2) {
            this.listMod = ev.i;
            this.listOffset = 0;
            this.listAt = performance.now();
            this.oledMini.showList(comboRows(ev.i, s));
          }
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
          if (ev.i === this.listMod) {
            this.listMod = -1;
            this.oledMini.clearList();
          }
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
          this.highlight('sw1');
          // In Basic Pitch, SW1 names a scale — and a scale is a scale *from
          // somewhere*, so it carries the root and the notes it lands on.
          const suffix = s.mode === 2
            ? ` - ${ROOT_NAMES[s.root] ?? '?'} <span>${scaleNotes(s.swA, s.root)}</span>`
            : '';
          this.addLog('SW1', `<b>SW1</b> <span>${names[ev.v] ?? ev.v}</span>${suffix}`);
        } else {
          this.highlight('sw2');
          this.addLog('SW2', `<b>SW2</b> <span>${SW2_POSITIONS[ev.v] ?? ev.v}</span>`);
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
          this.screenAmend(top.key, `${modelName(ev.v)} #${ev.v}`);
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
        // Rec's P2+P10 cycle can move both flags in one press (stopped ->
        // capturing arms and starts), so capture is the headline when it
        // changed — matching the firmware's own label choice.
        const melState = melodicState(s.mode, ev.sub, ev.running, ev.armed);
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
        // Melodic transport — P2+P10's meaning outside Rec. It gates whether
        // the arp and the Rec loop sound at all, and used to pass with no
        // message anywhere.
        if (ev.running !== ev.prevRunning) {
          this.addLog('mel-transport',
            `<b>P2+P10</b> melodic transport <span>${ev.running ? 'play' : 'stop'}</span>`);
        }
        if (ev.armed !== ev.prevArmed || ev.running !== ev.prevRunning) {
          this.oledMini.show(
            ev.armed !== ev.prevArmed ? 'P2+P10 Rec capture' : 'P2+P10 Transport',
            melState);
        }
        this.renderStatus(s);
        break;
      }
      case 'hold': {
        // A hold building toward a threshold (P0+P2, rec entry, layer
        // clear, layer copy) — the mini screen's one hardware-real bar,
        // same precedence as the device LED. Deliberately mini-screen only:
        // the log/wide screen are a web-only companion (see oled-wide.ts's
        // header comment), and a bar filling every ~80ms would just spam it.
        const confirmed = ev.holdKind !== 0 && ev.stage > 0
          && (ev.holdKind !== this.lastHoldKind || ev.stage !== this.lastHoldStage);
        if (confirmed) {
          this.oledMini.confirmFlash(
            holdLabel(ev.holdKind, s.mode, s.recSlot),
            confirmText(ev.holdKind, s.mode, ev.stage, ev.outcome));
        } else if (ev.holdKind !== 0) {
          this.oledMini.showProgress(holdLabel(ev.holdKind, s.mode, s.recSlot), ev.progress,
                                     holdNote(ev.holdKind, s.mode, ev.stage, s.sndEdit));
        } else {
          this.oledMini.clearProgress();
        }
        this.lastHoldKind = ev.holdKind;
        this.lastHoldStage = ev.stage;
        break;
      }
      case 'playing':
        // P2+P11, a MIDI Start/Stop, or the seq's own first-entry auto-start.
        // Same reasoning as the melodic transport above: the combo fires on
        // P11's press edge, so without this the screen announces P11's bare
        // octave role instead of what the combo just did.
        this.oledMini.show('P2+P11 Drum seq', s.playing ? 'Play' : 'Stop');
        this.renderStatus(s);
        break;
      case 'mode':
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
      } else if (s.mode === 0 && s.recSlot === null && i === 5) {
        value = patternValue(s.swA, s.controls[5]);
      } else if (s.mode === 0 && s.recSlot === null && i === 3) {
        value = densityValue(s.controls[3]);
      } else if (s.mode === 0 && s.recSlot === null && i === 4) {
        value = chanceValue(s.controls[4]);
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
   * pads' roles/notes. Values live on the OLED screen. */
  private renderStatic(s: DeviceState) {
    this.staticWrap.innerHTML = '';
    if (this.overlayMode === 'dynamic') return;
    const ids = this.overlayMode === 'ids';
    // Font tracks the rendered panel size (KNOB_R svg units → overlay px)
    // times the user's A−/A+ setting on the device handle.
    const scale = this.svgToOverlay(KNOB_R, 0).x - this.svgToOverlay(0, 0).x;
    const fontPx = Math.max(8.5, Math.min(36, scale * 0.72 * labelScale()));
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

  /** Append the device's note output to a held pad's screen and log line. */
  private attachNote(pad: number, note: number, s: DeviceState) {
    const base = this.padBaseHtml.get(pad);
    if (!base) return;
    const noteHtml = `<span>${noteName(note)} · ${note}</span>`;
    const d = describePad(pad, s);
    if (d) this.addLog(d.combo, `<b>${d.combo}</b> ${d.fn} ${noteHtml}`);
    else this.screenPush(PADS[pad].name, `${base} ${noteHtml}`);
  }

  /** Rolling list of the last few actions; repeats of the same gesture update
   * in place instead of flooding the list. Every log line also goes to both
   * OLED screens — the log is their single feed apart from the modifier-pad
   * hints, which the screens show but the log skips. */
  private addLog(key: string, html: string, pickup?: { pot: number; target: number }) {
    this.screenPush(key, html, pickup);
    if (this.log[0]?.key === key) this.log[0].html = html;
    else this.log.unshift({ key, html });
    this.log = this.log.slice(0, 4);
    this.renderLog();
  }

  /** Feed one action-line HTML string to both screens: the wide screen keeps
   * the styled html, the mini screen gets it as plain text (its font is a
   * fixed bitmap grid, not HTML). */
  private screenPush(key: string, html: string,
                     pickup?: { pot: number; target: number }) {
    this.oledWide.push(key, html);
    const { label, value } = splitLabelValue(html);
    // An armed pot gets the pickup screen: the value is the stored one that's
    // actually in effect, and the track under it is the only thing saying the
    // pot in your hand isn't driving it yet.
    if (pickup)
      this.oledMini.showPickup(stripTags(label), stripTags(value),
                               pickup.pot, pickup.target);
    else this.oledMini.show(stripTags(label), stripTags(value));
  }

  private screenAmend(key: string, value: string) {
    this.oledWide.amendValue(key, value);
    this.oledMini.amendValue(value);
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
    this.oledWide.setStatus(parts.join(' · '));
    // Persistent indicator block — mirrors icons_for() in display/oled_ui.cpp.
    if (s.mode === 0 && s.recSlot !== null) {
      // Seq slot editing gets the circle too: no layer stack to show, but it
      // is the other state you have to leave deliberately and that otherwise
      // looks idle.
      this.oledMini.setIcons({ rec: true, layers: -1, mute: 0, open: -1 });
    } else if (s.mode === 1 && s.arpSub === 2) {
      const rec = s.recArmed && s.arpRunning;
      const layers = Math.min(5, s.recLayers);
      this.oledMini.setIcons({
        rec,
        layers,
        mute: s.recMute,
        open: rec && layers < 5 ? layers : -1,
      });
    } else {
      this.oledMini.setIcons({ rec: false, layers: -1, mute: 0, open: -1 });
    }
    // Mini screen's home row: a 1:1 mirror of the firmware's status_row()
    // (display/oled_ui.cpp) — per-mode, and deliberately free of seq_step,
    // which changes every block and so can never be redrawn fast enough to
    // be true on the real panel.
    if (s.mode === 0 && s.recSlot !== null) {
      const role = PADS[s.recSlot + 3]?.seqRole;
      // The value row cycles rather than sitting on the model: Rec is the one
      // mode you can be stuck in, and both ways out are holds on pads you are
      // not otherwise touching. Same three phases and period as the
      // firmware's status_row() (kRecCycleMs).
      const phase = Math.floor(Date.now() / REC_CYCLE_MS) % 3;
      const value = phase === 1 ? `Hold P${s.recSlot + 3} save`
                  : phase === 2 ? '+pad copies'
                  : modelName(s.kit?.[s.recSlot]?.engine ?? s.model);
      this.oledMini.setStatus(
        `Rec P${s.recSlot + 3}${role ? ` ${role}` : ''}`, value);
    } else if (s.mode === 0) {
      // Transport rides the label row so the value row can name the pattern
      // that's actually playing (s.seqPattern, the device's own slot — S35's
      // pot is behind a pickup and can be parked anywhere).
      const genre = ['IDM', 'Techno', 'Electro'][s.swA] ?? '?';
      const ext = s.clockSrc === 1 ? ' ext' : s.clockSrc === 2 ? ' cv' : '';
      this.oledMini.setStatus(
        `${s.playing ? 'Seq' : 'Seq stop'} ${genre}${ext}`,
        patternSlotValue(s.swA, s.seqPattern));
    } else if (s.mode === 1) {
      const sub = ['Arp', 'Hold', 'Rec'][s.arpSub] ?? 'Arp';
      // In Rec, which state you're in matters more than the model — armed and
      // running are independent (a punched-out loop keeps playing). In
      // Arp/Hold the model is more useful, except while stopped, the one
      // state where the mode looks broken rather than quiet.
      const value = s.arpSub === 2 || !s.arpRunning
        ? melodicState(s.mode, s.arpSub, s.arpRunning, s.recArmed)
        : modelName(s.model);
      this.oledMini.setStatus(`Arp/Mel ${sub}${s.sndEdit ? ' edit' : ''}`, value);
    } else {
      // Scale and root belong together — they name the key the pads are in,
      // and "Minor" alone never said which minor.
      const scale = ['Minor', 'Chromatic', 'Major'][s.swA] ?? '?';
      this.oledMini.setStatus(
        `Pitch ${scale} ${ROOT_NAMES[s.root] ?? '?'}`, modelName(s.model));
    }
  }

  /** Light up a control on the drawing for a while (Infinity = held pad).
   * The matching text lives on the OLED screen, not next to the control. */
  private highlight(svgId: string, ttl = HL_TTL_MS) {
    let h = this.hls.get(svgId);
    if (!h) {
      const el = this.panel.elementFor(svgId);
      if (!el) return;
      h = { el, expiresAt: 0 };
      this.hls.set(svgId, h);
    }
    h.el.classList.add('live-hl');
    h.expiresAt = ttl === Infinity ? Infinity : performance.now() + ttl;
  }

  private release(svgId: string) {
    const h = this.hls.get(svgId);
    if (h) h.expiresAt = performance.now() + 400;
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
    return mapSvgToOverlay(this.panel.svg, this.overlay, x, y);
  }

  private expire() {
    const now = performance.now();
    for (const [id, h] of this.hls) {
      if (h.expiresAt !== Infinity && now > h.expiresAt) {
        h.el.classList.remove('live-hl');
        this.hls.delete(id);
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
