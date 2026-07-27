// Scripted demo transport: exercises every binding without hardware. Used to
// verify the panel visually and to rehearse recordings before the firmware
// telemetry exists.

import type { Transport } from './transport';
import type { DeviceStore } from '../core/state';

const LOOP_S = 31;

export class MockTransport implements Transport {
  readonly kind = 'mock';
  private timer: number | null = null;
  private t = 0;
  private lastNotePad = -1;

  constructor(private store: DeviceStore) {}

  async connect(): Promise<void> {
    this.store.setConnected(true);
    this.store.setModel(2); // Six-Op A
    // A plausible random kit so the info panel's Seq view has data.
    this.store.setKit([
      { engine: 21, harmonics: 0.35, timbre: 0.4, morph: 0.5, decay: 0.6, note: 40 },
      { engine: 22, harmonics: 0.5, timbre: 0.6, morph: 0.5, decay: 0.55, note: 52 },
      { engine: 23, harmonics: 0.6, timbre: 0.7, morph: 0.5, decay: 0.5, note: 90 },
      { engine: 23, harmonics: 0.55, timbre: 0.6, morph: 0.5, decay: 0.7, note: 86 },
      { engine: 17, harmonics: 0.1, timbre: 0.8, morph: 0.4, decay: 0.4, note: 60 },
      { engine: 21, harmonics: 0.45, timbre: 0.25, morph: 0.5, decay: 0.65, note: 55 },
      { engine: 20, harmonics: 0.2, timbre: 0.4, morph: 0.5, decay: 0.6, note: 70 },
    ]);
    this.timer = window.setInterval(() => this.tick(), 33);
  }

  disconnect(): void {
    if (this.timer !== null) clearInterval(this.timer);
    this.timer = null;
    this.store.setConnected(false);
  }

  describe(): string {
    return 'Demo (scripted)';
  }

