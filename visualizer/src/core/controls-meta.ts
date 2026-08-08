// Names and panel labels for every control — the single source used by the
// visualizer overlay today and by the future manual / virtual-device modes.
// Sources: README.md panel drawing and MANUAL.md tables.

import {
  GENRE_NAMES, GENRE_PATTERN_COUNT, GENRE_PATTERN_IDX, SEQ_PATTERN_NAMES,
} from './patterns-gen';

export interface ControlMeta {
  svgId: string;
  name: string;      // silk-screen designator, e.g. "S31"
  main: string;      // Basic Pitch function (panel bottom label)
  seq?: string;      // Seq-mode function (panel top label)
  arp?: string;      // Arp/Mel function (mode 1; falls back to main)
  fx?: string;       // FX-layer function (P1FX held)
}

/** Index = S30..S37 (controls[] order). */
export const CONTROLS: ControlMeta[] = [
  { svgId: 'knob-s30', name: 'S30', main: 'Drive', seq: 'Drive', arp: 'Drive', fx: 'Reverb' },
  { svgId: 'knob-s31', name: 'S31', main: 'Decay', seq: 'Tempo', arp: 'Decay' },
  { svgId: 'knob-s32', name: 'S32', main: 'Harmonics', seq: 'Swing', arp: 'Division' },
  { svgId: 'knob-s33', name: 'S33', main: 'Timbre', seq: 'Pattern density', arp: 'Swing' },
  { svgId: 'knob-s34', name: 'S34', main: 'Morph', seq: 'Step chance', arp: 'Density' },
  { svgId: 'knob-s35', name: 'S35', main: 'Model sel', seq: 'Pattern', arp: 'Order', fx: 'Delay' },
  { svgId: 'fader-s36', name: 'S36', main: 'Volume', seq: 'Volume', arp: 'Volume' },
  { svgId: 'fader-s37', name: 'S37', main: 'Blend', seq: 'Tightness', arp: 'Blend' },
];

export interface PadMeta {
  svgId: string;
  name: string;
  seqRole?: string;  // drum slot role in Seq mode
  hint?: string;     // modifier/global function
}

/** Index = P0..P11 (pads[] order). */
export const PADS: PadMeta[] = [
  { svgId: 'pad-p0', name: 'P0', hint: 'modifier' },
  { svgId: 'pad-p1fx', name: 'P1 FX', hint: 'FX layer' },
  { svgId: 'pad-p2', name: 'P2', hint: 'modifier' },
  { svgId: 'pad-p3', name: 'P3', seqRole: 'Kick' },
  { svgId: 'pad-p4', name: 'P4', seqRole: 'Snare' },
  { svgId: 'pad-p5', name: 'P5', seqRole: 'Closed hat' },
  { svgId: 'pad-p6', name: 'P6', seqRole: 'Open hat' },
  { svgId: 'pad-p7', name: 'P7', seqRole: 'Clap' },
  { svgId: 'pad-p8', name: 'P8', seqRole: 'Tom' },
  { svgId: 'pad-p9', name: 'P9', seqRole: 'Perc' },
  { svgId: 'pad-p10', name: 'P10', hint: 'Oct − / pitch −1' },
  { svgId: 'pad-p11', name: 'P11', hint: 'Oct + / pitch +1' },
];

/** SW1 positions (left / mid / right), per mode family. In Arp/Mel SW1 is
 * the sub-state (change-latched on the device — this shows the live lever). */
export const SW1_POSITIONS = {
  seq: ['IDM', 'Techno', 'Electro'],
  pitch: ['Minor', 'Chromatic', 'Major'],
  arp: ['Hold', 'Arp', 'Rec'],
};

/** SW2 positions (up / mid / down). */
export const SW2_POSITIONS = ['Seq', 'Arp/Mel', 'Pitch'];

export const MODE_NAMES = ['Seq', 'Arp/Mel', 'Pitch'];

/** Plaits engines as used by this firmware (MANUAL.md model table). */
export const MODELS: string[] = [
  'Virtual analog VCF', // 0
  'Phase distortion',   // 1
  'Six-Op A',           // 2
  'Six-Op B',           // 3
  'Six-Op C',           // 4
  'Wave terrain',       // 5
  'String machine',     // 6
  'Chiptune (skipped)', // 7
  'Virtual analog',     // 8
  'Waveshaping',        // 9
  'FM 2-op',            // 10
  'Grain',              // 11
  'Additive',           // 12
  'Wavetable',          // 13
  'Chord',              // 14
  'Speech',             // 15
  'Swarm',              // 16
  'Noise',              // 17
  'Particle',           // 18
  'String',             // 19
  'Modal',              // 20
  'Bass drum',          // 21
  'Snare drum',         // 22
  'Hi-hat',             // 23
];

