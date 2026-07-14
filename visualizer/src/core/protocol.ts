// TouchPlaited telemetry SysEx — must stay in lockstep with midi/telemetry.cpp
// in the firmware.
//
//   F0 7D 54 50 <ver> <type> <payload…> F7      "TP" = 0x54 0x50, mfr 0x7D
//
// STATE (0x01) payload, 18 bytes, all 7-bit (bytes 16-17 appended in fw v2;
// decode guards on length so 16-byte v1 frames still parse):
//   0    pads P0..P6 bitmask (bit0 = P0)
//   1    pads P7..P11 bitmask (bit0 = P7)
//   2-9  S30..S37, 0..127
//   10   SW1 position 0..2
//   11   SW2 position 0..2
//   12   user LED brightness 0..127
//   13   model 0..23
//   14   mode flags: bits0-1 mode (0 Seq, 1 Random, 2 Pitch), bit2 playing
//   15   seq step, 0x7F = none
//   16   octave offset + 3, 0..6
//   17   root semitone, 0..11
//
// FX (0x04) payload: drive, reverb, delay, nTrims, trims…   (all 0..127)
// EVENT (0x02) payload: id, arg — 1 padDown, 2 padUp (low-latency touch feel)
// HELLO (0x03) payload: verMajor, verMinor, featureBits
// REQUEST (0x7E), host→device, no payload: please send HELLO + STATE + FX now.
//
// Firmware v1 sends STATE (incl. 500 ms heartbeat, which covers joining
// mid-stream) and FX; EVENT/HELLO/REQUEST are reserved — decoding them here
// already so a firmware that adds them needs no webapp change.

import type { DeviceStore } from './state';

export const SYSEX_START = 0xf0;
export const SYSEX_END = 0xf7;
export const HEADER = [0x7d, 0x54, 0x50] as const;
export const PROTOCOL_VERSION = 1;

export const FrameType = {
  STATE: 0x01,
  EVENT: 0x02,
  HELLO: 0x03,
  FX: 0x04,
  REQUEST: 0x7e,
} as const;

export const EventId = {
  PAD_DOWN: 1,
  PAD_UP: 2,
} as const;

export function isTelemetry(data: Uint8Array): boolean {
  return (
    data.length >= 7 &&
    data[0] === SYSEX_START &&
    data[1] === HEADER[0] &&
    data[2] === HEADER[1] &&
    data[3] === HEADER[2]
  );
}

export function encodeRequest(): number[] {
  return [SYSEX_START, ...HEADER, PROTOCOL_VERSION, FrameType.REQUEST, SYSEX_END];
}

/** Decode one telemetry SysEx message into the store. Returns false if the
 * message wasn't ours (caller may then treat it as ordinary MIDI). */
export function applySysex(data: Uint8Array, store: DeviceStore): boolean {
  if (!isTelemetry(data)) return false;
  // data[4] = protocol version; only structural changes should bump it, so an
  // unknown version is still worth trying to parse rather than going dark.
  const type = data[5];
  const p = data.subarray(6, data.length - 1); // payload without trailing F7

  switch (type) {
    case FrameType.STATE: {
      if (p.length < 16) return true;
      for (let i = 0; i < 7; i++) store.setPad(i, ((p[0] >> i) & 1) === 1);
      for (let i = 0; i < 5; i++) store.setPad(7 + i, ((p[1] >> i) & 1) === 1);
      for (let i = 0; i < 8; i++) store.setControl(i, p[2 + i] / 127);
      store.setSw('A', p[10]);
      store.setSw('B', p[11]);
      store.setLed(p[12] / 127);
      store.setModel(p[13]);
      store.setMode(p[14] & 0x03);
      store.setPlaying((p[14] & 0x04) !== 0);
      store.setSeqStep(p[15] === 0x7f ? null : p[15]);
      if (p.length >= 18) {
        store.setOctave(p[16] - 3);
        store.setRoot(p[17]);
      }
      return true;
    }
    case FrameType.EVENT: {
      if (p.length < 2) return true;
      if (p[0] === EventId.PAD_DOWN) store.setPad(p[1], true);
      else if (p[0] === EventId.PAD_UP) store.setPad(p[1], false);
      return true;
    }
    case FrameType.FX: {
      if (p.length < 4) return true;
      const trims: number[] = [];
      const n = Math.min(p[3], p.length - 4);
      for (let i = 0; i < n; i++) trims.push(p[4 + i] / 127);
      store.setFx({
        drive: p[0] / 127,
        reverb: p[1] / 127,
        delay: p[2] / 127,
        ...(trims.length ? { trims } : {}),
      });
      return true;
    }
    case FrameType.HELLO:
      // Firmware identity; nothing to render yet, but it confirms the link.
      return true;
    default:
      return true; // ours, but a frame type we don't know — swallow it
  }
}