  private tick() {
    this.t = (this.t + 0.033) % LOOP_S;
    const t = this.t;
    const s = this.store;

    if (t < 6) {
      // Phase 1 — knob sweeps, one at a time (S31..S34); also resets the
      // Rec/clock state phase 6 leaves behind (setters no-op when unchanged)
      const i = Math.floor(t / 1.5);
      const phase = (t % 1.5) / 1.5;
      s.setMode(2);
      s.setSw('B', 2);
      s.setPlaying(false);
      s.setSeqStep(null);
      s.setClockSrc(0);
      s.setArpSub(0, false);
      s.setRecLayers(0, 0);
      s.setControl(1 + i, 0.5 - 0.45 * Math.cos(phase * Math.PI * 2));
      s.setLed(0);
    } else if (t < 10) {
      // Phase 2 — pads P3..P9 then P10/P11 (the logo halves), LED follows.
      // Note events fire BEFORE the pad-down, like the real device (NoteOn is
      // immediate, the pad rides the rate-limited STATE frame) — this
      // exercises the labels' pending-note buffer.
      const step = Math.floor((t - 6) / 0.44);
      const active = (t % 0.44) < 0.28 ? 3 + (step % 9) : -1;
      if (active !== this.lastNotePad) {
        const scale = [0, 2, 4, 5, 7, 9, 11];
        if (this.lastNotePad >= 3 && this.lastNotePad <= 9)
          s.noteEvent(0, 60 + scale[this.lastNotePad - 3], false);
        if (active >= 3 && active <= 9) s.noteEvent(0, 60 + scale[active - 3], true);
        this.lastNotePad = active;
      }
      for (let p = 3; p <= 11; p++) s.setPad(p, p === active);
      s.setLed((t % 0.44) < 0.28 ? 1 : 0);
    } else if (t < 16) {
      // Phase 3 — Seq mode playing: steps advance, drum pads flash, LED blinks
      for (let p = 10; p <= 11; p++) s.setPad(p, false);
      s.setMode(0);
      s.setSw('B', 0);
      s.setPlaying(true);
      const step = Math.floor((t - 10) * 4) % 8;
      s.setSeqStep(step);
      s.setLed(step % 4 === 0 ? 1 : 0.15);
      const drum = 3 + (step % 7);
      for (let p = 3; p <= 9; p++) s.setPad(p, p === drum && ((t * 4) % 1) < 0.4);
      s.setControl(1, 0.6); // tempo parked
      s.setRecSlot(t > 13.5 ? 1 : null); // last stretch: editing the snare slot
    } else if (t < 19) {
      // Phase 4 — switches and model select
      s.setPlaying(false);
      s.setSeqStep(null);
      s.setRecSlot(null);
      for (let p = 3; p <= 9; p++) s.setPad(p, false);
      const k = Math.floor(t - 16);
      s.setSw('A', k % 3);
      if (t > 17.5) {
        s.setMode(2);
        s.setSw('B', 2);
        s.setPad(0, true); // hold P0: bank-0 model select
        const v = (t - 17.5) / 1.5;
        s.setControl(5, v);
        s.setModel(Math.min(11, Math.floor(v * 12)));
      }
    } else if (t < 24) {
      // Phase 5 — faders + FX layer
      s.setPad(0, false);
      s.setControl(6, 0.5 - 0.5 * Math.cos((t - 19) / 5 * Math.PI * 2)); // volume
      s.setControl(7, 0.5 + 0.4 * Math.sin((t - 19) / 2.5 * Math.PI));   // blend
      if (t > 21 && t < 23) {
        s.setPad(1, true); // hold P1FX
        const v = 0.5 - 0.5 * Math.cos((t - 21) * Math.PI);
        s.setControl(0, v);
        if (Math.floor(t * 10) % 5 === 0) s.setFx({ drive: 0.3, reverb: v, delay: 0.25 });
      } else {
        s.setPad(1, false);
      }
      s.setLed(0);
    } else {
      // Phase 6 — Arp/Mel Rec: arm capture, stack layers (dots fill on
      // P3–P7), mute layer 2, disarm, and a CV-clock cameo at the end
      s.setPad(1, false);
      s.setMode(1);
      s.setSw('B', 1);
      s.setSw('A', 2); // lever on Rec
      const u = t - 24; // 0..7
      s.setArpSub(2, u > 0.6 && u < 5.4);
      const layers = Math.min(5, Math.max(0, Math.floor((u - 0.6) / 0.9) + 1));
      s.setRecLayers(layers, u > 3.4 && layers >= 2 ? 0b00010 : 0);
      s.setClockSrc(u > 5.6 ? 2 : 0);
    }

    // Held-combo progress bar demo (independent of the phases above — its
    // own 4s sub-cycle, rotating through all four hold kinds across the
    // full loop): fills, confirms, then a gap before the next kind. Kind 1
    // (P0+P2) gets two stages, each with its own confirm — the others are
    // single-stage. Kind 3 (layer clear) alternates success/empty outcomes
    // across loop iterations so both confirm texts get exercised.
    const cyc = t % 4;
    const kind = 1 + (Math.floor(t / 4) % 4);
    if (kind === 1) {
      const segDur = 1.5, rampDur = 1.3;
      const seg = Math.floor(cyc / segDur); // 0 or 1
      const segT = cyc - seg * segDur;
      if (cyc >= 3) s.setHold(0, 0, 0, 0);
      else if (segT < rampDur) s.setHold(1, Math.min(127, Math.round((segT / rampDur) * 127)), seg, 0);
      else s.setHold(1, 127, seg + 1, 0); // just confirmed stage seg+1
    } else {
      const rampDur = 2.8;
      if (cyc >= 3) s.setHold(0, 0, 0, 0);
      else if (cyc < rampDur) s.setHold(kind, Math.min(127, Math.round((cyc / rampDur) * 127)), 0, 0);
      else {
        const outcome = kind === 3 ? (Math.floor(t / 8) % 2 === 0 ? 1 : 2) : 0;
        s.setHold(kind, 127, 1, outcome);
      }
    }
  }
}
