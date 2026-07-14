// Scripted demo transport: exercises every binding without hardware. Used to
// verify the panel visually and to rehearse recordings before the firmware
// telemetry exists.

import type { Transport } from './transport';
import type { DeviceStore } from '../core/state';

const LOOP_S = 24;

export class MockTransport implements Transport {
  readonly kind = 'mock';
  private timer: number | null = null;
  private t = 0;
  private lastNotePad = -1;

  constructor(private store: DeviceStore) {}

  async connect(): Promise<void> {
    this.store.setConnected(true);
    this.store.setModel(2); // Six-Op A
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
      // Phase 1 — knob sweeps, one at a time (S31..S34)
      const i = Math.floor(t / 1.5);
      const phase = (t % 1.5) / 1.5;
      s.setMode(2);
      s.setSw('B', 2);
      s.setPlaying(false);
      s.setSeqStep(null);
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
    } else if (t < 19) {
      // Phase 4 — switches and model select
      s.setPlaying(false);
      s.setSeqStep(null);
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
    } else {
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
    }
  }
}
