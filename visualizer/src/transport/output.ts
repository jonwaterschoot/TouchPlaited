// Global MIDI-out facade: UI elements (pads, CC sliders, piano) send through
// this without knowing which transport is active. The MIDI transport registers
// itself on connect; in demo/disconnected state sends are dropped silently.

export const midiOut = {
  sender: null as ((bytes: number[]) => void) | null,

  send(bytes: number[]) {
    this.sender?.(bytes);
  },

  get available(): boolean {
    return this.sender !== null;
  },
};
