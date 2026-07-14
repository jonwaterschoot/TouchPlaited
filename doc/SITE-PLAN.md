# TouchPlaited web presence — GitHub Pages plan

> **Status 2026-07-14: built.** Everything below is implemented and verified
> locally (`npm run build && npm run build:site` in `visualizer/`, then serve
> `_site/`). One deviation: the site builder lives at
> `visualizer/tools/build-site.mjs` (not `doc/`), so `marked` resolves from
> the visualizer's node_modules. Remaining manual step: **Settings → Pages →
> Source: "GitHub Actions"**, then push to main.

Three pages, one repo, one deploy, one theme:

| URL (github.io/TouchPlaited…) | What | Source of truth |
|---|---|---|
| `/` | Landing page — styled HTML render of README.md | `README.md` + `doc/` template |
| `/visualizer/` | Live-panel visualizer app | `visualizer/` (Vite build) |
| `/editor/` | SEQ pattern editor | `tools/pattern_editor.html` (copied verbatim) |

GitHub Pages serves exactly one site per repo, so "three apps" is really one
static artifact with three directories. Everything is client-side (Web MIDI,
WebAudio), so plain static hosting works; Pages is HTTPS, which Web MIDI
requires — the visualizer needs Chrome/Edge and grants SysEx per-origin, same
as on localhost.

## 1. Deploy: one Actions workflow, no gh-pages branch

`.github/workflows/pages.yml` (Settings → Pages → Source: **GitHub Actions**,
a one-time manual step):

