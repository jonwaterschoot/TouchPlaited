// Assembles the GitHub Pages site into <repo>/_site/ (see doc/SITE-PLAN.md):
//
//   index.html   ← README.md rendered through doc/template.html
//   manual.html  ← MANUAL.md, same template
//   theme.css    ← doc/theme.css
//   img/         ← repo img/ (README/MANUAL images)
//   editor/      ← tools/pattern_editor.html, copied verbatim
//   visualizer/  ← visualizer/dist (run `npm run build` first)
//
// Lives in visualizer/tools/ so `marked` resolves from visualizer's
// node_modules. Run as `npm run build:site` (any cwd works — paths are
// derived from this file's location).

import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { marked } from 'marked';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const out = join(repo, '_site');
const GITHUB = 'https://github.com/jonwaterschoot/TouchPlaited';

// GitHub-style heading slugs, so the README's own #anchors keep working.
const slug = (text) =>
  text.toLowerCase().replace(/<[^>]*>/g, '').replace(/[^\w -]/g, '').replace(/ /g, '-');

/** Rewrite repo-relative markdown links for the hosted site: pages we host
 * point at their hosted versions, everything else goes to GitHub. */
function rewriteLinks(md) {
  return md.replace(/\]\(([^)\s]+)\)/g, (m, url) => {
    if (/^(https?:|#|mailto:)/.test(url)) return m;
    if (/^img\//.test(url)) return m; // copied into _site
    if (url === '../../releases') return `](${GITHUB}/releases)`;
    const [path, hash = ''] = url.split('#');
    if (path === 'MANUAL.md') return `](manual.html${hash && '#' + hash})`;
    if (path === 'README.md') return `](./${hash && '#' + hash})`;
    if (path === 'tools/pattern_editor.html') return `](editor/)`;
    // other repo files/folders → GitHub (dir links need /tree/, files /blob/)
    const abs = join(repo, path);
    const kind = existsSync(abs) && statSync(abs).isDirectory() ? 'tree' : 'blob';
    return `](${GITHUB}/${kind}/main/${path}${hash && '#' + hash})`;
  });
}

function renderPage(mdFile, title, outFile) {
  const md = rewriteLinks(readFileSync(join(repo, mdFile), 'utf8'));
  let html = marked.parse(md, { gfm: true });
  // heading ids (marked dropped built-in ids; regex keeps us version-proof)
  html = html.replace(/<h([1-6])>([\s\S]*?)<\/h\1>/g, (_, l, inner) =>
    `<h${l} id="${slug(inner)}">${inner}</h${l}>`);
  const page = template.replace('{{title}}', title).replace('{{content}}', html);
  writeFileSync(join(out, outFile), page);
  console.log(`  ${outFile}  ← ${mdFile}`);
}

const template = readFileSync(join(repo, 'doc', 'template.html'), 'utf8');

rmSync(out, { recursive: true, force: true });
mkdirSync(join(out, 'editor'), { recursive: true });

renderPage('README.md', 'TouchPlaited', 'index.html');
renderPage('MANUAL.md', 'TouchPlaited — Manual', 'manual.html');

cpSync(join(repo, 'doc', 'theme.css'), join(out, 'theme.css'));
// web images only — img/ also holds the .ai sources, which don't belong online
cpSync(join(repo, 'img'), join(out, 'img'), {
  recursive: true,
  filter: (src) => statSync(src).isDirectory() || /\.(png|jpe?g|svg|gif|webp)$/i.test(src),
});
cpSync(join(repo, 'tools', 'pattern_editor.html'), join(out, 'editor', 'index.html'));
writeFileSync(join(out, '.nojekyll'), '');
console.log('  theme.css · img/ · editor/ · .nojekyll');

const dist = join(repo, 'visualizer', 'dist');
if (existsSync(dist)) {
  cpSync(dist, join(out, 'visualizer'), { recursive: true });
  console.log('  visualizer/  ← visualizer/dist');
} else {
  console.warn('  ! visualizer/dist missing — run `npm run build` first');
}

console.log(`site assembled in ${out}`);
