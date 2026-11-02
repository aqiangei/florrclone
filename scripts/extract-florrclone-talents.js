'use strict';
const fs = require('fs');
const path = require('path');

const chunk = path.resolve(
  __dirname,
  '../../florrclone/florr-local-site/_next/static/chunks/page-BflkVDCC.js',
);
const s = fs.readFileSync(chunk, 'utf8');

const needle = "{id:`health`,name:`Health`";
const i = s.indexOf(needle);
if (i < 0) throw new Error('talent table not found');

// Walk from first talent object through the array that contains it.
let start = i;
while (start > 0 && s[start] !== '[') start--;
let depth = 0;
let end = start;
for (; end < s.length; end++) {
  const c = s[end];
  if (c === '[') depth++;
  else if (c === ']') {
    depth--;
    if (depth === 0) {
      end++;
      break;
    }
  }
}
const raw = s.slice(start, end);
fs.writeFileSync(path.resolve(__dirname, '../docs/florrclone-talents.raw.js'), raw);

// Evaluate with the referenced helper arrays stubbed as empty where needed —
// costs/rarity/effect strings are literal enough after a light rewrite.
const rewritten = raw
  .replace(/`([^`]*)`/g, (_, t) => JSON.stringify(t))
  .replace(/effect:[a-zA-Z_$][\w$]*\.map\([^)]*\)/g, 'effect:[]')
  .replace(/effect:Array\.from\([^)]*\)/g, 'effect:[]')
  .replace(/effect:[a-zA-Z_$][\w$]*/g, 'effect:[]')
  .replace(/requires:\{id:("[^"]+"),level:(\d+)\}/g, 'requires:{id:$1,level:$2}');

let talents;
try {
  // eslint-disable-next-line no-new-func
  talents = Function(`"use strict"; return (${rewritten});`)();
} catch (err) {
  console.error('eval failed', err.message);
  // Fallback: regex-parse id/costs/rarity/requires
  talents = [];
  const re =
    /\{id:"([^"]+)",name:"([^"]+)",zh:"([^"]*)",costs:(\[[^\]]*\]),rarity:(\[[^\]]*\])(?:,requires:\{id:"([^"]+)",level:(\d+)\})?/g;
  let m;
  while ((m = re.exec(rewritten))) {
    talents.push({
      id: m[1],
      name: m[2],
      zh: m[3],
      costs: JSON.parse(m[4]),
      rarity: JSON.parse(m[5]),
      requires: m[6] ? { id: m[6], level: Number(m[7]) } : null,
    });
  }
}

const out = path.resolve(__dirname, '../docs/florrclone-talents.json');
fs.writeFileSync(out, JSON.stringify(talents, null, 2));
console.log('wrote', out, 'count', talents.length);
for (const t of talents) {
  console.log(
    t.id,
    'max',
    t.costs?.length,
    'costs',
    JSON.stringify(t.costs),
    t.requires ? `req ${t.requires.id}@${t.requires.level}` : '',
  );
}
