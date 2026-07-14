// Derives visualizer/assets/panel.svg from img/simpletouchdrawing_blank.svg.
//
// The Illustrator export has one object per interactive part but almost no ids.
// Instead of renaming layers in the .ai file, this script stamps stable ids onto
// the export (matching elements by their geometry, which survives re-export as
// long as the artwork itself doesn't move) so the app can bind to them.
//
// Usage:  node tools/build-panel-svg.mjs
//
// Re-run after re-exporting the .ai. Every match is asserted, so a moved or
// deleted shape fails loudly here instead of silently breaking the app.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const srcPath = join(here, '..', '..', 'img', 'simpletouchdrawing_blank.svg');
const outPath = join(here, '..', 'assets', 'panel.svg');

let svg = readFileSync(srcPath, 'utf8');

let failures = [];
function sub(name, pattern, replacement, expected = 1) {
  let n = 0;
  svg = svg.replace(pattern, (...args) => {
    n++;
    if (typeof replacement === 'function') return replacement(...args);
    // manual $N substitution — the function wrapper disables the native one
    return replacement.replace(/\$(\d+)/g, (_, d) => args[Number(d)] ?? '');
  });
  if (n !== expected) failures.push(`${name}: expected ${expected} match(es), got ${n}`);
}

// --- root id ---------------------------------------------------------------
sub('root id', /<svg id="Laag_1"/, '<svg id="panel"');

// --- knobs: anonymous <g><circle cx cy .../><line .../></g> -----------------
// Matched by circle center. Pointer line at rest points to 7:30 (min position).
const knobs = [
  ['knob-s30', '49.78', '175.03'],
  ['knob-s31', '49.78', '126.37'],
  ['knob-s32', '94.02', '126.37'],
  ['knob-s33', '138.27', '126.37'],
  ['knob-s34', '182.51', '126.19'],
  ['knob-s35', '182.51', '175.03'],
];
for (const [id, cx, cy] of knobs) {
  sub(
    id,
    new RegExp(`<g>(\\s*<circle class="st6" cx="${cx.replace('.', '\\.')}" cy="${cy.replace('.', '\\.')}")`),
    `<g id="${id}" data-cx="${cx}" data-cy="${cy}">$1`
  );
}

// --- fader handles ----------------------------------------------------------
// Track slots (cut into Faceplate_main_shape) span y 122.19..193.49; handle is
// 12.47 tall, so its top edge travels ~123.5 (value 1) .. ~179.5 (value 0).
const faderData = 'data-min-y="123.5" data-max-y="179.5"';
sub(
  'fader-s36',
  /<rect class="st6" x="13\.06" y="157\.84"/,
  `<rect id="fader-s36" ${faderData} class="st6" x="13.06" y="157.84"`
);
sub(
  'fader-s37',
  /<rect class="st6" x="214\.72" y="158\.87"/,
  `<rect id="fader-s37" ${faderData} class="st6" x="214.72" y="158.87"`
);

// --- pads: paths inside <g id="PADS">, matched by their moveto ---------------
const pads = [
  ['pad-p0', 'M108.51,251.32'],
  ['pad-p1fx', 'M133.54,223.87'],
  ['pad-p2', 'M188.17,272.52'],
  ['pad-p3', 'M41.49,278.33'],
  ['pad-p4', 'M92.46,311.32'],
  ['pad-p5', 'M129.83,327.63'],
  ['pad-p6', 'M188.25,313.81'],
  ['pad-p7', 'M227.35,354.92'],
  ['pad-p8', 'M107.4,355.79'],
  ['pad-p9', 'M197.91,353.51'],
];
for (const [id, moveto] of pads) {
  sub(
    id,
    new RegExp(`<path class="(st\\d+)" d="${moveto.replace(/\./g, '\\.')}`),
    `<path id="${id}" class="$1" d="${moveto}`
  );
}

// Degenerate leftover fragments in the PADS group (empty or sliver subpaths
// from the drawing process) — drop them.
const strays = [
  'M31.52,248.56', 'M47.43,208.62', 'M47.43,208.49', 'M47.06,208.44',
  'M54.91,327.69', 'M92.68,257.9', 'M65.36,205.59', 'M160.04,206.08',
  'M97.23,213.5',
];
for (const moveto of strays) {
  sub(
    `stray ${moveto}`,
    new RegExp(`\\s*<path class="st\\d+" d="${moveto.replace(/\./g, '\\.')}[^"]*"/>`),
    ''
  );
}

