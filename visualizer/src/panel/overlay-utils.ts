// Shared helpers for HTML elements overlaid on the device drawing.

/** Map a point in SVG user units to overlay-local px. preserveAspectRatio
 * default: content scaled uniformly, centered — the SVG element box is
 * letterboxed when width-limited (portrait phones), so the element rect is
 * NOT the drawn device; go through the viewBox instead. */
export function svgToOverlay(
  svg: SVGSVGElement, overlay: HTMLElement, x: number, y: number,
): { x: number; y: number } {
  const vb = svg.viewBox.baseVal;
  const s = svg.getBoundingClientRect();
  const o = overlay.getBoundingClientRect();
  const scale = Math.min(s.width / vb.width, s.height / vb.height);
  const ox = s.left - o.left + (s.width - vb.width * scale) / 2 - vb.x * scale;
  const oy = s.top - o.top + (s.height - vb.height * scale) / 2 - vb.y * scale;
  return { x: ox + x * scale, y: oy + y * scale };
}

// User-set text scale shared by the static labels and the OLED screen —
// the A−/A+ buttons on the device's handle cluster (layout.ts) write it,
// listeners re-render on the change event.
const LABEL_SCALE_KEY = 'tp-label-scale';
export const LABEL_SCALE_EVENT = 'tp-label-scale';

export function labelScale(): number {
  const v = parseFloat(localStorage.getItem(LABEL_SCALE_KEY) ?? '1');
  return Number.isFinite(v) ? Math.min(2.2, Math.max(0.6, v)) : 1;
}

export function setLabelScale(v: number) {
  localStorage.setItem(LABEL_SCALE_KEY, String(Math.min(2.2, Math.max(0.6, v))));
  window.dispatchEvent(new Event(LABEL_SCALE_EVENT));
}
