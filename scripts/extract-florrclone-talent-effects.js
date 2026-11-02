'use strict';
const fs = require('fs');
const path = require('path');
const chunk = path.resolve(
  __dirname,
  '../../florrclone/florr-local-site/_next/static/chunks/page-BflkVDCC.js',
);
const s = fs.readFileSync(chunk, 'utf8');

// Find the talent-effect helper tables near the talent defs.
const anchor = s.indexOf('{id:`health`,name:`Health`');
const window = s.slice(Math.max(0, anchor - 2500), anchor + 200);
fs.writeFileSync(path.resolve(__dirname, '../docs/florrclone-talents-preamble.txt'), window);

// Pull named numeric arrays used by effect strings just before the table.
const arrays = {};
const re = /\b([a-z]{1,3})=(\[[0-9.,eE+\-\s]+\])/g;
let m;
const pre = s.slice(Math.max(0, anchor - 4000), anchor);
while ((m = re.exec(pre))) {
  try {
    arrays[m[1]] = Function(`return (${m[2]})`)();
  } catch (_) {}
}
console.log('nearby arrays', Object.keys(arrays).map((k) => `${k}:${arrays[k].length}`).join(' '));

// Also grab effect description generators nearby
const post = s.slice(anchor, anchor + 6000);
const idHits = [...post.matchAll(/id:`([a-zA-Z]+)`/g)].map((x) => x[1]);
console.log('ids', idHits.join(','));

// Find slots() / loadout slot formula
for (const needle of ['function slots', 'slots=(', 'slots(e,t)', 'talents.loadout']) {
  const i = s.indexOf(needle);
  if (i >= 0) console.log(needle, s.slice(i, i + 200).replace(/\n/g, ' '));
}

// yt reload helper
for (const needle of ['function yt(', 'yt=(e,t)', 'reloadMult']) {
  let i = 0, n = 0;
  while ((i = s.indexOf(needle, i)) >= 0 && n < 2) {
    console.log('---', needle, s.slice(i, i + 250).replace(/\n/g, ' '));
    i += needle.length;
    n++;
  }
}
