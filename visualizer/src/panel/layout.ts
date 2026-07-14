// Reposition/resize the device drawing itself: one pointer on a non-pad area
// drags it, two pointers pinch-zoom, the mouse wheel zooms, double-click
// resets. Persisted across sessions like the info panel. The overlay code
// measures everything through getBoundingClientRect / the viewBox, so the CSS
// transform is picked up automatically.

import type { Panel } from './panel';

const KEY = 'tp-panel-layout';
const MIN_SCALE = 0.4;
const MAX_SCALE = 3;

export function enablePanelLayout(panel: Panel) {
  const svg = panel.svg;
  let dx = 0, dy = 0, scale = 1;
  try {
    const s = JSON.parse(localStorage.getItem(KEY) ?? '');
    if (typeof s.dx === 'number' && typeof s.dy === 'number' && typeof s.scale === 'number')
      ({ dx, dy, scale } = s);
  } catch {
    /* defaults */
  }

  svg.style.transformOrigin = 'center center';
  svg.style.cursor = 'grab'; // pads set their own pointer cursor
  const apply = () => {
    svg.style.transform =
      dx || dy || scale !== 1 ? `translate(${dx}px, ${dy}px) scale(${scale})` : '';
  };
  const save = () => localStorage.setItem(KEY, JSON.stringify({ dx, dy, scale }));
  const clampScale = (s: number) => Math.min(MAX_SCALE, Math.max(MIN_SCALE, s));
  apply();

  const pts = new Map<number, { x: number; y: number }>();
  let sDx = 0, sDy = 0, sScale = 1, sDist = 0, sMid = { x: 0, y: 0 };

  // (re)baseline the gesture from the current pointer set — called when a
  // pointer joins or leaves so drags/pinches hand over without jumps
  const gestureStart = () => {
    sDx = dx;
    sDy = dy;
    sScale = scale;
    const p = [...pts.values()];
    if (p.length >= 2) {
      sDist = Math.hypot(p[0].x - p[1].x, p[0].y - p[1].y);
      sMid = { x: (p[0].x + p[1].x) / 2, y: (p[0].y + p[1].y) / 2 };
    } else if (p.length === 1) {
      sDist = 0;
      sMid = { ...p[0] };
    }
  };

  svg.addEventListener('pointerdown', (e) => {
    // pads play notes — don't also start a drag from them
    if ((e.target as Element).closest?.('[id^="pad-"]')) return;
    svg.setPointerCapture(e.pointerId);
    pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    gestureStart();
  });

  svg.addEventListener('pointermove', (e) => {
    if (!pts.has(e.pointerId)) return;
    pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    const p = [...pts.values()];
    if (p.length >= 2 && sDist > 0) {
      const dist = Math.hypot(p[0].x - p[1].x, p[0].y - p[1].y);
      scale = clampScale(sScale * (dist / sDist));
      dx = sDx + (p[0].x + p[1].x) / 2 - sMid.x;
      dy = sDy + (p[0].y + p[1].y) / 2 - sMid.y;
    } else {
      dx = sDx + e.clientX - sMid.x;
      dy = sDy + e.clientY - sMid.y;
    }
    apply();
  });

  const end = (e: PointerEvent) => {
    if (!pts.delete(e.pointerId)) return;
    if (pts.size > 0) gestureStart();
    save();
  };
  svg.addEventListener('pointerup', end);
  svg.addEventListener('pointercancel', end);

  svg.addEventListener(
    'wheel',
    (e) => {
      e.preventDefault();
      scale = clampScale(scale * Math.exp(-e.deltaY / 480));
      apply();
      save();
    },
    { passive: false },
  );

  svg.addEventListener('dblclick', () => {
    dx = 0;
    dy = 0;
    scale = 1;
    localStorage.removeItem(KEY);
    apply();
  });
}
