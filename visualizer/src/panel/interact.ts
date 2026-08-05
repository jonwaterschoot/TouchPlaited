// Click/touch the drawn pads to play the device over MIDI, matching what the
// device itself would send in its current mode: Seq mode → GM drum notes on
// ch 10, pitched modes → the scale note on ch 1 (from SW1 scale + root/octave
// in the STATE telemetry). The note is captured at press so a mode change
// mid-hold still releases the right note. A local flash gives feedback since
// MIDI-in doesn't move the device's capacitive pad state.

import type { Panel } from './panel';
import type { DeviceStore } from '../core/state';
import { midiOut } from '../transport/output';
import {
  DRUM_NOTES, MIDI_DRUM_CH, MIDI_PITCH_CH, pitchedNote,
} from '../core/controls-meta';

export function enablePadInteraction(panel: Panel, store: DeviceStore) {
  // Touch screens: a long-press anywhere on the "device" must not open the
  // browser context menu (it fires mid-hold on the pads).
  panel.svg.addEventListener('contextmenu', (e) => e.preventDefault());

  for (const padIdx of Object.keys(DRUM_NOTES).map(Number)) {
    const el = panel.pads[padIdx];
    if (!el) continue;
    el.style.cursor = 'pointer';

    let held: { ch: number; note: number } | null = null;

    el.addEventListener('pointerdown', (e) => {
      el.setPointerCapture(e.pointerId);
      const s = store.state;
      held = s.mode === 0
        ? { ch: MIDI_DRUM_CH, note: DRUM_NOTES[padIdx] }
        : { ch: MIDI_PITCH_CH, note: pitchedNote(padIdx, s.scaleLatched, s.root, s.octave) };
      midiOut.send([0x90 | held.ch, held.note, 100]);
      el.classList.add('midi-flash');
    });
    const release = (e: PointerEvent) => {
      if (!held) return;
      el.releasePointerCapture?.(e.pointerId);
      midiOut.send([0x80 | held.ch, held.note, 0]);
      held = null;
      el.classList.remove('midi-flash');
    };
    el.addEventListener('pointerup', release);
    el.addEventListener('pointercancel', release);
  }
}
