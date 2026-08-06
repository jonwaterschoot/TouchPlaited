// Single observable device state. Transports write into it, views subscribe.
// Events are fine-grained so views can react to exactly what changed (label
// callouts key off individual control/pad events, not whole-state diffs).

export interface FxState {
  drive?: number;   // 0..1
  reverb?: number;  // 0..1
  delay?: number;   // 0..1
  trims?: number[]; // per-slot send trims, 0..1
}

/** One drum slot from the KIT frame; params 0..1, note = MIDI note. */
export interface KitSlot {
  engine: number;
  harmonics: number;
  timbre: number;
  morph: number;
  decay: number;
  note: number;
}

export interface DeviceState {
  connected: boolean;
  pads: boolean[];        // P0..P11 (index 1 = P1FX)
  controls: number[];     // S30..S37 as 0..1
  swA: number;            // SW1 position 0..2 (left/mid/right)
  swB: number;            // SW2 position 0..2 (up=Seq / mid=Arp/Mel / down=Pitch)
  led: number;            // user LED brightness 0..1
  model: number;          // Plaits engine 0..23
  mode: number;           // 0 Seq, 1 Arp/Mel, 2 Pitch (mirrors SW2)
  playing: boolean;       // drum sequencer transport
  sndEdit: boolean;       // Arp/Mel sound-edit knob layer active (P0+P1)
  seqStep: number | null;
  octave: number;         // pitched-mode octave offset, −3..+3
  root: number;           // pitched-mode root semitone, 0..11
  fx: FxState;
  recSlot: number | null; // drum slot being edited in Recording, null = idle
  kit: KitSlot[] | null;  // Seq drum kit (P3..P9), null until a KIT frame lands
  recLayers: number;      // NoteRec committed layer count, 0..5 (Arp/Mel Rec)
  recMute: number;        // bitmask, bit i = NoteRec layer i muted
  clockSrc: number;       // master clock: 0 internal, 1 MIDI, 2 CV
  arpSub: number;         // Arp/Mel sub-state: 0 Arp, 1 Hold, 2 Rec — the
                          // device's change-latched state, NOT the SW1 lever
  recArmed: boolean;      // Rec capture armed (P2+P10)
  arpRunning: boolean;    // melodic transport (P2+P10 outside Rec) — gates
                          // the arp and the Rec loop together; independent
                          // of recArmed, a punched-out Rec still loops
  arpPool: number;        // bitmask, bit i = pad P(3+i) is in the arp's note
                          // pool. NOT the same as pads[] in Hold, which is
                          // the point of publishing it: notes latch there, so
                          // the pool routinely holds notes nothing is
                          // touching (and a re-touch removes one)
  arpRange: number;       // arp octave range 0..3 — extra octaves the arp
                          // climbs ABOVE its base octave, so the two compose:
                          // base +1 with range 2 spans +1..+3
  seqPattern: number;     // Seq pattern slot playing within the genre, 0-based
  holdKind: number;       // 0 none, 1 P0+P2, 2 rec entry, 3 layer clear,
                          // 4 layer copy, 5 rec save — a hold building
                          // toward a threshold; same precedence as the LED
  holdProgress: number;   // 0..1, fraction of the way to holdKind's threshold
  holdStage: number;      // confirms fired so far for the current hold — 0
                          // while building; edge-detect a rise to catch a
                          // confirm (1/2/3 for holdKind 1's stages, 0->1 for
                          // the single-stage holds)
  holdOutcome: number;    // holdKind 3 only, at the instant holdStage hits 1:
                          // 0 n/a, 1 success, 2 empty (nothing to clear)
  pickupArmed: number;    // bitmask, bit i = S3(0+i) is behind a pickup: the
                          // pot drives nothing until it reaches pickupTarget[i].
                          // Every playmode has its own pickup layer, so every
                          // hand-off between them puts knobs in this state
  pickupTarget: number[]; // 0..1 per S30..S37; meaningless where the bit is 0
  genreLatched: number;   // SW1 only takes effect in the mode you move it in,
  scaleLatched: number;   // so these are what is actually LOADED, while swA is
                          // merely where the lever sits. They diverge as soon
                          // as you flick SW1 in another mode, and everything
                          // that names a genre or scale must read these — swA
                          // is only for "has the lever moved" and for marking
                          // the divergence. Both panel order, like swA.
}