export function modelName(n: number): string {
  return MODELS[n] ?? `Model ${n}`;
}

// ── Per-engine knob functions (pitched modes) ───────────────────────────────
// What the four timbral knobs actually do on each engine — sourced from the
// Plaits manual and the engine sources. `morph: null` marks the engines where
// S34 has no effect: the firmware routes the unified Decay knob to MORPH
// there (decay_via_morph() in TouchPlaited.cpp — keep the two in sync).
// `aux` names what the engine's AUX output carries (the S37 Blend fader mixes
// OUT↔AUX); null = AUX is identical to OUT, so Blend and stereo width have no
// effect (six_op_engine.cc writes aux[i] = out[i]).

export interface EngineKnobs {
  harmonics: string;   // S32
  timbre: string;      // S33
  morph: string | null; // S34; null = unassigned on this engine
  decay: string;       // S31 — what the unified Decay knob reaches
  aux: string | null;  // S37 blend target; null = blend/width unassigned
}

/** Mirrors decay_via_morph() in TouchPlaited.cpp: engines whose intrinsic
 * decay lives on MORPH. S31 drives it there; S34 goes dead. */
export function decayViaMorph(e: number): boolean {
  return (e >= 2 && e <= 4) || (e >= 19 && e <= 23);
}

const SIX_OP: EngineKnobs = {
  harmonics: 'Patch select', timbre: 'Mod level', morph: null, decay: 'Envelope time', aux: null,
};

export const ENGINE_KNOBS: Record<number, EngineKnobs> = {
  0:  { harmonics: 'Resonance / character', timbre: 'Filter cutoff', morph: 'Waveform & sub', decay: 'Decay', aux: 'Highpass' },
  1:  { harmonics: 'Distortion freq', timbre: 'Distortion amount', morph: 'Asymmetry', decay: 'Decay', aux: 'Free-running' },
  2:  SIX_OP,
  3:  SIX_OP,
  4:  SIX_OP,
  5:  { harmonics: 'Terrain', timbre: 'Path radius', morph: 'Path offset', decay: 'Decay', aux: 'Alt path' },
  6:  { harmonics: 'Chord', timbre: 'Filter / chorus', morph: 'Waveform', decay: 'Decay', aux: 'Filtered mix' },
  8:  { harmonics: 'Detune', timbre: 'Square shape', morph: 'Saw shape', decay: 'Decay', aux: 'Synced variant' },
  9:  { harmonics: 'Waveshaper', timbre: 'Fold amount', morph: 'Asymmetry', decay: 'Decay', aux: 'Sine fold' },
  10: { harmonics: 'Freq ratio', timbre: 'Mod index', morph: 'Feedback', decay: 'Decay', aux: 'Sub oscillator' },
  11: { harmonics: 'Freq ratio', timbre: 'Formant freq', morph: 'Formant shape', decay: 'Decay', aux: 'Alt formant' },
  12: { harmonics: 'Spectrum bumps', timbre: 'Main harmonic', morph: 'Bump width', decay: 'Decay', aux: 'High harmonics' },
  13: { harmonics: 'Bank', timbre: 'Row', morph: 'Column', decay: 'Decay', aux: 'Lo-fi' },
  14: { harmonics: 'Chord type', timbre: 'Inversion', morph: 'Waveform', decay: 'Decay', aux: 'Chord subset' },
  15: { harmonics: 'Synth mode', timbre: 'Species', morph: 'Phoneme / word', decay: 'Decay', aux: 'Unfiltered' },
  16: { harmonics: 'Detune spread', timbre: 'Grain rate', morph: 'Grain duration', decay: 'Decay', aux: 'Sine grains' },
  17: { harmonics: 'Filter response', timbre: 'Clock freq', morph: 'Resonance', decay: 'Decay', aux: 'Bandpass' },
  18: { harmonics: 'Freq spread', timbre: 'Density', morph: 'Filter / diffusion', decay: 'Decay', aux: 'Raw particles' },
  19: { harmonics: 'Inharmonicity', timbre: 'Excitation brightness', morph: null, decay: 'Damping', aux: 'Raw exciter' },
  20: { harmonics: 'Material', timbre: 'Excitation brightness', morph: null, decay: 'Damping', aux: 'Raw exciter' },
  21: { harmonics: 'Attack / overdrive', timbre: 'Brightness', morph: null, decay: 'Tail', aux: 'Alt model' },
  22: { harmonics: 'Tone–noise mix', timbre: 'Brightness', morph: null, decay: 'Tail', aux: 'Alt model' },
  23: { harmonics: 'Noise colour', timbre: 'HPF cutoff', morph: null, decay: 'Tail', aux: 'Alt model' },
};

