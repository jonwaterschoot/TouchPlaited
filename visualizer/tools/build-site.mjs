// Assembles the GitHub Pages site into <repo>/_site/ (see doc/SITE-PLAN.md):
//
//   index.html   ← README.md rendered through doc/template.html
//   manual.html  ← MANUAL.md, same template
//   theme.css, site-head.css ← doc/
//   img/, docs/  ← the repo's images (README/MANUAL images, the control map)
//   play/        ← the Simple Touch labels web app (player.html and what it
//                  runs on), with this site's header and doc/play.css, plus a
//                  copy of simple-touch-labels.json and the faceplate
//   editor/      ← tools/pattern_editor.html, copied verbatim
//   codemap/     ← tools/codemap.html, copied verbatim
//   visualizer/  ← visualizer/dist (run `npm run build` first)
//
// play/ needs a checkout of https://github.com/jonwaterschoot/simple-touch-labels:
// $SIMPLE_TOUCH_LABELS, else <repo>/_labels (where pages.yml puts it), else
// the folder next to this repo.
//
// Lives in visualizer/tools/ so `marked` resolves from visualizer's
// node_modules. Run as `npm run build:site` (any cwd works; paths are
// derived from this file's location).

import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { marked } from 'marked';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const out = join(repo, '_site');
const GITHUB = 'https://github.com/jonwaterschoot/TouchPlaited';

const NAV = [
  ['', 'Home'],
  ['play/', 'Play'],
  ['manual.html', 'Manual'],
  ['editor/', 'Pattern editor'],
  ['visualizer/', 'Visualizer'],
  ['codemap/', 'Code map'],
];

/** The site header, as seen from a page `depth` folders down, with `here`
 * (a NAV path) marked as the current page. */
function siteHeader(here, depth = 0) {
  const up = '../'.repeat(depth);
  const links = NAV.map(([path, text]) =>
    `      <a href="${up + path || './'}"${path === here ? ' aria-current="page"' : ''}>${text}</a>`);
  return `  <header class="site-head">
    <a class="brand" href="${up || './'}">Touch<span class="px">Plaited</span></a>
    <nav>
${links.join('\n')}
      <a href="${GITHUB}">GitHub</a>
    </nav>
  </header>`;
}

// GitHub-style heading slugs, so the README's own #anchors keep working.
const slug = (text) =>
  text.toLowerCase().replace(/<[^>]*>/g, '').replace(/[^\w -]/g, '').replace(/ /g, '-');

/** Rewrite repo-relative markdown links for the hosted site: pages we host
 * point at their hosted versions, everything else goes to GitHub. */
function rewriteLinks(md) {
  return md.replace(/\]\(([^)\s]+)\)/g, (m, url) => {
    if (/^(https?:|#|mailto:)/.test(url)) {
      // the README links to the hosted pages by full URL: keep those on this site
      const local = url.match(/^https:\/\/jonwaterschoot\.github\.io\/TouchPlaited\/(.*)$/);
      return local ? `](${local[1] || './'})` : m;
    }
    if (/^(img|docs)\//.test(url)) return m; // copied into _site
    if (url === '../../releases') return `](${GITHUB}/releases)`;
    const [path, hash = ''] = url.split('#');
    if (path === 'MANUAL.md') return `](manual.html${hash && '#' + hash})`;
    if (path === 'README.md') return `](./${hash && '#' + hash})`;
    if (path === 'tools/pattern_editor.html') return `](editor/)`;
    if (path === 'tools/codemap.html') return `](codemap/)`;
    // other repo files/folders → GitHub (dir links need /tree/, files /blob/)
    const abs = join(repo, path);
    const kind = existsSync(abs) && statSync(abs).isDirectory() ? 'tree' : 'blob';
    return `](${GITHUB}/${kind}/main/${path}${hash && '#' + hash})`;
  });
}

function renderPage(mdFile, title, outFile, here) {
  const md = rewriteLinks(readFileSync(join(repo, mdFile), 'utf8'));
  let html = marked.parse(md, { gfm: true });
  // heading ids (marked dropped built-in ids; regex keeps us version-proof)
  html = html.replace(/<h([1-6])>([\s\S]*?)<\/h\1>/g, (_, l, inner) =>
    `<h${l} id="${slug(inner)}">${inner}</h${l}>`);
  const page = template.replace('{{title}}', title).replace('{{header}}', siteHeader(here))
    .replace('{{content}}', () => html);
  writeFileSync(join(out, outFile), page);
  console.log(`  ${outFile}  ← ${mdFile}`);
}

/** play/: the web app that the designer's "Web app…" exports, taken from a
 * checkout of simple-touch-labels. Its own list of files (APP_FILES in
 * editor.html) says what to copy. Two things are added to player.html: this
 * site's header and doc/play.css. Each insert has to find its place, so a
 * change in the library's page stops the build. */
