// A transport feeds the DeviceStore from some link to the device (Web MIDI
// today; Web Serial or a mock later/alongside). Transports are the only code
// allowed to write into the store.

export interface Transport {
  readonly kind: string;
  connect(): Promise<void>;
  disconnect(): void;
  /** Human-readable link description for the toolbar. */
  describe(): string;
}
