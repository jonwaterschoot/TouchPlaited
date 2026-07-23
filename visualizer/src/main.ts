import './style.css';
import { DeviceStore } from './core/state';
import { Panel } from './panel/panel';
import { PanelBindings } from './panel/bindings';
import { Labels } from './panel/labels';
import { enablePadInteraction } from './panel/interact';
import { enablePanelLayout } from './panel/layout';
import { Toolbar } from './ui/toolbar';
import { CcPanel } from './ui/ccpanel';
import { MockTransport } from './transport/mock';
import { MidiTransport } from './transport/midi';

// URL flags (all combinable):
//   ?transparent   transparent page background (OBS browser-source overlay)
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
enablePanelLayout(panel, overlay);
const toolbar = new Toolbar(topbar, store);
new Labels(overlay, panel, store, (el) => toolbar.addMenuItem(el));
const ccPanel = new CcPanel((el) => toolbar.addMenuItem(el));
if (params.has('drawer')) ccPanel.open();
if (params.has('menu')) toolbar.openMenu();

if (params.has('transparent')) document.body.classList.add('transparent');
if (params.has('bare')) topbar.style.display = 'none';

const view = params.get('view');
if (view === 'pads') panel.svg.setAttribute('viewBox', '0 190 232.36 170.56');
else if (view === 'panel') panel.svg.setAttribute('viewBox', '0 20 232.36 200');

const zoom = parseFloat(params.get('zoom') ?? '');
if (!Number.isNaN(zoom) && zoom > 0) {
  panelWrap.style.transform = `scale(${zoom})`;
  panelWrap.style.transformOrigin = 'center center';
}

if (params.has('demo')) toolbar.start(new MockTransport(store));
else if (params.has('midi')) toolbar.start(new MidiTransport(store));