function buildPlay() {
  const src = [process.env.SIMPLE_TOUCH_LABELS, join(repo, '_labels'), resolve(repo, '..', 'simple-touch-labels')]
    .find((dir) => dir && existsSync(join(dir, 'player.html')));
  if (!src) {
    const msg = 'play/ not built: no simple-touch-labels checkout (set SIMPLE_TOUCH_LABELS, or clone it next to this repo)';
    if (process.env.CI) throw new Error(msg);
    console.warn(`  ! ${msg}`);
    return;
  }
  const dest = join(out, 'play');
  const list = readFileSync(join(src, 'editor.html'), 'utf8').match(/const APP_FILES=\[([\s\S]*?)\];/);
  if (!list) throw new Error('APP_FILES not found in simple-touch-labels/editor.html');
  const files = [...list[1].matchAll(/'([^']+)'/g)].map((m) => m[1]);
  if (!files.includes('core.js')) throw new Error('APP_FILES in simple-touch-labels/editor.html reads wrong');
  for (const f of files) {
    mkdirSync(dirname(join(dest, f)), { recursive: true });
    cpSync(join(src, f), join(dest, f));
  }
  cpSync(join(src, 'LICENSE'), join(dest, 'LICENSE-simple-touch-labels.txt'));

  let page = readFileSync(join(src, 'player.html'), 'utf8');
  const insert = (re, text, what) => {
    if (!re.test(page)) throw new Error(`player.html: ${what} not found`);
    page = page.replace(re, text);
  };
  insert(/<\/head>/, () => '<link rel="stylesheet" href="../site-head.css">\n<link rel="stylesheet" href="play.css">\n</head>', '</head>');
  insert(/<body[^>]*>/, (m) => `${m}\n${siteHeader('play/', 1)}`, '<body>');
  writeFileSync(join(dest, 'index.html'), page);
  cpSync(join(repo, 'doc', 'play.css'), join(dest, 'play.css'));

  // On localhost, and when the repository can't be read, the page shows the
  // copy beside it. The faceplate sits at the path the label file gives.
  const labels = readFileSync(join(repo, 'simple-touch-labels.json'), 'utf8');
  writeFileSync(join(dest, 'simple-touch-labels.json'), labels);
  const doc = JSON.parse(labels);
  for (const path of new Set([doc.faceplate, doc.fpDesktop?.faceplate])) {
    if (typeof path !== 'string' || !/^[\w./-]+$/.test(path) || path.includes('..')) continue;
    if (!existsSync(join(repo, path))) { console.warn(`  ! faceplate ${path} missing`); continue; }
    mkdirSync(dirname(join(dest, path)), { recursive: true });
    cpSync(join(repo, path), join(dest, path));
  }
  console.log(`  play/  ← ${src} (${files.length} files) + simple-touch-labels.json`);
}

const template = readFileSync(join(repo, 'doc', 'template.html'), 'utf8');

rmSync(out, { recursive: true, force: true });
mkdirSync(join(out, 'editor'), { recursive: true });
mkdirSync(join(out, 'codemap'), { recursive: true });

renderPage('README.md', 'TouchPlaited', 'index.html', '');
renderPage('MANUAL.md', 'TouchPlaited manual', 'manual.html', 'manual.html');

cpSync(join(repo, 'doc', 'theme.css'), join(out, 'theme.css'));
cpSync(join(repo, 'doc', 'site-head.css'), join(out, 'site-head.css'));
// web images only: img/ also holds the .ai sources, which don't belong online
cpSync(join(repo, 'img'), join(out, 'img'), {
  recursive: true,
  filter: (src) => statSync(src).isDirectory() || /\.(png|jpe?g|svg|gif|webp)$/i.test(src),
});
// the control map from the designer's repository kit, shown on the home page
cpSync(join(repo, 'docs'), join(out, 'docs'), {
  recursive: true,
  filter: (src) => statSync(src).isDirectory() || /\.(png|jpe?g|svg|gif|webp)$/i.test(src),
});
cpSync(join(repo, 'tools', 'pattern_editor.html'), join(out, 'editor', 'index.html'));
cpSync(join(repo, 'tools', 'codemap.html'), join(out, 'codemap', 'index.html'));
writeFileSync(join(out, '.nojekyll'), '');
console.log('  theme.css · site-head.css · img/ · editor/ · codemap/ · .nojekyll');

buildPlay();

const dist = join(repo, 'visualizer', 'dist');
if (existsSync(dist)) {
  cpSync(dist, join(out, 'visualizer'), { recursive: true });
  console.log('  visualizer/  ← visualizer/dist');
} else {
  console.warn('  ! visualizer/dist missing: run `npm run build` first');
}

console.log(`site assembled in ${out}`);