1. checkout
2. `npm ci && npm run build` in `visualizer/`
3. `node doc/build-site.mjs` — renders README.md → `_site/index.html`
   through the `doc/` template (needs `marked`, added as a visualizer
   devDependency so there's a single `package.json`)
4. assemble:
   ```
   _site/
     index.html            ← from build-site.mjs
     theme.css             ← doc/theme.css
     img/…                 ← copied (README images)
     visualizer/…          ← visualizer/dist
     editor/index.html     ← tools/pattern_editor.html
     .nojekyll
   ```
5. `upload-pages-artifact` + `deploy-pages`

Trigger: push to `main` touching `visualizer/`, `tools/pattern_editor.html`,
`doc/`, `README.md`, `img/`; plus `workflow_dispatch`.

One code change makes the visualizer path-independent: `base: './'` in
`vite.config.ts` (relative asset URLs — no router, so this is safe, and dev
mode is unaffected).

## 2. Unified theme — `doc/theme.css`

Dark, flat, sharp, warm. Tokens (the Simple Touch illustration yellow as the
one accent; backgrounds stay near-black so panel colors carry no meaning):

```css
:root {
  --bg:        #141412;  /* page — warm near-black */
  --surface:   #1d1c19;  /* the ONE raised tone (cards, drawers) */
  --line:      #35322b;  /* borders */
  --text:      #e8e4da;  /* ~13:1 on --bg */
  --dim:       #8f8a7d;  /* secondary text, ~4.6:1 */
  --accent:    #edb655;  /* Simple Touch yellow, ~9:1 — interactive + headings */
  --accent-hi: #ffd97a;  /* hover only */
  --value:     #9fd0ff;  /* live values (visualizer convention, kept) */
  --alert:     #e6371e;
}
* { border-radius: 0 !important; }        /* sharp corners, everywhere */
/* flat: no gradients, no box-shadows, no backdrop blur; hierarchy comes
   from --line borders and type, not elevation */
body { background: var(--bg); color: var(--text);
  font: 14px/1.5 "Cascadia Mono", Consolas, ui-monospace, monospace; }
```

Per app:

- **Editor** (`tools/pattern_editor.html`): already flat + sharp
  (`border-radius: 0 !important`) — it's the model. Retheme = swap its
  `:root` block: greenish `--bg/--panel/--line/--txt` → the tokens above,
  and the lime `--w` (chance "always") → `--accent` yellow. Its 75/50/25 %
  chance ramp (amber → orange → red) is already warm; keep it. The file
  stays self-contained (must keep working from `file://`), so the ~10 token
  values are deliberately duplicated with a `/* keep in sync with
  doc/theme.css */` comment.
- **Visualizer**: `style.css` moves onto the tokens (colors already match
  the target palette). Mechanical flattening: all `border-radius` → 0, drop
  the `backdrop-filter` blur and `drop-shadow`s, keep the info-panel
  translucency (readability over artwork ≠ elevation).
- **Landing**: built on `theme.css` directly.

## 3. Landing page — `doc/`

- `doc/template.html` — header (`TOUCHPLAITED`, nav: **Visualizer · Pattern
  editor · Manual · GitHub**), content slot, footer. Max-width ~72ch,
  single column, no hero art beyond the existing README images.
- `doc/build-site.mjs` — small Node script: `marked` renders README.md
  (and MANUAL.md → `/manual.html`, same template — cheap now, and it seeds
  the future interactive manual), rewrites the few intra-repo links
  (`MANUAL.md` → `manual.html`, `tools/pattern_editor.html` → `editor/`),
  copies `img/`.
- Local preview: `node doc/build-site.mjs && npx serve _site`.

Generating from README (instead of hand-writing HTML) keeps one source of
truth — the landing page can never drift from the repo README.

## 4. Future: MkDocs (page #3-and-beyond)

When docs outgrow one page: MkDocs (Material, `docs_dir: doc/docs`) takes
over `/`, with the README landing content becoming its `index.md`. The same
tokens go into an `extra_css` override so the theme carries over. The
workflow gains a Python step and merges `mkdocs build` output into `_site/`
**around** `visualizer/` and `editor/`, which keep their URLs. Nothing in
this plan blocks that — only ownership of `/` moves.

## 5. Order of work

1. `vite.config.ts`: `base: './'`.
2. `doc/theme.css` + `doc/template.html` + `doc/build-site.mjs`; wire
   `marked` into `visualizer/package.json` (`npm run build:site`).
3. Retheme the editor's `:root` (small diff, screenshot before/after).
4. Flatten/tokenize the visualizer CSS.
5. `.github/workflows/pages.yml`; push; flip Settings → Pages to "GitHub
   Actions".
6. Verify all three URLs + Web MIDI connect from the hosted visualizer;
   add the links to README.




---

Playmode overhaul:

instead of Random we'll introduce ARP / MEL

So we'll have 
1. Basic Pitch
2. Arp / melody
3. Sequencer

2. is new, we'll reuse as much logic as possible, keeping controls tied to the other playmodes, using the catch up logic for pots and switches

There's an Arp mode and a rec mode, SW1 handles three states: Arp, Arp hold, Rec mode

Controls will be:

S30 - Drive (applied only to arp)
S31 - Division (for arp mode, can also change speed of recorded mode)
S32 - Swing
S33 - Density - first half = 0-100% Euclidean fill + chance, second half is Euclidean fill (meaning second half will generate a steady pattern)
S34 - Decay
S35 - Arp order (does not apply to Rec)
      - played order, ascend, descend, pingpong, random (random is default)
S36 - Normal Mix like in basic pitch
P0 / P2 + S35 = change model

Arp modes from sw1
- Arp: dynamically adds notes to the arp as long as they're held
- Arp: hold notes, toggle to remove from arp pool

  - Octaves: P10 / P11 change base octave for the playing notes
    - Use P0 + P10 / P11 to add / subtract a range of octaves

Problems to decide: 
  - clear an arp by leaving arp mode or by holding P0 + P2
  - Otherwise arp keeps playing when sequencer is running
  - if you want to play only the arp without seq, what should we do?
  - tied to play / stop? always running? what's the general approach here
  - sequencer running in all modes P2 + P11 is start sequencer, we could also do it per mode, or use P2 + P10 for arp , P2 + P11 for sequencer

Recording:
- As soon as SW1 is in REC it starts listening to the pads
- we record in layers
- undo allows going back layers
- max layers?
- Octave mode apply to live notes or to recorded seq?
- what's the influence of the controls over the recording?
  - S30 drive = ok
  - S31 devision > not applied from the start, after knob is touched can spread recording, center is normal speed, left slower, right faster
  - S32 Swing = yes?
  - S33 density = > not applied from the start, after knob is touched can add chance left 0 % chance, right 100 %
  - S34 Decay = ok, move while recording to change the decay per note
  - S35 Order = use this to shift timing; center original timing, left move back, right move forward 