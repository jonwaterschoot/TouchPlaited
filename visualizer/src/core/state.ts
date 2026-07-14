// Single observable device state. Transports write into it, views subscribe.
// Events are fine-grained so views can react to exactly what changed (label
// callouts key off individual control/pad events, not whole-state diffs).

export interface FxState {
  drive?: number;   // 0..1
  reverb?: number;  // 0..1
  delay?: number;   // 0..1
  trims?: number[]; // per-slot send trims, 0..1
}

export interface DeviceState {
  connected: boolean;
  pads: boolean[];        // P0..P11 (index 1 = P1FX)
  controls: number[];     // S30..S37 as 0..1
  swA: number;            // SW1 position 0..2 (left/mid/right)
  swB: number;            // SW2 position 0..2 (up=Seq / mid=Random / down=Pitch)
  led: number;            // user LED brightness 0..1
  model: number;          // Plaits engine 0..23
  mode: number;           // 0 Seq, 1 Random, 2 Pitch (mirrors SW2)
  playing: boolean;       // drum sequencer transport
  seqStep: number | null;
  octave: number;         // pitched-mode octave offset, −3..+3
  root: number;           // pitched-mode root semitone, 0..11
  fx: FxState;
}

export type StateEvent =
  | { kind: 'pad'; i: number; v: boolean }
  | { kind: 'control'; i: number; v: number }
  | { kind: 'sw'; which: 'A' | 'B'; v: number }
  | { kind: 'led'; v: number }
  | { kind: 'model'; v: number }
  | { kind: 'mode'; v: number }
  | { kind: 'playing'; v: boolean }
  | { kind: 'seqStep'; v: number | null }
  | { kind: 'octave'; v: number }
  | { kind: 'root'; v: number }
  | { kind: 'fx'; fx: FxState }
  | { kind: 'connected'; v: boolean }
  | { kind: 'note'; channel: number; note: number; on: boolean } // transient, not stored
  | { kind: 'sync' }; // emitted after a bulk update; views should repaint all

export type StateListener = (ev: StateEvent, s: DeviceState) => void;

function initialState(): DeviceState {
  return {
    connected: false,
    pads: new Array(12).fill(false),
    controls: new Array(8).fill(0.5),
    swA: 1,
    swB: 0,
    led: 0,
    model: 0,
    mode: 0,
    playing: false,
    seqStep: null,
    octave: 0,
    root: 0,
    fx: {},
  };
}

// Controls arrive as 7-bit values. 1.6 LSB acts as hysteresis against pot
// jitter (±1 LSB oscillation never accumulates; deliberate turns do) — a
// second line of defense behind the firmware's own reporting deadband.
const CONTROL_EPS = 1.6 / 127;

export class DeviceStore {
  readonly state: DeviceState = initialState();
  private listeners: StateListener[] = [];

  on(fn: StateListener): () => void {
    this.listeners.push(fn);
    return () => {
      this.listeners = this.listeners.filter((l) => l !== fn);
    };
  }

  private emit(ev: StateEvent) {
    for (const l of this.listeners) l(ev, this.state);
  }

  /** Ask views to repaint everything (after connect or a bulk state frame). */
  sync() {
    this.emit({ kind: 'sync' });
  }

  setPad(i: number, v: boolean) {
    if (this.state.pads[i] === v) return;
    this.state.pads[i] = v;
    this.emit({ kind: 'pad', i, v });
  }

  setControl(i: number, v: number) {
    if (Math.abs(this.state.controls[i] - v) < CONTROL_EPS) return;
    this.state.controls[i] = v;
    this.emit({ kind: 'control', i, v });
  }

  setSw(which: 'A' | 'B', v: number) {
    const key = which === 'A' ? 'swA' : 'swB';
    if (this.state[key] === v) return;
    this.state[key] = v;
    this.emit({ kind: 'sw', which, v });
  }

  setLed(v: number) {
    if (Math.abs(this.state.led - v) < CONTROL_EPS) return;
    this.state.led = v;
    this.emit({ kind: 'led', v });
  }

  setModel(v: number) {
    if (this.state.model === v) return;
    this.state.model = v;
    this.emit({ kind: 'model', v });
  }

  setMode(v: number) {
    if (this.state.mode === v) return;
    this.state.mode = v;
    this.emit({ kind: 'mode', v });
  }

  setPlaying(v: boolean) {
    if (this.state.playing === v) return;
    this.state.playing = v;
    this.emit({ kind: 'playing', v });
  }

  setSeqStep(v: number | null) {
    if (this.state.seqStep === v) return;
    this.state.seqStep = v;
    this.emit({ kind: 'seqStep', v });
  }

  setOctave(v: number) {
    if (this.state.octave === v) return;
    this.state.octave = v;
    this.emit({ kind: 'octave', v });
  }

  setRoot(v: number) {
    if (this.state.root === v) return;
    this.state.root = v;
    this.emit({ kind: 'root', v });
  }

  setFx(fx: FxState) {
    Object.assign(this.state.fx, fx);
    this.emit({ kind: 'fx', fx });
  }

  setConnected(v: boolean) {
    if (this.state.connected === v) return;
    this.state.connected = v;
    this.emit({ kind: 'connected', v });
  }

  /** Transient MIDI note traffic (device output) — emitted, never stored. */
  noteEvent(channel: number, note: number, on: boolean) {
    this.emit({ kind: 'note', channel, note, on });
  }
}
