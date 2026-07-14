// Collapsible MIDI drawer: a fader for every CC the firmware listens to, and
// a multi-octave piano for pitched notes on ch 1. Everything goes out through
// midiOut, so it works the moment the MIDI transport is connected.

import { CCS, MIDI_PITCH_CH, noteName, fxValueLabel } from '../core/controls-meta';
import { midiOut } from '../transport/output';

const WHITE_SEMIS = [0, 2, 4, 5, 7, 9, 11];
const BLACK_SEMIS: Record<number, number> = { 0: 1, 1: 3, 3: 6, 4: 8, 5: 10 }; // white idx → semi

export class CcPanel {
  private drawer: HTMLDivElement;
  private baseOctave = 3; // piano starts at C3
  private pianoEl!: HTMLDivElement;
  private octLabel!: HTMLSpanElement;

  constructor(addToMenu: (el: HTMLElement) => void) {
    const btn = document.createElement('button');
    btn.textContent = 'MIDI panel';
    btn.className = 'menu-item';
    btn.onclick = () => this.drawer.classList.toggle('open');
    addToMenu(btn);

    this.drawer = document.createElement('div');
    this.drawer.className = 'cc-drawer';
    // The open drawer covers the toolbar toggle, so it needs its own close.
    const header = document.createElement('div');
    header.className = 'cc-header';
    const title = document.createElement('span');
    title.textContent = 'MIDI → device';
    const close = document.createElement('button');
    close.className = 'cc-close';
    close.textContent = '×';
    close.title = 'Close';
    close.onclick = () => this.drawer.classList.remove('open');
    header.append(title, close);
    this.drawer.append(header, this.buildCcList(), this.buildPianoSection());
    document.body.appendChild(this.drawer);
  }

  open() {
    this.drawer.classList.add('open');
  }

  private buildCcList(): HTMLElement {
    const wrap = document.createElement('div');
    wrap.className = 'cc-list';
    const title = document.createElement('div');
    title.className = 'cc-title';
    title.textContent = 'CC → device (ch 1)';
    wrap.appendChild(title);

    for (const meta of CCS) {
      const row = document.createElement('label');
      row.className = 'cc-row';
      const val = meta.fxKind ? 64 : 0;
      // center-off FX CCs show the decoded result ("Room 45%"), others the
      // raw 0..127 value
      const display = (v: number) =>
        meta.fxKind ? fxValueLabel(meta.fxKind, v / 127) : String(v);
      row.innerHTML =
        `<span class="cc-num">${meta.cc}</span>` +
        `<span class="cc-name">${meta.name}</span>` +
        `<span class="cc-val">${display(val)}</span>`;
      const slider = document.createElement('input');
      slider.type = 'range';
      slider.min = '0';
      slider.max = '127';
      slider.value = String(val);
      slider.oninput = () => {
        midiOut.send([0xb0 | MIDI_PITCH_CH, meta.cc, Number(slider.value)]);
        row.querySelector('.cc-val')!.textContent = display(Number(slider.value));
      };
      // center-off CCs: double-click snaps back to off
      if (meta.fxKind) {
        slider.ondblclick = () => {
          slider.value = '64';
          slider.oninput!(new Event('input') as never);
        };
        row.title = 'center = off · left/right halves are the two characters (double-click to reset)';
      } else {
        row.title = `shadows ${meta.shadows}`;
      }
      row.appendChild(slider);
      wrap.appendChild(row);
    }
    return wrap;
  }

  private buildPianoSection(): HTMLElement {
    const wrap = document.createElement('div');
    wrap.className = 'piano-section';

    const bar = document.createElement('div');
    bar.className = 'piano-bar';
    const down = document.createElement('button');
    down.textContent = '−';
    const up = document.createElement('button');
    up.textContent = '+';
    this.octLabel = document.createElement('span');
    down.onclick = () => this.shiftOctave(-1);
    up.onclick = () => this.shiftOctave(1);
    bar.append('Notes ch 1 · oct ', down, this.octLabel, up);
    wrap.appendChild(bar);

    this.pianoEl = document.createElement('div');
    this.pianoEl.className = 'piano';
    // long-press on a held key must not open the context menu (touch)
    this.pianoEl.addEventListener('contextmenu', (e) => e.preventDefault());
    wrap.appendChild(this.pianoEl);
    this.renderPiano();
    return wrap;
  }

  private shiftOctave(d: number) {
    this.baseOctave = Math.min(6, Math.max(0, this.baseOctave + d));
    this.renderPiano();
  }

  private renderPiano() {
    const octaves = 3;
    const base = (this.baseOctave + 1) * 12; // MIDI: C-1 = 0
    this.octLabel.textContent = ` C${this.baseOctave}–C${this.baseOctave + octaves} `;
    this.pianoEl.innerHTML = '';

    const whites = octaves * 7 + 1;
    for (let w = 0; w < whites; w++) {
      const oct = Math.floor(w / 7);
      const note = base + oct * 12 + WHITE_SEMIS[w % 7];
      this.pianoEl.appendChild(this.key(note, 'white', w, whites));
      const blackSemi = BLACK_SEMIS[w % 7];
      if (blackSemi !== undefined && w < whites - 1) {
        this.pianoEl.appendChild(this.key(base + oct * 12 + blackSemi, 'black', w, whites));
      }
    }
  }

  private key(note: number, color: 'white' | 'black', whiteIdx: number, whites: number): HTMLDivElement {
    const k = document.createElement('div');
    k.className = `piano-key ${color}`;
    const wPct = 100 / whites;
    if (color === 'white') {
      k.style.left = `${whiteIdx * wPct}%`;
      k.style.width = `${wPct}%`;
      if (note % 12 === 0) k.textContent = noteName(note);
    } else {
      k.style.left = `${(whiteIdx + 0.65) * wPct}%`;
      k.style.width = `${wPct * 0.7}%`;
    }
    k.title = `${noteName(note)} (${note})`;
    k.addEventListener('pointerdown', (e) => {
      k.setPointerCapture(e.pointerId);
      midiOut.send([0x90 | MIDI_PITCH_CH, note, 100]);
      k.classList.add('down');
    });
    const off = (e: PointerEvent) => {
      if (!k.classList.contains('down')) return;
      k.releasePointerCapture?.(e.pointerId);
      midiOut.send([0x80 | MIDI_PITCH_CH, note, 0]);
      k.classList.remove('down');
    };
    k.addEventListener('pointerup', off);
    k.addEventListener('pointercancel', off);
    return k;
  }
}