// --- P10 / P11: the "TouCH" logo halves ARE the pads ("Tou" = P10, "CH" =
// P11, confirmed on hardware). The letters are drawn as two standalone fill
// paths followed by an outline group; regroup them into one <g> per half so
// the letter shapes themselves highlight (CSS recolors the st3 fills and
// glows the group).
let touFill = '';
sub('logo tou fill', /\s*<path class="st3" d="M126\.69,[^"]*"\/>/, (m) => {
  touFill = m.trim();
  return '';
});
let chFill = '';
sub('logo ch fill', /\s*<path class="st3" d="M159\.41,[^"]*"\/>/, (m) => {
  chFill = m.trim();
  return '';
});
sub('logo pad groups', /<g>\s*<path class="st10" d="M102\.53[\s\S]*?<\/g>\s*<\/g>/, (block) => {
  const pick = (moveto) => {
    const re = new RegExp(`<path class="st\\d+" d="${moveto.replace(/\./g, '\\.')}[^"]*"/>`);
    const m = block.match(re);
    if (!m) failures.push(`logo path ${moveto} not found in outline block`);
    return m ? m[0] : '';
  };
  // document order within each half preserved
  const p10 = [touFill, pick('M102.53'), pick('M103.76'), pick('M127.31'), pick('M126.01')];
  const p11 = [chFill, pick('M144.66'), pick('M158.73'), pick('M160.48')];
  return `<g id="pad-p10">\n    ${p10.join('\n    ')}\n  </g>\n  <g id="pad-p11">\n    ${p11.join('\n    ')}\n  </g>`;
});

// --- LEDs --------------------------------------------------------------------
// The export already names the firmware-driven LED "userLED"; keep the concept,
// normalise the id. The unnamed sibling group is the other on-board LED.
sub('led-user', /<g id="userLED">/, '<g id="led-user">');
sub('led-r', /<g>(\s*<rect class="st0" x="193\.47")/, '<g id="led-r">$1');

// --- switches ----------------------------------------------------------------
// Two red tag rects float loose in the doc; two arrow-glyph groups sit at the
// end ("SW2" belongs to SW2, "SW21" is mislabeled data-name but sits over SW1).
// Pull each rect into its group and tag the three option glyphs so the app can
// highlight the active position (sw-opt-0 / 1 / 2 in panel order).
let sw2rect = '';
sub(
  'extract sw2 rect',
  /\s*<rect class="st12" x="114\.75"[^/]*\/>/,
  (m) => { sw2rect = m.trim(); return ''; }
);
let sw1rect = '';
sub(
  'extract sw1 rect',
  /\s*<rect class="st12" x="69\.62"[^/]*\/>/,
  (m) => { sw1rect = m.trim(); return ''; }
);

sub('sw2 group', /<g id="SW2">/, `<g id="sw2">\n    ${sw2rect}`);
sub('sw1 group', /<g id="SW21" data-name="SW2">/, `<g id="sw1">\n    ${sw1rect}`);

// Option glyphs, in document order first→third within each group.
// sw1 (horizontal ◄ ● ►): left / centre / right.
sub('sw1 opt0', /<path class="st2" d="M78\.52/, '<path class="st2 sw-opt sw-opt-0" d="M78.52');
sub('sw1 opt1', /<path class="st2" d="M83\.96/, '<path class="st2 sw-opt sw-opt-1" d="M83.96');
sub('sw1 opt2', /<path class="st2" d="M89\.49/, '<path class="st2 sw-opt sw-opt-2" d="M89.49');
// sw2: the tag is drawn pre-rotated (-60°), so document order is the visual
// BOTTOM arrow first — assign classes reversed so sw-opt-0 is the visual top
// (verified against hardware: up = Seq).
sub('sw2 opt0', /<path class="st2" d="M128\.72/, '<path class="st2 sw-opt sw-opt-2" d="M128.72');
sub('sw2 opt1', /<path class="st2" d="M131\.16/, '<path class="st2 sw-opt sw-opt-1" d="M131.16');
sub('sw2 opt2', /<path class="st2" d="M134\.24/, '<path class="st2 sw-opt sw-opt-0" d="M134.24');

// -----------------------------------------------------------------------------
if (failures.length) {
  console.error('panel.svg build FAILED:\n  ' + failures.join('\n  '));
  process.exit(1);
}

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, svg);
console.log(`wrote ${outPath} (${svg.length} bytes)`);
