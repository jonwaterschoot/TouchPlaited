// Reposition/resize the device drawing itself: one pointer on a non-pad area
// drags it, two pointers pinch-zoom, the mouse wheel zooms. A drag grip
// pinned to the drawing's top-left corner gives touch a dedicated target —
// the old double-click reset is gone (double-tapping a pad to play it kept
// resetting the position). Persisted across sessions like the info panel. The
// overlay code measures everything through getBoundingClientRect / the
// viewBox, so the CSS transform is picked up automatically.
//
// The A−/A+ text-size buttons and the reset that used to ride along with the
// grip now live in the one settings bar (ui/settings-bar.ts); the grip stays
// here because it drags this drawing and has to sit on it.

import type { Panel } from './panel';
import { svgToOverlay, labelScale, setLabelScale } from './overlay-utils';
import { SettingsBar, SETTINGS_TOGGLE_EVENT } from '../ui/settings-bar';

const KEY = 'tp-panel-layout';
const MIN_SCALE = 0.4;
const MAX_SCALE = 3;

/** The pan is stored in screen px and the zoom is relative to a letterboxed
 * fit, so a transform only means anything in the viewport shape it was made
 * in. Rotating a phone is the case that matters: the saved offsets put the
 * drawing somewhere you then have to pinch to find. */
const isPortrait = () => window.innerHeight > window.innerWidth;

export function enablePanelLayout(panel: Panel, overlay: HTMLElement, settings: SettingsBar) {
  const svg = panel.svg;
  let dx = 0, dy = 0, scale = 1;
  // Orientation the current transform belongs to.
  let madeInPortrait = isPortrait();
  try {
    const s = JSON.parse(localStorage.getItem(KEY) ?? '');
    if (typeof s.dx === 'number' && typeof s.dy === 'number' && typeof s.scale === 'number') {
      // Only adopt it if the screen is still the shape it was made for;
      // otherwise start from the plain letterboxed fit, which is what the
      // CSS gives us for free.
      if (typeof s.portrait !== 'boolean' || s.portrait === madeInPortrait)
        ({ dx, dy, scale } = s);
    }
  } catch {
    /* defaults */
  }

  svg.style.transformOrigin = 'center center';
  svg.style.cursor = 'grab'; // pads set their own pointer cursor
  const apply = () => {
    svg.style.transform =
      dx || dy || scale !== 1 ? `translate(${dx}px, ${dy}px) scale(${scale})` : '';
    // Static label overlays are positioned in overlay px — tell them to
    // follow the drawing (labels.ts listens, rAF-throttled).
    window.dispatchEvent(new Event('tp-panel-layout'));
  };
  const save = () => {
    madeInPortrait = isPortrait();
    localStorage.setItem(KEY, JSON.stringify({ dx, dy, scale, portrait: madeInPortrait }));
  };
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

  // --- drag grip, and this drawing's entries in the settings bar ----------

  const cluster = document.createElement('div');
  cluster.className = 'info-controls panel-handle';
  const grip = document.createElement('span');
  grip.className = 'panel-grip';
  grip.textContent = '⠿';
  grip.title = 'Drag the device drawing (pinch or scroll to zoom)';
  cluster.append(grip);
  overlay.appendChild(cluster);

  // Same step and range as the info panel's pair (overlay-utils clamps both
  // to 0.6–2.2) — they are two different texts, but "make it bigger" has to
  // mean the same thing in both.
  settings.addGroup(
    'Panel',
    20,
    SettingsBar.button('A−', 'Smaller faceplate label & screen text',
                       () => setLabelScale(labelScale() - 0.15)),
    SettingsBar.button('A+', 'Larger faceplate label & screen text',
                       () => setLabelScale(labelScale() + 0.15)),
  );
  settings.onReset(() => {
    dx = 0;
    dy = 0;
    scale = 1;
    localStorage.removeItem(KEY);
    setLabelScale(1);
    apply();
  });

  // Pinned near the drawing's top-left corner but clamped into the stage,
  // so the grip stays reachable even when the drawing is dragged out of view.
  const place = () => {
    const p = svgToOverlay(svg, overlay, 2, 2);
    const o = overlay.getBoundingClientRect();
    const x = Math.min(Math.max(4, p.x), o.width - cluster.offsetWidth - 4);
    let y = Math.min(Math.max(4, p.y), o.height - cluster.offsetHeight - 4);
    // The settings bar is pinned to the same corner of the stage, and on a
    // near-square viewport the drawing's own corner lands right under it —
    // drop below the bar rather than hiding beneath it.
    const bar = settings.bounds(); // zero-sized when hidden by ?bare
    const overlaps = bar.width > 0
      && x < bar.right - o.left + 4 && x + cluster.offsetWidth + 4 > bar.left - o.left
      && y < bar.bottom - o.top + 4 && y + cluster.offsetHeight + 4 > bar.top - o.top;
    if (overlaps)
      y = Math.min(bar.bottom - o.top + 4, o.height - cluster.offsetHeight - 4);
    cluster.style.left = `${x.toFixed(1)}px`;
    cluster.style.top = `${y.toFixed(1)}px`;
  };
  place();
  window.addEventListener('resize', place);
  window.addEventListener(SETTINGS_TOGGLE_EVENT, place);
  let raf = 0;
  window.addEventListener('tp-panel-layout', () => {
    if (raf) return;
    raf = requestAnimationFrame(() => {
      raf = 0;
      place();
    });
  });

  // Grip drag moves the drawing — same state as the on-drawing drag, so
  // both persist through save().
  let gripStart = { x: 0, y: 0, dx: 0, dy: 0 };
  grip.addEventListener('pointerdown', (e) => {
    grip.setPointerCapture(e.pointerId);
    gripStart = { x: e.clientX, y: e.clientY, dx, dy };
  });
  grip.addEventListener('pointermove', (e) => {
    if (!grip.hasPointerCapture(e.pointerId)) return;
    dx = gripStart.dx + e.clientX - gripStart.x;
    dy = gripStart.dy + e.clientY - gripStart.y;
    apply();
  });
  grip.addEventListener('pointerup', (e) => {
    grip.releasePointerCapture(e.pointerId);
    save();
  });

  // The CSS already letterboxes the drawing into the stage in both axes, so
  // "fit to the viewport" means dropping a pan/zoom that has taken it out of
  // that fit — a stored transform from a desktop session, or from the other
  // orientation, is the usual way a phone ends up having to pinch.
  const fit = () => {
    if (!dx && !dy && scale === 1) return;
    dx = 0;
    dy = 0;
    scale = 1;
    apply();
    save();
  };

  // Turning the phone re-fits. Nothing else auto-fits: on a desktop the
  // window changing size is not a reason to throw away a framing you set up.
  window.addEventListener('resize', () => {
    if (isPortrait() !== madeInPortrait) fit();
    madeInPortrait = isPortrait();
  });

  return { fit };
}
