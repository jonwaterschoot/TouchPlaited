import './style.css';
import { DeviceStore } from './core/state';
import { Panel } from './panel/panel';
import { PanelBindings } from './panel/bindings';
import { Labels } from './panel/labels';
import { enablePadInteraction } from './panel/interact';
import { enablePanelLayout } from './panel/layout';
import { Toolbar } from './ui/toolbar';
import { SettingsMenu } from './ui/settings-menu';
import { CcPanel } from './ui/ccpanel';
import { MockTransport } from './transport/mock';
import { MidiTransport } from './transport/midi';

// URL flags (all combinable):
//   ?transparent   transparent page background (OBS browser-source overlay)
//   ?bg=green|blue|magenta   solid key colour, for chroma-keying a window capture
//   ?bare          hide the toolbar
//   ?view=pads|panel   crop to the pad field / the knob panel
//   ?zoom=1.5      scale everything
//   ?demo          autostart the scripted demo
//   ?midi          autoconnect Web MIDI (needs a prior permission grant)
//   ?drawer        open the MIDI drawer
//   ?menu          open the hamburger menu (mostly for screenshots/tests)
const params = new URLSearchParams(location.search);

const store = new DeviceStore();
const panelWrap = document.getElementById('panel-wrap')!;
const overlay = document.getElementById('overlay')!;
const topbar = document.getElementById('topbar')!;

const panel = new Panel(panelWrap);
new PanelBindings(panel, store);
enablePadInteraction(panel, store);
// Every display setting lives in the ☰ menu's Settings section; the drawing
// and the info panel keep only their drag grips.
const toolbar = new Toolbar(topbar, store);
const settings = new SettingsMenu(toolbar.settingsSection());
const layout = enablePanelLayout(panel, overlay, settings);
new Labels(overlay, panel, store, settings, (el, section) => toolbar.addMenuItem(el, section));
const ccPanel = new CcPanel((el) => toolbar.addMenuItem(el));
toolbar.addAction('Fit to screen', () => layout.fit());
if (params.has('drawer')) ccPanel.open();
if (params.has('menu')) toolbar.openMenu();

if (params.has('transparent')) document.body.classList.add('transparent');
else enableBackgroundSetting();
if (params.has('bare')) topbar.style.display = 'none';

// Mobile browsers change the *visual* viewport when the URL bar slides away
// without firing a window resize, so the overlays would sit against a stage
// height that no longer exists. Everything positioned over the drawing already
// re-places on resize — hand it the same event, one per frame (the URL bar
// animating fires this a dozen times, and a re-place re-renders the labels).
let vvRaf = 0;
window.visualViewport?.addEventListener('resize', () => {
  if (vvRaf) return;
  vvRaf = requestAnimationFrame(() => {
    vvRaf = 0;
    window.dispatchEvent(new Event('resize'));
  });
});

const view = params.get('view');
if (view === 'pads') panel.svg.setAttribute('viewBox', '0 190 232.36 170.56');
else if (view === 'panel') panel.svg.setAttribute('viewBox', '0 20 232.36 200');

const zoom = parseFloat(params.get('zoom') ?? '');
if (!Number.isNaN(zoom) && zoom > 0) {
  panelWrap.style.transform = `scale(${zoom})`;
  panelWrap.style.transformOrigin = 'center center';
}

/** Page background, chosen under ☰ → Settings. OBS's own browser can't reach
 * Web MIDI, so following the device on stream means capturing Chrome and
 * chroma-keying a solid colour away; ?transparent covers the Browser Source
 * case and hides this row. ?bg= picks one for this load without storing it. */
function enableBackgroundSetting() {
  const KEY = 'tp-bg';
  const BGS = [
    ['Default', ''],
    ['Green', '#00ff00'],
    ['Blue', '#0000ff'],
    ['Magenta', '#ff00ff'],
  ] as const;
  const find = (name: string | null) =>
    Math.max(0, BGS.findIndex(([n]) => n.toLowerCase() === name?.toLowerCase()));
  let i = find(params.get('bg') ?? localStorage.getItem(KEY));
  const btn = SettingsMenu.button('', 'Page background: a solid colour to chroma-key out in OBS', () => {
    i = (i + 1) % BGS.length;
    localStorage.setItem(KEY, BGS[i][0].toLowerCase());
    apply();
  });
  const apply = () => {
    const [name, colour] = BGS[i];
    document.body.style.background = colour;
    document.body.classList.toggle('keyed', !!colour);
    btn.innerHTML = colour
      ? `<span class="bg-swatch" style="background:${colour}"></span>${name}`
      : name;
  };
  apply();
  settings.addGroup('Background', 40, btn);
}

if (params.has('demo')) toolbar.start(new MockTransport(store));
else if (params.has('midi')) toolbar.start(new MidiTransport(store));