/** The 11-chord bank shared by String machine (6) and Chord (14) — order
 * matches chord_bank.cc. */
export const CHORD_NAMES = [
  'Oct', '5th', 'sus4', 'm', 'm7', 'm9', 'm11', '69', 'M9', 'M7', 'M',
];

/** Arp note orders (S35 in Arp/Mel) — mirrors arp.h SetOrder: floor(v·5),
 * clamped to the 5 entries. */
export const ARP_ORDERS = ['Played', 'Up', 'Down', 'Ping-pong', 'Random'];

export function arpOrderName(v: number): string {
  return ARP_ORDERS[Math.min(4, Math.max(0, Math.floor(v * 5)))];
}

/** Seq S35 (Pattern): name of the selected pattern + its position within the
 * current genre, e.g. "fourfloor 1/6" — mirrors Sequencer::pattern_index()'s
 * quantization (synth/sequencer.h) so the displayed slot always matches what
 * actually plays. genre is SW1 (0 IDM 1 Techno 2 Electro). */
export function patternValue(genre: number, v: number): string {
  const g = genre >= 0 && genre < GENRE_NAMES.length ? genre : 0;
  const n = GENRE_PATTERN_COUNT[g];
  const vi = Math.min(n - 1, Math.floor(v * n));
  const idx = GENRE_PATTERN_IDX[g][vi];
  return `${SEQ_PATTERN_NAMES[idx]} ${vi + 1}/${n}`;
}

/** Same as patternValue(), but from an already-resolved slot index (the
 * device's own `Sequencer::VariantSlot`, published as byte 27) rather than a
 * knob position — what the status row needs, since S35's pot is behind a
 * pickup and may not be where the playing pattern is. Mirrors
 * pattern_slot_value() in display/oled_ui.cpp. */
export function patternSlotValue(genre: number, slot: number): string {
  const g = genre >= 0 && genre < GENRE_NAMES.length ? genre : 0;
  const n = GENRE_PATTERN_COUNT[g];
  const vi = slot >= 0 && slot < n ? slot : 0;
  return `${SEQ_PATTERN_NAMES[GENRE_PATTERN_IDX[g][vi]]} ${vi + 1}/${n}`;
}

/** Seq S33 (Density): which weight layers survive at each stage — index 0 =
 * stage 1. Mirrors the same four strings in display/oled_ui.cpp's
 * kDensityWords, including their <= 11-char budget (the firmware's value row
 * keeps one font across the sweep at that length). */
export const DENSITY_WORDS = ['layer 4', 'layers 3-4', 'layers 2-4', 'layers 1-4'];

/** Mirrors Sequencer::SetDensity's quantization (synth/sequencer.h) exactly,
 * so the displayed stage always matches what's actually playing. */
export function densityValue(v: number): string {
  let d = 1 + Math.floor(v * 3 + 0.5);
  d = Math.min(4, Math.max(1, d));
  return DENSITY_WORDS[d - 1];
}

/** Seq S34 (Chance): a three-zone control, not a percentage — a raw knob %
 * read as "100% = always plays" when full right is in fact the *sparsest*
 * setting. Mirrors eval_step()'s curve in synth/sequencer.h (miss rate x1 at
 * centre, up to kChanceExtraMax = 3 at full right) and chance_value() in
 * display/oled_ui.cpp. */
export function chanceValue(v: number): string {
  const DEAD = 0.02;
  if (v <= DEAD) return 'always fire';
  if (v >= 0.5 - DEAD && v <= 0.5 + DEAD) return 'as authored';
  if (v < 0.5) return `fuller ${Math.round((0.5 - v) * 200)}%`;
  return `sparse ${(1 + (v - 0.5) * 4).toFixed(1)}x`;
}

