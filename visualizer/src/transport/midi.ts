// Web MIDI transport: listens for telemetry SysEx from the device and can send
// MIDI *to* it (the in-app way to demo MIDI features — also sidesteps Windows'
// single-client MIDI limitation, since the browser holds the only handle).

import type { Transport } from './transport';
import type { DeviceStore } from '../core/state';
import { applySysex, encodeRequest } from '../core/protocol';
import { midiOut } from './output';

const DEVICE_NAME = /daisy|seed|touch|plait/i;

export class MidiTransport implements Transport {
  readonly kind = 'midi';
  private access: MIDIAccess | null = null;
  private input: MIDIInput | null = null;
  private output: MIDIOutput | null = null;

  constructor(private store: DeviceStore) {}

  async connect(): Promise<void> {
    this.access = await navigator.requestMIDIAccess({ sysex: true });
    this.access.onstatechange = () => this.pickPorts();
    this.pickPorts();
    if (!this.input) throw new Error('No MIDI input found — is the device plugged in?');
    midiOut.sender = (bytes) => this.send(bytes);
  }

  disconnect(): void {
    if (this.input) this.input.onmidimessage = null;
    this.input = null;
    this.output = null;
    if (this.access) this.access.onstatechange = null;
    this.access = null;
    midiOut.sender = null;
    this.store.setConnected(false);
  }

  describe(): string {
    if (!this.input) return 'MIDI: no device';
    return `MIDI: ${this.input.name ?? 'in'}${this.output ? ` / ${this.output.name}` : ''}`;
  }

  /** Send raw MIDI bytes to the device (notes, CC, or protocol requests). */
  send(bytes: number[]): void {
    this.output?.send(bytes);
  }

  private pickPorts() {
    if (!this.access) return;
    const inputs = [...this.access.inputs.values()];
    const outputs = [...this.access.outputs.values()];
    const pick = <T extends MIDIPort>(ports: T[]) =>
      ports.find((p) => DEVICE_NAME.test(p.name ?? '')) ?? ports[0] ?? null;

    const input = pick(inputs);
    if (input !== this.input) {
      if (this.input) this.input.onmidimessage = null;
      this.input = input;
      if (input) {
        input.onmidimessage = (e: MIDIMessageEvent) => {
          if (e.data) this.onMessage(e.data);
        };
      }
    }
    this.output = pick(outputs);
    this.store.setConnected(!!this.input);
    if (this.output) this.send(encodeRequest()); // ask for a full state snapshot
  }

  private onMessage(data: Uint8Array) {
    if (applySysex(data, this.store)) return;
    // The device's own note output: pitched notes on ch1 carry the actual
    // pitch (scale/octave/root applied) — labels attach them to pad callouts.
    if (data.length < 3) return;
    const status = data[0] & 0xf0;
    const channel = data[0] & 0x0f;
    if (status === 0x90 && data[2] > 0) this.store.noteEvent(channel, data[1], true);
    else if (status === 0x80 || (status === 0x90 && data[2] === 0))
      this.store.noteEvent(channel, data[1], false);
  }
}