export type StateEvent =
  | { kind: 'pad'; i: number; v: boolean }
  | { kind: 'control'; i: number; v: number }
  | { kind: 'sw'; which: 'A' | 'B'; v: number }
  | { kind: 'led'; v: number }
  | { kind: 'model'; v: number }
  | { kind: 'mode'; v: number }
  | { kind: 'playing'; v: boolean }
  | { kind: 'sndEdit'; v: boolean }
  | { kind: 'seqStep'; v: number | null }
  | { kind: 'octave'; v: number }
  | { kind: 'root'; v: number }
  | { kind: 'fx'; fx: FxState }
  | { kind: 'recSlot'; v: number | null }
  | { kind: 'kit' }
  | { kind: 'recLayers'; layers: number; mute: number; prevLayers: number; prevMute: number }
  | { kind: 'clockSrc'; v: number }
  | { kind: 'arpSub'; sub: number; armed: boolean; running: boolean;
      prevSub: number; prevArmed: boolean; prevRunning: boolean }
  | { kind: 'arpRange'; v: number }
  | { kind: 'arpPool'; v: number; prev: number }
  | { kind: 'seqPattern'; v: number }
  | { kind: 'sw1Latch'; genre: number; scale: number }
  | { kind: 'hold'; holdKind: number; progress: number; stage: number; outcome: number }
  | { kind: 'pickup'; armed: number; targets: number[] }
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
    sndEdit: false,
    seqStep: null,
    octave: 0,
    root: 0,
    fx: {},
    recSlot: null,
    kit: null,
    recLayers: 0,
    recMute: 0,
    clockSrc: 0,
    arpSub: 0,
    arpPool: 0,
    arpRange: 0,
    recArmed: false,
    arpRunning: true,   // device default: the plain arp sounds from boot
    seqPattern: 0,
    holdKind: 0,
    holdProgress: 0,
    holdStage: 0,
    holdOutcome: 0,
    pickupArmed: 0,
    pickupTarget: new Array(8).fill(0),
    genreLatched: 1,   // Techno, matching seq_genre_lk's boot default
    scaleLatched: 1,   // scale follows the lever at boot; 1 = swA's default
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

  setSndEdit(v: boolean) {
    if (this.state.sndEdit === v) return;
    this.state.sndEdit = v;
    this.emit({ kind: 'sndEdit', v });
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

  setRecSlot(v: number | null) {
    if (this.state.recSlot === v) return;
    this.state.recSlot = v;
    this.emit({ kind: 'recSlot', v });
  }

  /** NoteRec layer count + mute mask (Arp/Mel Rec) — carries both the new
   * and previous values so listeners can tell a clear (count dropped) from
   * a mute toggle (count same, mask bit flipped) without re-deriving state. */
  setRecLayers(layers: number, mute: number) {
    const prevLayers = this.state.recLayers;
    const prevMute = this.state.recMute;
    if (layers === prevLayers && mute === prevMute) return;
    this.state.recLayers = layers;
    this.state.recMute = mute;
    this.emit({ kind: 'recLayers', layers, mute, prevLayers, prevMute });
  }

  setClockSrc(v: number) {
    if (this.state.clockSrc === v) return;
    this.state.clockSrc = v;
    this.emit({ kind: 'clockSrc', v });
  }

  /** Arp/Mel sub-state (change-latched on the device) + Rec armed flag +
   * melodic transport — carries the previous values so listeners can log the
   * specific transition (arm/disarm vs. play/stop vs. a sub-state move)
   * without re-deriving it. */
  setArpSub(sub: number, armed: boolean, running: boolean) {
    const prevSub = this.state.arpSub;
    const prevArmed = this.state.recArmed;
    const prevRunning = this.state.arpRunning;
    if (sub === prevSub && armed === prevArmed && running === prevRunning) return;
    this.state.arpSub = sub;
    this.state.recArmed = armed;
    this.state.arpRunning = running;
    this.emit({ kind: 'arpSub', sub, armed, running, prevSub, prevArmed, prevRunning });
  }

  /** Arp octave range. Its own setter rather than a field on setArpSub:
   * that event carries prev values so listeners can name a transition, and a
   * range tweak is not a sub-state move. */
  setArpRange(v: number) {
    if (this.state.arpRange === v) return;
    this.state.arpRange = v;
    this.emit({ kind: 'arpRange', v });
  }

  /** The arp's note pool. Carries the previous mask so a listener can name
   * which pad went in or out — in Hold that is a toggle, and the pad event
   * that caused it says nothing about which way it went. */
  setArpPool(v: number) {
    const prev = this.state.arpPool;
    if (v === prev) return;
    this.state.arpPool = v;
    this.emit({ kind: 'arpPool', v, prev });
  }

  /** Which pattern slot the drum sequencer is actually playing. */
  setSeqPattern(v: number) {
    if (this.state.seqPattern === v) return;
    this.state.seqPattern = v;
    this.emit({ kind: 'seqPattern', v });
  }

  /** What SW1's two roles actually hold, as opposed to where the lever sits.
   * Emitted together: a single frame can move both (a restore, a reconnect). */
  setSw1Latch(genre: number, scale: number) {
    if (this.state.genreLatched === genre && this.state.scaleLatched === scale) return;
    this.state.genreLatched = genre;
    this.state.scaleLatched = scale;
    this.emit({ kind: 'sw1Latch', genre, scale });
  }

  /** Which pots are behind a pickup, and what each one has to reach. Only
   * the armed slots are compared: an unarmed slot's target is stale by
   * definition, and letting it churn would emit on every frame. */
  setPickup(armed: number, targets: number[]) {
    let changed = this.state.pickupArmed !== armed;
    for (let i = 0; i < 8 && !changed; i++)
      if ((armed >> i) & 1) changed = this.state.pickupTarget[i] !== targets[i];
    if (!changed) return;
    this.state.pickupArmed = armed;
    this.state.pickupTarget = targets;
    this.emit({ kind: 'pickup', armed, targets });
  }

  /** A hold building toward a threshold (P0+P2, rec entry, layer clear,
   * layer copy) — progress arrives as a raw 7-bit value, normalized to 0..1
   * like every other control. Fires on every STATE frame while a hold is
   * live so the bar visibly fills, not just on holdKind/stage changing —
   * listeners edge-detect a stage rise themselves to catch the confirm. */
  setHold(holdKind: number, progress7: number, stage: number, outcome: number) {
    const progress = progress7 / 127;
    if (holdKind === this.state.holdKind && progress === this.state.holdProgress
        && stage === this.state.holdStage && outcome === this.state.holdOutcome) return;
    this.state.holdKind = holdKind;
    this.state.holdProgress = progress;
    this.state.holdStage = stage;
    this.state.holdOutcome = outcome;
    this.emit({ kind: 'hold', holdKind, progress, stage, outcome });
  }

  /** Replace the kit snapshot; the 2 s KIT heartbeat re-sends unchanged kits,
   * so diff here to keep views from repainting on every heartbeat. */
  setKit(slots: KitSlot[]) {
    const prev = this.state.kit;
    const same =
      prev !== null &&
      prev.length === slots.length &&
      prev.every((p, i) => {
        const n = slots[i];
        return (
          p.engine === n.engine && p.harmonics === n.harmonics &&
          p.timbre === n.timbre && p.morph === n.morph &&
          p.decay === n.decay && p.note === n.note
        );
      });
    if (same) return;
    this.state.kit = slots;
    this.emit({ kind: 'kit' });
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
