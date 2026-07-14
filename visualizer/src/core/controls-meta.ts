// Names and panel labels for every control — the single source used by the
// visualizer overlay today and by the future manual / virtual-device modes.
// Sources: README.md panel drawing and MANUAL.md tables.

export interface ControlMeta {
  svgId: string;
  name: string;      // silk-screen designator, e.g. "S31"
  main: string;      // pitched-mode function (panel bottom label)
  seq?: string;      // Seq-mode function (panel top label)
  fx?: string;       // FX-layer function (P1FX held)
}

/** Index = S30..S37 (controls[] order). */
export const CONTROLS: ControlMeta[] = [
  { svgId: 'knob-s30', name: 'S30', main: 'Drive', seq: 'Drive', fx: 'Reverb' },
  { svgId: 'knob-s31', name: 'S31', main: 'Decay', seq: 'Tempo' },
  { svgId: 'knob-s32', name: 'S32', main: 'Harmonics', seq: 'Swing' },
  { svgId: 'knob-s33', name: 'S33', main: 'Timbre', seq: 'Density' },
  { svgId: 'knob-s34', name: 'S34', main: 'Morph', seq: 'Punch' },
  { svgId: 'knob-s35', name: 'S35', main: 'Model sel', seq: 'Pattern', fx: 'Delay' },
  { svgId: 'fader-s36', name: 'S36', main: 'Volume', seq: 'Volume' },
  { svgId: 'fader-s37', name: 'S37', main: 'Blend', seq: 'Tightness' },
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

/** SW1 positions (left / mid / right), per mode family. */
export const SW1_POSITIONS = {
  seq: ['IDM', 'Techno', 'Electro'],
  pitch: ['Minor', 'Chromatic', 'Major'],
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

// ── MIDI facts (MANUAL.md "MIDI" section / TouchPlaited.cpp) ────────────────

export const MIDI_PITCH_CH = 0; // ch 1 — pitched notes, global CCs
export const MIDI_DRUM_CH = 9;  // ch 10 — GM drum notes

/** Pad index → outgoing GM drum note (P3..P9; sequencer + Seq-mode taps). */
export const DRUM_NOTES: Record<number, number> = {
  3: 36, // Kick
  4: 38, // Snare
  5: 42, // Closed hat
  6: 46, // Open hat
  7: 39, // Clap
  8: 45, // Tom
  9: 37, // Perc
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
  { cc: 30, name: 'Kick punch', shadows: 'S34 Seq' },
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