export type KnobParam = 'harmonics' | 'timbre' | 'morph' | 'decay';

/** Engine-aware value rendering: quantized selectors show the selected item
 * ("Patch 12/32", a chord name) instead of a meaningless %. Mirrors each
 * engine's own quantizer (× 1.02, same as the firmware's blink mirror). */
export function formatKnobValue(engine: number, param: KnobParam, v: number): string {
  if (param === 'harmonics') {
    if (engine >= 2 && engine <= 4) {
      const idx = Math.min(31, Math.floor(v * 1.02 * 32));
      return `Patch ${idx + 1}/32`;
    }
    if (engine === 6 || engine === 14) {
      const idx = Math.min(CHORD_NAMES.length - 1, Math.floor(v * 1.02 * CHORD_NAMES.length));
      return CHORD_NAMES[idx];
    }
  }
  return `${Math.round(v * 100)}%`;
}

/** Engine-specific label for a timbral control in Basic Pitch, by controls[]
 * index (1=S31 … 4=S34). `dead: true` = the knob does nothing on this engine.
 * Returns null for non-timbral controls or unknown engines. */
export function engineKnobLabel(
  i: number, model: number,
): { fn: string; dead: boolean } | null {
  const ek = ENGINE_KNOBS[model];
  if (!ek) return null;
  const pair = (generic: string, specific: string) =>
    ({ fn: generic === specific ? generic : `${generic} · ${specific}`, dead: false });
  switch (i) {
    case 1: return pair('Decay', ek.decay);
    case 2: return pair('Harmonics', ek.harmonics);
    case 3: return pair('Timbre', ek.timbre);
    case 4: return ek.morph === null
      ? { fn: 'Morph', dead: true }
      : pair('Morph', ek.morph);
    default: return null;
  }
}

// ── MIDI facts (MANUAL.md "MIDI" section / TouchPlaited.cpp) ────────────────

export const MIDI_PITCH_CH = 0; // ch 1 — pitched notes, global CCs
export const MIDI_DRUM_CH = 9;  // ch 10 — GM drum notes

/** Pad index → outgoing GM drum note (P3..P9; sequencer + Seq-mode taps).
 * The anchors of the standard 4×4 grid a pad controller lands on, so its
 * bottom two rows drive the kit with no remapping — mirrors kDrumSlotGm in
 * TouchPlaited.cpp:
 *     48  49  50  51
 *     44  45 [46 OHH] 47
 *     40 [41 TOM][42 CHH][43 PERC]
 *    [36 KICK] 37 [38 SNARE][39 CLAP]  */
/** Kick lab bank — names only, in lockstep with kKickPresets in
 * `synth/kick_presets.h` (the firmware owns the parameter values; the app only
 * ever needs to turn the published 1-based index back into a name). */
export const KICK_PRESETS = [
  '808 DEEP', '808 SUB', '808 DRIVE',
  '909 PUNCH', '909 SWEEP', '909 CLICK', 'HYBRID',
  'FM 1:1', 'FM SUB 1:2', 'FM AUX SUB', 'FM GRIT', 'FM SNAP', 'FM LONG',
  'SINE PURE', 'FOLD SUB', 'VCF BOOM', 'MODAL KNOCK',
  'SUB+CLICK', 'SUB+SNAP', '909+808',
];

/** "K07 909 SWEEP" from the 1-based index the device publishes. */
export function kickPresetText(n: number): string {
  const num = `K${n < 10 ? '0' : ''}${n}`;
  const name = KICK_PRESETS[n - 1];
  return name ? `${num} ${name}` : num;
}

export const DRUM_NOTES: Record<number, number> = {
  3: 36, // Kick
  4: 38, // Snare
  5: 42, // Closed hat
  6: 46, // Open hat
  7: 39, // Clap
  8: 41, // Tom
  9: 43, // Perc
};

/** Pitched-note math, mirroring TouchPlaited.cpp compute_note(): the scale
 * degrees per SW1 panel position (left=Minor / center=Chromatic / right=Major)
 * on top of kPitchBase + root + octave. */
export const PITCH_BASE = 60;

