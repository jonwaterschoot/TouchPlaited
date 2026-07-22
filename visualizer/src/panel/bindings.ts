// state → panel visuals: knob rotation, fader travel, pad glow, switch
// position, LED brightness. Pure view code — never writes to the store.

import type { Panel } from './panel';
import type { DeviceStore, StateEvent } from '../core/state';

// Pointer at rest points to 7:30 (value 0); full travel is 300°.
const KNOB_SWEEP_DEG = 300;

export class PanelBindings {
  constructor(private panel: Panel, private store: DeviceStore) {
    store.on((ev) => this.apply(ev));
    this.syncAll();
  }

  syncAll() {
    const s = this.store.state;
    for (let i = 0; i < s.controls.length; i++) this.control(i, s.controls[i]);
    for (let i = 0; i < s.pads.length; i++) this.pad(i, s.pads[i]);
    this.switch('A', s.swA);
    this.switch('B', s.swB);
    this.led(s.led);
    this.layerDots();
  }

  private apply(ev: StateEvent) {
    switch (ev.kind) {
      case 'control': this.control(ev.i, ev.v); break;
      case 'pad': this.pad(ev.i, ev.v); break;
      case 'sw': this.switch(ev.which, ev.v); break;
      case 'led': this.led(ev.v); break;
      case 'recLayers':
      case 'arpSub':
      case 'mode': this.layerDots(); break;
      case 'sync': this.syncAll(); break;
    }
  }

  /** NoteRec layer badges on P3–P7: filled = committed, dimmed = muted,
   * outline = empty; the next free slot pulses while capture is armed.
   * Shown only in Arp/Mel with something to say (in Rec, or with committed
   * layers still looping from another sub-state). */
  private layerDots() {
    const s = this.store.state;
    const show = s.mode === 1 && (s.arpSub === 2 || s.recLayers > 0);
    this.panel.recDots.forEach((dot, i) => {
      const committed = i < s.recLayers;
      const muted = committed && ((s.recMute >> i) & 1) !== 0;
      dot.classList.toggle('hidden', !show);
      dot.classList.toggle('filled', committed && !muted);
      dot.classList.toggle('muted', muted);
      dot.classList.toggle('armed',
        show && s.arpSub === 2 && s.recArmed && i === s.recLayers);
    });
  }

  private control(i: number, v: number) {
    const knob = this.panel.knobs.get(i);
    if (knob) {
      const deg = KNOB_SWEEP_DEG * v;
      knob.g.setAttribute('transform', `rotate(${deg.toFixed(1)} ${knob.cx} ${knob.cy})`);
      return;
    }
    const fader = this.panel.faders.get(i);
    if (fader) {
      const y = fader.maxY - v * (fader.maxY - fader.minY);
      fader.rect.setAttribute('y', y.toFixed(2));
    }
  }

  private pad(i: number, v: boolean) {
    this.panel.pads[i].classList.toggle('active', v);
  }

  private switch(which: 'A' | 'B', v: number) {
    const g = which === 'A' ? this.panel.sw1 : this.panel.sw2;
    g.dataset.pos = String(v);
  }

  private led(v: number) {
    const rect = this.panel.ledUser;
    rect.style.opacity = (0.18 + 0.82 * v).toFixed(3);
    rect.style.filter = v > 0.02 ? `drop-shadow(0 0 ${(v * 5).toFixed(1)}px #ff4536)` : '';
  }
}