export const SCALES: number[][] = [
  [0, 2, 3, 5, 7, 8, 10],  // Minor
  [0, 1, 2, 3, 4, 5, 6],   // Chromatic
  [0, 2, 4, 5, 7, 9, 11],  // Major
];

/** What the device would play for musical pad P3..P9 in a pitched mode.
 * (Approximation: the firmware change-latches the scale, we use the live SW1
 * position.) */
export function pitchedNote(pad: number, swA: number, root: number, octave: number): number {
  const scale = SCALES[swA] ?? SCALES[1];
  const note = PITCH_BASE + root + scale[pad - 3] + octave * 12;
  return Math.max(0, Math.min(127, note));
}

// ── FX mirror-knob decode (TouchPlaited.cpp fx_decode) ──────────────────────
// Center ≈ off (dead zone ±0.06); each half is a character, wet grows outward.

const FX_DEAD_ZONE = 0.06;
const FX_CHARS = {
  reverb: ['Room', 'Hall'],
  delay: ['Slapback', 'Dotted ⅛'],
} as const;

export type FxKind = keyof typeof FX_CHARS;

/** Human-readable result of a center-off FX value 0..1: "Off" / "Room 45%". */
export function fxValueLabel(kind: FxKind, v: number): string {
  const half = 0.5 - FX_DEAD_ZONE;
  const [a, b] = FX_CHARS[kind];
  if (v < half) return `${a} ${Math.round(((half - v) / half) * 100)}%`;
  if (v > 1 - half) return `${b} ${Math.round(((v - (1 - half)) / half) * 100)}%`;
  return 'Off';
}

export interface CcMeta {
  cc: number;
  name: string;
  shadows: string;    // which panel control it mirrors
  fxKind?: FxKind;    // center-off CC: 64 ≈ off; <64 character A, >64 character B
}

/** Everything the firmware listens to (CCs address functions, not knobs). */
export const CCS: CcMeta[] = [
  { cc: 20, name: 'Harmonics', shadows: 'S32' },
  { cc: 21, name: 'Timbre', shadows: 'S33' },
  { cc: 22, name: 'Morph', shadows: 'S34' },
  { cc: 23, name: 'Decay', shadows: 'S31' },
  { cc: 24, name: 'Drive', shadows: 'S30' },
  { cc: 25, name: 'LPG colour', shadows: 'CC only' },
  { cc: 26, name: 'Level (pitched)', shadows: 'S36' },
  { cc: 27, name: 'Seq tempo', shadows: 'S31 Seq' },
  { cc: 28, name: 'Seq shuffle', shadows: 'S32 Seq' },
  { cc: 29, name: 'Seq density', shadows: 'S33 Seq' },
  { cc: 30, name: 'Seq chance', shadows: 'S34 Seq' },
  { cc: 31, name: 'Seq tightness', shadows: 'S37 Seq' },
  { cc: 85, name: 'Reverb (pitched)', shadows: 'P1+S30', fxKind: 'reverb' },
  { cc: 86, name: 'Reverb (drums)', shadows: 'P1+S30 Seq', fxKind: 'reverb' },
  { cc: 87, name: 'Delay (pitched)', shadows: 'P1+S35', fxKind: 'delay' },
  { cc: 88, name: 'Delay (drums)', shadows: 'P1+S35 Seq', fxKind: 'delay' },
];

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

export function noteName(n: number): string {
  return `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
}

/** The root as a bare pitch class — root is octave-less by design on the
 * device (the octave lives in its own per-mode store). */
export const ROOT_NAMES = NOTE_NAMES;

/** The seven pads as pitch classes from the current root, in order:
 * "D# F F# G# A# B C#". Mirrors display/oled_ui.cpp's scale_notes(): what
 * makes it visible that shifting the root transposes the whole scale rather
 * than only retuning the first pad. */
export function scaleNotes(swA: number, root: number): string {
  return scaleNoteList(swA, root).join(' ');
}

/** The same seven pitch classes one per entry — what the OLED's pool row
 * needs, since it lays each one out in its own fixed column with a marker
 * underneath (oled-mini.ts's showPool, mirroring OledScreen::ShowPool). */
export function scaleNoteList(swA: number, root: number): string[] {
  const scale = SCALES[swA] ?? SCALES[1];
  const r = ((root % 12) + 12) % 12;
  return scale.map((d) => NOTE_NAMES[(r + d) % 12]);
}
